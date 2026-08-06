/*******************************************************************************************
*
*   physics.c - Box2D world wrapper for hex-magical
*
********************************************************************************************/

#include "physics.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define BALL_USER_TAG ((intptr_t)-1)
#define WORLD_GRAVITY_Y 2100.0f
// World units are pixels; Box2D's default speed cap (400 units/s) would choke
// ramp launches, so allow genuinely fast flight
#define WORLD_MAX_SPEED 10000.0f
// Boost zones accelerate the ball along its velocity — roughly 2x gravity, strong
// enough to carry it across gaps/pits it could not clear on its own
#define BOOST_ACCEL 4200.0f
// Below this speed the ball has no meaningful direction to amplify along
#define BOOST_MIN_SPEED 1.0f

//----------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------
static b2Vec2 ToB2(Vector2 v)
{
    return (b2Vec2){ v.x, v.y };
}

static Vector2 FromB2(b2Vec2 v)
{
    return (Vector2){ v.x, v.y };
}

static void ClearDrawn(PhysicsWorld *phys)
{
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        phys->drawn[i].active = false;
        phys->drawn[i].bodyId = b2_nullBodyId;
        phys->drawn[i].pointCount = 0;
        memset(phys->drawn[i].boostSeg, 0, sizeof(phys->drawn[i].boostSeg));
    }
    phys->drawnCount = 0;
}

// Cannons, ghost trail, checkpoint and undo all reset with the level
static void ClearBuildExtras(PhysicsWorld *phys)
{
    memset(phys->cannons, 0, sizeof(phys->cannons));
    phys->trailCount = 0;
    phys->trailStepCounter = 0;
    phys->ghostCount = 0;
    phys->checkpointSet = false;
    phys->undoCount = 0;
    phys->undoApplying = false;
}

// Reserve the next undo slot (oldest action dropped when the stack is full).
// Returns NULL while PhysicsUndoLastAction runs so reverts don't re-record.
static UndoAction *PushUndo(PhysicsWorld *phys, UndoKind kind)
{
    if (phys->undoApplying) return NULL;

    if (phys->undoCount >= UNDO_MAX_ACTIONS)
    {
        memmove(&phys->undo[0], &phys->undo[1], (UNDO_MAX_ACTIONS - 1) * sizeof(UndoAction));
        phys->undoCount = UNDO_MAX_ACTIONS - 1;
    }

    UndoAction *action = &phys->undo[phys->undoCount++];
    memset(action, 0, sizeof(*action));
    action->kind = kind;
    return action;
}

// Distance from p to segment ab, plus the segment's unit tangent
static float PointSegmentDist(Vector2 p, Vector2 a, Vector2 b, Vector2 *tangent)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 0.0001f)
    {
        if (tangent) *tangent = (Vector2){ 0.0f, 0.0f };
        float ex = p.x - a.x;
        float ey = p.y - a.y;
        return sqrtf(ex * ex + ey * ey);
    }

    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float ex = p.x - (a.x + t * dx);
    float ey = p.y - (a.y + t * dy);
    float len = sqrtf(lenSq);
    if (tangent) *tangent = (Vector2){ dx / len, dy / len };
    return sqrtf(ex * ex + ey * ey);
}

static float PolylineLength(const Vector2 *points, int count)
{
    float length = 0.0f;
    for (int i = 0; i < count - 1; i++)
    {
        float dx = points[i + 1].x - points[i].x;
        float dy = points[i + 1].y - points[i].y;
        length += sqrtf(dx * dx + dy * dy);
    }
    return length;
}

static float SegLen(Vector2 a, Vector2 b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return sqrtf(dx * dx + dy * dy);
}

static int AllocDrawnSlot(PhysicsWorld *phys)
{
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        if (!phys->drawn[i].active) return i;
    }
    return -1;
}

// Subdivide spans longer than maxLen. Relaxes maxLen when the stroke would
// otherwise exceed maxOut so the full polyline (including the endpoint) is kept.
static int ResampleMaxSegLen(const Vector2 *in, int count, float maxLen, Vector2 *out, int maxOut)
{
    if ((count < 2) || (maxOut < 2)) return 0;

    // How fine can we subdivide and still fit? totalLen / (maxOut-1) is the floor.
    float totalLen = PolylineLength(in, count);
    float fitLen = totalLen / (float)(maxOut - 1);
    if (fitLen > maxLen) maxLen = fitLen;

    out[0] = in[0];
    int outCount = 1;

    for (int i = 0; i < count - 1; i++)
    {
        Vector2 a = in[i];
        Vector2 b = in[i + 1];
        float len = SegLen(a, b);
        if (len < 0.0001f)
        {
            // Skip degenerate; keep final endpoint via the next real segment
            if ((i == count - 2) && (outCount < maxOut)) out[outCount++] = b;
            continue;
        }

        int steps = (int)ceilf(len / maxLen);
        if (steps < 1) steps = 1;

        // Reserve one slot per remaining input vertex so the stroke never truncates
        int inputLeft = (count - 1) - i; // including current segment's endpoint
        int remainingSlots = maxOut - outCount;
        if (remainingSlots < inputLeft) steps = 1;
        else if (steps > remainingSlots - (inputLeft - 1)) steps = remainingSlots - (inputLeft - 1);
        if (steps < 1) steps = 1;

        for (int s = 1; s <= steps; s++)
        {
            if (outCount >= maxOut) break;
            float t = (float)s / (float)steps;
            out[outCount++] = (Vector2){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
        }
    }

    // Guarantee the original endpoint survives
    if (outCount >= 1) out[outCount - 1] = in[count - 1];
    return outCount;
}

// Map each output segment's boost from the nearest input segment (by midpoint).
static void RemapBoostMask(const Vector2 *in, int inCount, const uint8_t *inBoost,
                           const Vector2 *out, int outCount, uint8_t *outBoost)
{
    int outSegs = outCount - 1;
    if (outSegs < 0) outSegs = 0;
    memset(outBoost, 0, (size_t)outSegs);

    if ((inBoost == NULL) || (inCount < 2) || (outCount < 2)) return;

    for (int o = 0; o < outSegs; o++)
    {
        Vector2 mid = {
            0.5f * (out[o].x + out[o + 1].x),
            0.5f * (out[o].y + out[o + 1].y)
        };
        float best = 1.0e30f;
        int bestSeg = 0;
        for (int i = 0; i < inCount - 1; i++)
        {
            float d = PointSegmentDist(mid, in[i], in[i + 1], NULL);
            if (d < best)
            {
                best = d;
                bestSeg = i;
            }
        }
        outBoost[o] = inBoost[bestSeg];
    }
}

static void DrawnWorldPoints(const DrawnBody *drawn, Vector2 *worldPts)
{
    b2Transform xf = b2Body_GetTransform(drawn->bodyId);
    for (int p = 0; p < drawn->pointCount; p++)
    {
        b2Vec2 world = b2TransformPoint(xf, ToB2(drawn->localPoints[p]));
        worldPts[p] = FromB2(world);
    }
}

static void DestroyDrawnSlot(PhysicsWorld *phys, int slot)
{
    DrawnBody *drawn = &phys->drawn[slot];
    if (!drawn->active) return;
    if (b2Body_IsValid(drawn->bodyId)) b2DestroyBody(drawn->bodyId);
    drawn->active = false;
    drawn->bodyId = b2_nullBodyId;
    drawn->pointCount = 0;
    memset(drawn->boostSeg, 0, sizeof(drawn->boostSeg));
}

static void CreateStaticBoxes(PhysicsWorld *phys, const LevelDef *level)
{
    for (int i = 0; i < level->boxCount; i++)
    {
        const StaticBox *box = &level->boxes[i];

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = (b2Vec2){ box->x, box->y };
        bodyDef.rotation = b2MakeRot(box->angleDeg * DEG2RAD);
        b2BodyId bodyId = b2CreateBody(phys->worldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.material.friction = 0.6f;
        shapeDef.material.restitution = 0.1f;

        b2Polygon polygon = b2MakeBox(box->halfWidth, box->halfHeight);
        b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
    }

    phys->staticBoxes = level->boxes;
    phys->staticBoxCount = level->boxCount;
}

static void CreateStaticPolygons(PhysicsWorld *phys, const LevelDef *level)
{
    for (int i = 0; i < level->polygonCount; i++)
    {
        const StaticPolygon *source = &level->polygons[i];
        b2Vec2 points[STATIC_POLYGON_MAX_POINTS];
        for (int p = 0; p < source->pointCount; p++)
        {
            points[p] = ToB2(source->points[p]);
        }

        b2Hull hull = b2ComputeHull(points, source->pointCount);
        if (hull.count < 3)
        {
            TraceLog(LOG_ERROR, "PHYSICS: invalid static polygon %d", i);
            continue;
        }

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        b2BodyId bodyId = b2CreateBody(phys->worldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.material.friction = 0.6f;
        shapeDef.material.restitution = 0.1f;

        b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
        b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
    }
}

static void CreateBall(PhysicsWorld *phys, const LevelDef *level)
{
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = ToB2(level->ballSpawn);
    bodyDef.linearDamping = 0.0f;    // no air drag — preserve launch speed for long flights
    // Low spin damping — the ball is the only mover now (Line Rider), so it must
    // roll freely along drawn track instead of bleeding energy and stalling
    bodyDef.angularDamping = 0.05f;
    bodyDef.isBullet = true;
    // Ball sits frozen (disabled) during the build phase; PhysicsStartSimulation enables it.
    // Everything else in the world is static, so the level is fully fixed until Start.
    bodyDef.isEnabled = false;
    phys->ballId = b2CreateBody(phys->worldId, &bodyDef);
    phys->ballRadius = level->ballRadius;
    phys->ballSpawn = level->ballSpawn;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = phys->tunables.ballDensity;
    shapeDef.material.friction = 0.4f;
    shapeDef.material.restitution = phys->tunables.ballRestitution;

    b2Circle circle = { 0 };
    circle.center = (b2Vec2){ 0.0f, 0.0f };
    circle.radius = level->ballRadius;
    b2CreateCircleShape(phys->ballId, &shapeDef, &circle);

    // Tag ball so erase queries skip it
    b2Body_SetUserData(phys->ballId, (void *)BALL_USER_TAG);
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void PhysicsTunablesDefaults(PhysicsTunables *t)
{
    t->ballDensity = TUNE_BALL_DENSITY_DEFAULT;
    t->ballRestitution = TUNE_BALL_RESTITUTION_DEFAULT;
    t->dropForce = TUNE_DROP_FORCE_DEFAULT;
    t->boostVelRate = TUNE_BOOST_VEL_RATE_DEFAULT;
    t->boostVelMax = TUNE_BOOST_VEL_MAX_DEFAULT;
}

void PhysicsApplyBallTunables(PhysicsWorld *phys)
{
    if (!phys->valid || !b2Body_IsValid(phys->ballId)) return;

    b2ShapeId shapes[1];
    int count = b2Body_GetShapes(phys->ballId, shapes, 1);
    if (count < 1) return;

    b2Shape_SetDensity(shapes[0], phys->tunables.ballDensity, true);
    b2Shape_SetRestitution(shapes[0], phys->tunables.ballRestitution);
}

void PhysicsInit(PhysicsWorld *phys)
{
    memset(phys, 0, sizeof(*phys));
    phys->worldId = b2_nullWorldId;
    phys->ballId = b2_nullBodyId;
    phys->simulating = false;
    PhysicsTunablesDefaults(&phys->tunables);
    ClearDrawn(phys);
}

void PhysicsShutdown(PhysicsWorld *phys)
{
    if (phys->valid)
    {
        b2DestroyWorld(phys->worldId);
        phys->valid = false;
        phys->worldId = b2_nullWorldId;
    }
    ClearDrawn(phys);
}

void PhysicsLoadLevel(PhysicsWorld *phys, const LevelDef *level)
{
    if (phys->valid)
    {
        b2DestroyWorld(phys->worldId);
        phys->valid = false;
    }

    ClearDrawn(phys);
    ClearBuildExtras(phys);
    phys->accumulator = 0.0f;
    phys->simulating = false;

    phys->lineCapacity = level->lineCapacity;
    phys->boostLineCapacity = level->boostLineCapacity;
    phys->cannonCapacity = level->cannonCount;

    b2WorldDef worldDef = b2DefaultWorldDef();
    // Gravity is always on — drawn strokes are static (Line Rider style) and the
    // ball is disabled until Start, so nothing can move during the build phase.
    // Screen Y grows downward — gravity points "down" the screen.
    worldDef.gravity = (b2Vec2){ 0.0f, WORLD_GRAVITY_Y };
    worldDef.maximumLinearSpeed = WORLD_MAX_SPEED;
    phys->worldId = b2CreateWorld(&worldDef);
    phys->valid = true;

    CreateStaticBoxes(phys, level);
    CreateStaticPolygons(phys, level);
    CreateBall(phys, level);

    phys->finishLine = level->finishLine;
    phys->pits = level->pits;
    phys->pitCount = level->pitCount;
    phys->boosts = level->boosts;
    phys->boostCount = level->boostCount;
    phys->antiGravity = level->antiGravity;
    phys->antiGravityCount = level->antiGravityCount;
}

void PhysicsStartSimulation(PhysicsWorld *phys)
{
    if (!phys->valid || phys->simulating) return;

    phys->simulating = true;
    phys->trailCount = 0;
    phys->trailStepCounter = 0;
    phys->boostLineAccel = 0.0f;
    for (int i = 0; i < MAX_CANNONS; i++) phys->cannons[i].cooldown = 0.0f;

    if (b2Body_IsValid(phys->ballId))
    {
        // Ball was created disabled — Start is the only mutation the world ever sees,
        // so the post-Start run is a pure function of (level, strokes, tunables).
        b2Body_Enable(phys->ballId);

        if (phys->checkpointSet)
        {
            // Resume mid-run from the flagged ghost sample: position, spin and
            // velocity are restored exactly as recorded — an iteration tool, so
            // track edited before the flag won't retroactively change this state
            b2Body_SetTransform(phys->ballId, ToB2(phys->checkpoint.pos), b2MakeRot(phys->checkpoint.angle));
            b2Body_SetLinearVelocity(phys->ballId, ToB2(phys->checkpoint.vel));
            b2Body_SetAngularVelocity(phys->ballId, phys->checkpoint.angularVel);
        }
        else if (phys->tunables.dropForce > 0.0f)
        {
            // Initial downward kick on top of gravity (admin tunable)
            b2Body_SetLinearVelocity(phys->ballId, (b2Vec2){ 0.0f, phys->tunables.dropForce });
        }
    }
}

// Stop: back to build phase. Strokes are static so they are untouched — only the
// ball needs resetting. Player can draw/erase/save/load/tweak, then Start again.
void PhysicsStopSimulation(PhysicsWorld *phys)
{
    if (!phys->valid || !phys->simulating) return;

    phys->simulating = false;
    phys->accumulator = 0.0f;
    phys->boostLineAccel = 0.0f;

    // The finished recording becomes the ghost trail shown during build
    if (phys->trailCount >= 2)
    {
        memcpy(phys->ghost, phys->trail, (size_t)phys->trailCount * sizeof(TrailSample));
        phys->ghostCount = phys->trailCount;
    }

    if (b2Body_IsValid(phys->ballId))
    {
        b2Body_SetLinearVelocity(phys->ballId, (b2Vec2){ 0.0f, 0.0f });
        b2Body_SetAngularVelocity(phys->ballId, 0.0f);
        b2Body_SetTransform(phys->ballId, ToB2(phys->ballSpawn), b2MakeRot(0.0f));
        b2Body_Disable(phys->ballId); // frozen again until the next Start
    }
}

bool PhysicsIsSimulating(const PhysicsWorld *phys)
{
    return phys->valid && phys->simulating;
}

// Default gravity is down (0, +Y). An anti-gravity zone rotates that vector
// clockwise by gravityAngleDeg on screen (Y-down): 90→left, 180→up, 270→right.
static void ApplyAntiGravityZones(PhysicsWorld *phys)
{
    b2Vec2 gravity = { 0.0f, WORLD_GRAVITY_Y };
    if ((phys->antiGravityCount > 0) && b2Body_IsValid(phys->ballId))
    {
        Vector2 ball = FromB2(b2Body_GetPosition(phys->ballId));
        for (int z = 0; z < phys->antiGravityCount; z++)
        {
            if (!PolyZoneContains(&phys->antiGravity[z].zone, ball)) continue;
            float rad = phys->antiGravity[z].gravityAngleDeg * DEG2RAD;
            gravity.x = -WORLD_GRAVITY_Y * sinf(rad);
            gravity.y = WORLD_GRAVITY_Y * cosf(rad);
            break; // first containing zone wins
        }
    }
    b2World_SetGravity(phys->worldId, gravity);
}

// While the ball sits inside a boost zone, push it along its velocity so it can
// carry across gaps (no-build, pits) it could not clear on its own
static void ApplyBoostZones(PhysicsWorld *phys)
{
    if ((phys->boostCount <= 0) || !b2Body_IsValid(phys->ballId)) return;

    Vector2 ball = FromB2(b2Body_GetPosition(phys->ballId));
    bool inside = false;
    for (int z = 0; z < phys->boostCount; z++)
    {
        if (PolyZoneContains(&phys->boosts[z], ball)) { inside = true; break; }
    }
    if (!inside) return;

    b2Vec2 vel = b2Body_GetLinearVelocity(phys->ballId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    if (speed < BOOST_MIN_SPEED) return;

    float mass = b2Body_GetMass(phys->ballId);
    b2Vec2 force = { vel.x / speed * BOOST_ACCEL * mass, vel.y / speed * BOOST_ACCEL * mass };
    b2Body_ApplyForceToCenter(phys->ballId, force, true);
}

// While near a boosted crayon segment, gradually build acceleration along the
// ball's current velocity (no steering). Charge resets when leaving every boost.
static void ApplyBoostSegments(PhysicsWorld *phys, float step)
{
    if (!b2Body_IsValid(phys->ballId))
    {
        phys->boostLineAccel = 0.0f;
        return;
    }

    Vector2 ball = FromB2(b2Body_GetPosition(phys->ballId));
    bool onBoost = false;
    Vector2 worldPts[MAX_STROKE_POINTS];

    for (int d = 0; d < MAX_DRAWN_BODIES && !onBoost; d++)
    {
        const DrawnBody *drawn = &phys->drawn[d];
        if (!drawn->active || (drawn->pointCount < 2)) continue;

        DrawnWorldPoints(drawn, worldPts);
        for (int i = 0; i < drawn->pointCount - 1; i++)
        {
            if (!drawn->boostSeg[i]) continue;
            if (PointSegmentDist(ball, worldPts[i], worldPts[i + 1], NULL) < BOOST_LINE_RADIUS)
            {
                onBoost = true;
                break;
            }
        }
    }

    if (!onBoost)
    {
        phys->boostLineAccel = 0.0f;
        return;
    }

    float maxAccel = phys->tunables.boostVelMax;
    if (maxAccel < 0.0f) maxAccel = 0.0f;
    phys->boostLineAccel += phys->tunables.boostVelRate * step;
    if (phys->boostLineAccel > maxAccel) phys->boostLineAccel = maxAccel;
    if (phys->boostLineAccel <= 0.0f) return;

    b2Vec2 vel = b2Body_GetLinearVelocity(phys->ballId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    if (speed < BOOST_MIN_SPEED) return;

    float mass = b2Body_GetMass(phys->ballId);
    b2Vec2 force = {
        vel.x / speed * phys->boostLineAccel * mass,
        vel.y / speed * phys->boostLineAccel * mass
    };
    b2Body_ApplyForceToCenter(phys->ballId, force, true);
}

// Ball entering a cannon's muzzle circle is relaunched along the barrel.
// The cooldown stops the same cannon re-firing every step while the ball leaves.
static void ApplyCannons(PhysicsWorld *phys, float step)
{
    if (!b2Body_IsValid(phys->ballId)) return;

    Vector2 ball = FromB2(b2Body_GetPosition(phys->ballId));
    for (int i = 0; i < MAX_CANNONS; i++)
    {
        Cannon *cannon = &phys->cannons[i];
        if (!cannon->active) continue;

        if (cannon->cooldown > 0.0f)
        {
            cannon->cooldown -= step;
            continue;
        }

        float dx = ball.x - cannon->pos.x;
        float dy = ball.y - cannon->pos.y;
        if ((dx * dx + dy * dy) > (CANNON_ENTRY_RADIUS * CANNON_ENTRY_RADIUS)) continue;

        b2Vec2 blast = {
            cosf(cannon->angleRad) * CANNON_BLAST_SPEED,
            sinf(cannon->angleRad) * CANNON_BLAST_SPEED
        };
        b2Body_SetTransform(phys->ballId, ToB2(cannon->pos), b2Body_GetRotation(phys->ballId));
        b2Body_SetLinearVelocity(phys->ballId, blast);
        cannon->cooldown = CANNON_COOLDOWN;
    }
}

// Sample the ball every TRAIL_SAMPLE_STRIDE fixed steps for the ghost trail
static void RecordTrailSample(PhysicsWorld *phys)
{
    if (!b2Body_IsValid(phys->ballId)) return;

    phys->trailStepCounter++;
    if ((phys->trailStepCounter % TRAIL_SAMPLE_STRIDE) != 0) return;
    if (phys->trailCount >= TRAIL_MAX_SAMPLES) return;

    TrailSample *sample = &phys->trail[phys->trailCount++];
    sample->pos = FromB2(b2Body_GetPosition(phys->ballId));
    sample->vel = FromB2(b2Body_GetLinearVelocity(phys->ballId));
    sample->angle = b2Rot_GetAngle(b2Body_GetRotation(phys->ballId));
    sample->angularVel = b2Body_GetAngularVelocity(phys->ballId);
}

void PhysicsStep(PhysicsWorld *phys, float dt)
{
    if (!phys->valid || !phys->simulating) return;

    if (dt > 0.05f) dt = 0.05f;

    const float step = 1.0f / PHYSICS_HZ;
    phys->accumulator += dt;

    while (phys->accumulator >= step)
    {
        ApplyAntiGravityZones(phys);
        ApplyBoostZones(phys);
        ApplyBoostSegments(phys, step);
        ApplyCannons(phys, step);
        b2World_Step(phys->worldId, step, PHYSICS_SUBSTEPS);
        RecordTrailSample(phys);
        phys->accumulator -= step;
    }
}

Vector2 PhysicsGetBallPos(const PhysicsWorld *phys)
{
    if (!phys->valid || !b2Body_IsValid(phys->ballId)) return (Vector2){ 0 };
    return FromB2(b2Body_GetPosition(phys->ballId));
}

float PhysicsGetBallAngle(const PhysicsWorld *phys)
{
    if (!phys->valid || !b2Body_IsValid(phys->ballId)) return 0.0f;
    return b2Rot_GetAngle(b2Body_GetRotation(phys->ballId));
}

bool PhysicsCheckWin(const PhysicsWorld *phys)
{
    if (!phys->valid) return false;
    return PolyZoneContains(&phys->finishLine, PhysicsGetBallPos(phys));
}

bool PhysicsCheckPit(const PhysicsWorld *phys)
{
    if (!phys->valid) return false;
    Vector2 ball = PhysicsGetBallPos(phys);
    for (int z = 0; z < phys->pitCount; z++)
    {
        if (PolyZoneContains(&phys->pits[z], ball)) return true;
    }
    return false;
}

b2Transform PhysicsGetBodyTransform(b2BodyId bodyId)
{
    return b2Body_GetTransform(bodyId);
}

DrawnBody *PhysicsGetDrawn(PhysicsWorld *phys, int index)
{
    if ((index < 0) || (index >= MAX_DRAWN_BODIES)) return NULL;
    return &phys->drawn[index];
}

int PhysicsCreateDrawnBody(PhysicsWorld *phys, const Vector2 *worldPoints, int count,
                           Color color, const uint8_t *boostSeg)
{
    if (!phys->valid || (count < 2)) return -1;

    int slot = AllocDrawnSlot(phys);
    if (slot < 0) return -1;

    // Bound + subdivide long spans for paint/erase granularity
    Vector2 capped[MAX_STROKE_POINTS];
    int cappedCount = (count < MAX_STROKE_POINTS) ? count : MAX_STROKE_POINTS;
    for (int i = 0; i < cappedCount; i++) capped[i] = worldPoints[i];

    Vector2 path[MAX_STROKE_POINTS];
    int pathCount = ResampleMaxSegLen(capped, cappedCount, STROKE_MAX_SEG_LEN, path, MAX_STROKE_POINTS);
    if (pathCount < 2) return -1;

    uint8_t mappedBoost[MAX_STROKE_SEGS];
    RemapBoostMask(capped, cappedCount, boostSeg, path, pathCount, mappedBoost);

    // Centroid — body origin; local points stay axis-aligned to world at creation
    Vector2 centroid = { 0 };
    for (int i = 0; i < pathCount; i++)
    {
        centroid.x += path[i].x;
        centroid.y += path[i].y;
    }
    centroid.x /= (float)pathCount;
    centroid.y /= (float)pathCount;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    // Line Rider style: strokes are fixed track, suspended in space where drawn
    bodyDef.type = b2_staticBody;
    bodyDef.position = ToB2(centroid);
    bodyDef.rotation = b2MakeRot(0.0f);
    b2BodyId bodyId = b2CreateBody(phys->worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.material.friction = 0.55f;
    shapeDef.material.restitution = 0.1f;

    // Capsule chain along the stroke — collider follows the ink, including concavities
    const float minSegLen = 1.0f;
    int capsules = 0;
    for (int i = 0; i < pathCount - 1; i++)
    {
        float segLen = SegLen(path[i], path[i + 1]);
        if (segLen < minSegLen) continue;

        b2Capsule capsule = { 0 };
        capsule.center1 = (b2Vec2){ path[i].x - centroid.x, path[i].y - centroid.y };
        capsule.center2 = (b2Vec2){ path[i + 1].x - centroid.x, path[i + 1].y - centroid.y };
        capsule.radius = STROKE_PHYSICS_RADIUS;
        b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        capsules++;
    }

    // Degenerate stroke (all points stacked): single circle at centroid
    if (capsules == 0)
    {
        b2Circle circle = { 0 };
        circle.center = (b2Vec2){ 0.0f, 0.0f };
        circle.radius = STROKE_PHYSICS_RADIUS;
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
    }

    DrawnBody *drawn = &phys->drawn[slot];
    drawn->active = true;
    drawn->bodyId = bodyId;
    drawn->crayonColor = color;
    drawn->pointCount = pathCount;
    memset(drawn->boostSeg, 0, sizeof(drawn->boostSeg));
    for (int i = 0; i < pathCount - 1; i++) drawn->boostSeg[i] = mappedBoost[i];

    for (int i = 0; i < pathCount; i++)
    {
        drawn->localPoints[i].x = path[i].x - centroid.x;
        drawn->localPoints[i].y = path[i].y - centroid.y;
    }

    // Tag as drawn so erase can find it: store slot+1 (non-zero)
    b2Body_SetUserData(bodyId, (void *)(intptr_t)(slot + 1));

    if (slot >= phys->drawnCount) phys->drawnCount = slot + 1;

    UndoAction *undo = PushUndo(phys, UNDO_DRAW_STROKE);
    if (undo) undo->slot = slot;

    return slot;
}

//----------------------------------------------------------------------------------
// Partial erase — clip polyline against erase circle, spawn surviving pieces
//----------------------------------------------------------------------------------
static bool PointInCircle(Vector2 p, Vector2 c, float r)
{
    float dx = p.x - c.x;
    float dy = p.y - c.y;
    return (dx * dx + dy * dy) <= (r * r);
}

// Solve |a + t(b-a) - c| = r for t in (0,1). Returns 0..2 sorted ascending.
static int SegmentCircleHits(Vector2 a, Vector2 b, Vector2 c, float r, float *tOut)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float fx = a.x - c.x;
    float fy = a.y - c.y;
    float A = dx * dx + dy * dy;
    float B = 2.0f * (fx * dx + fy * dy);
    float C = fx * fx + fy * fy - r * r;
    if (A < 0.0001f) return 0;

    float disc = B * B - 4.0f * A * C;
    if (disc < 0.0f) return 0;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-B - sqrtDisc) / (2.0f * A);
    float t1 = (-B + sqrtDisc) / (2.0f * A);
    int n = 0;
    if ((t0 > 0.0f) && (t0 < 1.0f)) tOut[n++] = t0;
    if ((t1 > 0.0f) && (t1 < 1.0f) && (fabsf(t1 - t0) > 0.0001f)) tOut[n++] = t1;
    if ((n == 2) && (tOut[0] > tOut[1]))
    {
        float tmp = tOut[0];
        tOut[0] = tOut[1];
        tOut[1] = tmp;
    }
    return n;
}

static Vector2 Lerp2(Vector2 a, Vector2 b, float t)
{
    return (Vector2){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

typedef struct EraseRun
{
    Vector2 points[MAX_STROKE_POINTS];
    uint8_t boostSeg[MAX_STROKE_SEGS];
    int pointCount;
} EraseRun;

static void EraseRunAppend(EraseRun *run, Vector2 p, uint8_t boostFromPrev)
{
    if (run->pointCount >= MAX_STROKE_POINTS) return;
    if (run->pointCount > 0)
    {
        int seg = run->pointCount - 1;
        if (seg < MAX_STROKE_SEGS) run->boostSeg[seg] = boostFromPrev;
    }
    run->points[run->pointCount++] = p;
}

static void EraseRunFlush(EraseRun *run, EraseRun *runs, int *runCount, int maxRuns)
{
    if (run->pointCount < 2) { run->pointCount = 0; return; }
    if (PolylineLength(run->points, run->pointCount) < ERASE_MIN_PIECE_LEN)
    {
        run->pointCount = 0;
        return;
    }
    if (*runCount >= maxRuns)
    {
        TraceLog(LOG_WARNING, "PHYSICS: erase produced more than %d pieces", maxRuns);
        run->pointCount = 0;
        return;
    }
    runs[(*runCount)++] = *run;
    run->pointCount = 0;
}

// Clip a world-space polyline against the erase circle; emit surviving runs.
static int CarvePolyline(const Vector2 *pts, int count, const uint8_t *boostSeg,
                         Vector2 center, float radius, EraseRun *runs, int maxRuns)
{
    int runCount = 0;
    EraseRun cur = { 0 };

    for (int i = 0; i < count - 1; i++)
    {
        Vector2 a = pts[i];
        Vector2 b = pts[i + 1];
        uint8_t segBoost = boostSeg ? boostSeg[i] : 0;
        bool aIn = PointInCircle(a, center, radius);
        bool bIn = PointInCircle(b, center, radius);
        float hits[2];
        int hitCount = SegmentCircleHits(a, b, center, radius, hits);

        // Build ordered sample params along the segment: 0, hits..., 1
        float ts[4];
        int tn = 0;
        ts[tn++] = 0.0f;
        for (int h = 0; h < hitCount; h++) ts[tn++] = hits[h];
        ts[tn++] = 1.0f;

        for (int s = 0; s < tn - 1; s++)
        {
            float t0 = ts[s];
            float t1 = ts[s + 1];
            if (t1 - t0 < 0.0001f) continue;

            Vector2 p0 = Lerp2(a, b, t0);
            Vector2 p1 = Lerp2(a, b, t1);
            Vector2 mid = Lerp2(a, b, 0.5f * (t0 + t1));
            bool midIn = PointInCircle(mid, center, radius);
            if (midIn) continue; // drop interior portion

            // Outside subsegment — stitch into current run or start a new one
            if (cur.pointCount == 0)
            {
                EraseRunAppend(&cur, p0, 0);
                EraseRunAppend(&cur, p1, segBoost);
            }
            else
            {
                Vector2 last = cur.points[cur.pointCount - 1];
                float gap = SegLen(last, p0);
                if (gap > 0.5f)
                {
                    // Discontinuity (chord through the circle) — flush and restart
                    EraseRunFlush(&cur, runs, &runCount, maxRuns);
                    EraseRunAppend(&cur, p0, 0);
                    EraseRunAppend(&cur, p1, segBoost);
                }
                else
                {
                    EraseRunAppend(&cur, p1, segBoost);
                }
            }
        }

        (void)aIn;
        (void)bIn;
    }

    EraseRunFlush(&cur, runs, &runCount, maxRuns);
    return runCount;
}

static int FindNearestDrawnStroke(PhysicsWorld *phys, Vector2 worldPoint, float radius)
{
    float best = radius;
    int bestSlot = -1;
    Vector2 worldPts[MAX_STROKE_POINTS];

    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        const DrawnBody *drawn = &phys->drawn[i];
        if (!drawn->active || (drawn->pointCount < 2)) continue;

        DrawnWorldPoints(drawn, worldPts);
        for (int p = 0; p < drawn->pointCount - 1; p++)
        {
            float d = PointSegmentDist(worldPoint, worldPts[p], worldPts[p + 1], NULL);
            if (d < best)
            {
                best = d;
                bestSlot = i;
            }
        }
    }
    return bestSlot;
}

bool PhysicsEraseAtPoint(PhysicsWorld *phys, Vector2 worldPoint)
{
    if (!phys->valid) return false;

    int slot = FindNearestDrawnStroke(phys, worldPoint, ERASE_RADIUS);
    if (slot < 0) return false;

    DrawnBody *drawn = &phys->drawn[slot];
    Vector2 worldPts[MAX_STROKE_POINTS];
    DrawnWorldPoints(drawn, worldPts);

    uint8_t origBoost[MAX_STROKE_SEGS];
    memcpy(origBoost, drawn->boostSeg, sizeof(origBoost));
    Color origColor = drawn->crayonColor;
    int origCount = drawn->pointCount;
    Vector2 origPts[MAX_STROKE_POINTS];
    memcpy(origPts, worldPts, (size_t)origCount * sizeof(Vector2));

    EraseRun runs[UNDO_MAX_PIECES];
    int runCount = CarvePolyline(worldPts, drawn->pointCount, drawn->boostSeg,
                                 worldPoint, ERASE_RADIUS, runs, UNDO_MAX_PIECES);

    // Record undo before destroying (piece slots filled after recreate)
    UndoAction *undo = PushUndo(phys, UNDO_SPLIT_STROKE);
    if (undo)
    {
        undo->pointCount = origCount;
        undo->color = origColor;
        memcpy(undo->points, origPts, (size_t)origCount * sizeof(Vector2));
        memcpy(undo->boostSeg, origBoost, sizeof(origBoost));
        undo->pieceCount = 0;
    }

    DestroyDrawnSlot(phys, slot);

    phys->undoApplying = true; // piece creates must not push UNDO_DRAW_STROKE
    for (int r = 0; r < runCount; r++)
    {
        int pieceSlot = PhysicsCreateDrawnBody(phys, runs[r].points, runs[r].pointCount,
                                               origColor, runs[r].boostSeg);
        if ((pieceSlot >= 0) && undo && (undo->pieceCount < UNDO_MAX_PIECES))
        {
            undo->pieceSlots[undo->pieceCount++] = pieceSlot;
        }
    }
    phys->undoApplying = false;

    return true;
}

//----------------------------------------------------------------------------------
// Boost paint on existing crayon strokes
//----------------------------------------------------------------------------------
int PhysicsPaintBoostAt(PhysicsWorld *phys, Vector2 worldPoint, bool apply, uint8_t *prevMask)
{
    if (!phys->valid) return -1;

    int slot = FindNearestDrawnStroke(phys, worldPoint, BOOST_PAINT_RADIUS);
    if (slot < 0) return -1;

    DrawnBody *drawn = &phys->drawn[slot];
    Vector2 worldPts[MAX_STROKE_POINTS];
    DrawnWorldPoints(drawn, worldPts);

    uint8_t before[MAX_STROKE_SEGS];
    memcpy(before, drawn->boostSeg, sizeof(before));

    float boostUsed = PhysicsBoostInkUsed(phys);
    float capacity = phys->boostLineCapacity;
    bool changed = false;

    for (int i = 0; i < drawn->pointCount - 1; i++)
    {
        float d = PointSegmentDist(worldPoint, worldPts[i], worldPts[i + 1], NULL);
        if (d > BOOST_PAINT_RADIUS) continue;

        float len = SegLen(worldPts[i], worldPts[i + 1]);
        if (apply)
        {
            if (drawn->boostSeg[i]) continue;
            // Budget: refuse segments that would exceed capacity
            if ((boostUsed + len) > capacity + 0.01f) continue;
            drawn->boostSeg[i] = 1;
            boostUsed += len;
            changed = true;
        }
        else
        {
            if (!drawn->boostSeg[i]) continue;
            drawn->boostSeg[i] = 0;
            changed = true;
        }
    }

    if (!changed) return -1;

    if (prevMask) memcpy(prevMask, before, sizeof(before));
    return slot;
}

void PhysicsRecordPaintBoostUndo(PhysicsWorld *phys, int slot, const uint8_t *prevMask)
{
    UndoAction *undo = PushUndo(phys, UNDO_PAINT_BOOST);
    if (!undo) return;
    undo->slot = slot;
    memcpy(undo->boostSeg, prevMask, sizeof(undo->boostSeg));
}

int PhysicsAddCannon(PhysicsWorld *phys, Vector2 pos, float angleRad)
{
    if (!phys->valid) return -1;

    for (int i = 0; i < MAX_CANNONS; i++)
    {
        Cannon *cannon = &phys->cannons[i];
        if (cannon->active) continue;

        cannon->active = true;
        cannon->pos = pos;
        cannon->angleRad = angleRad;
        cannon->cooldown = 0.0f;

        UndoAction *undo = PushUndo(phys, UNDO_ADD_CANNON);
        if (undo) undo->slot = i;

        return i;
    }
    return -1;
}

bool PhysicsEraseCannonAt(PhysicsWorld *phys, Vector2 worldPoint)
{
    for (int i = 0; i < MAX_CANNONS; i++)
    {
        Cannon *cannon = &phys->cannons[i];
        if (!cannon->active) continue;

        float dx = worldPoint.x - cannon->pos.x;
        float dy = worldPoint.y - cannon->pos.y;
        if ((dx * dx + dy * dy) <= (CANNON_ENTRY_RADIUS * CANNON_ENTRY_RADIUS))
        {
            UndoAction *undo = PushUndo(phys, UNDO_ERASE_CANNON);
            if (undo)
            {
                undo->pos = cannon->pos;
                undo->angleRad = cannon->angleRad;
            }

            cannon->active = false;
            return true;
        }
    }
    return false;
}

int PhysicsActiveCannonCount(const PhysicsWorld *phys)
{
    int count = 0;
    for (int i = 0; i < MAX_CANNONS; i++)
    {
        if (phys->cannons[i].active) count++;
    }
    return count;
}

//----------------------------------------------------------------------------------
// Undo — pop the last recorded build action and revert it
//----------------------------------------------------------------------------------
bool PhysicsUndoLastAction(PhysicsWorld *phys)
{
    if (!phys->valid || (phys->undoCount <= 0)) return false;

    UndoAction *action = &phys->undo[--phys->undoCount];
    phys->undoApplying = true; // reverts below must not push new undo entries

    switch (action->kind)
    {
        case UNDO_DRAW_STROKE:
        {
            DestroyDrawnSlot(phys, action->slot);
        } break;
        case UNDO_ADD_CANNON:
        {
            phys->cannons[action->slot].active = false;
        } break;
        case UNDO_SPLIT_STROKE:
        {
            for (int i = 0; i < action->pieceCount; i++)
            {
                DestroyDrawnSlot(phys, action->pieceSlots[i]);
            }
            PhysicsCreateDrawnBody(phys, action->points, action->pointCount,
                                   action->color, action->boostSeg);
        } break;
        case UNDO_PAINT_BOOST:
        {
            DrawnBody *drawn = &phys->drawn[action->slot];
            if (drawn->active)
            {
                memcpy(drawn->boostSeg, action->boostSeg, sizeof(drawn->boostSeg));
            }
        } break;
        case UNDO_ERASE_CANNON:
        {
            PhysicsAddCannon(phys, action->pos, action->angleRad);
        } break;
        default: break;
    }

    phys->undoApplying = false;
    return true;
}

//----------------------------------------------------------------------------------
// Ink accounting — recomputed from live geometry so erasing refunds for free
//----------------------------------------------------------------------------------
float PhysicsDrawnInkUsed(const PhysicsWorld *phys)
{
    float used = 0.0f;
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        const DrawnBody *drawn = &phys->drawn[i];
        if (!drawn->active) continue;
        // Local points preserve distances (static, no scale) — length == world length
        used += PolylineLength(drawn->localPoints, drawn->pointCount);
    }
    return used;
}

float PhysicsBoostInkUsed(const PhysicsWorld *phys)
{
    float used = 0.0f;
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        const DrawnBody *drawn = &phys->drawn[i];
        if (!drawn->active || (drawn->pointCount < 2)) continue;
        for (int s = 0; s < drawn->pointCount - 1; s++)
        {
            if (!drawn->boostSeg[s]) continue;
            used += SegLen(drawn->localPoints[s], drawn->localPoints[s + 1]);
        }
    }
    return used;
}

//----------------------------------------------------------------------------------
// Checkpoint
//----------------------------------------------------------------------------------
bool PhysicsSetCheckpointNear(PhysicsWorld *phys, Vector2 p)
{
    float bestDistSq = CHECKPOINT_SNAP_RADIUS * CHECKPOINT_SNAP_RADIUS;
    int bestIndex = -1;
    for (int i = 0; i < phys->ghostCount; i++)
    {
        float dx = p.x - phys->ghost[i].pos.x;
        float dy = p.y - phys->ghost[i].pos.y;
        float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestIndex = i;
        }
    }

    if (bestIndex < 0)
    {
        // Click away from the trail clears the flag
        phys->checkpointSet = false;
        return false;
    }

    phys->checkpoint = phys->ghost[bestIndex]; // copied — survives newer ghost runs
    phys->checkpointSet = true;
    return true;
}
