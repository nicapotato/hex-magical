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
// Below this speed the ball has no meaningful direction to boost along
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
    }
    phys->drawnCount = 0;
}

// Boost lines, cannons, ghost trail, checkpoint and undo all reset with the level
static void ClearBuildExtras(PhysicsWorld *phys)
{
    memset(phys->boostLines, 0, sizeof(phys->boostLines));
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
        *tangent = (Vector2){ 0.0f, 0.0f };
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
    *tangent = (Vector2){ dx / len, dy / len };
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

static int AllocDrawnSlot(PhysicsWorld *phys)
{
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        if (!phys->drawn[i].active) return i;
    }
    return -1;
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
}

void PhysicsStartSimulation(PhysicsWorld *phys)
{
    if (!phys->valid || phys->simulating) return;

    phys->simulating = true;
    phys->trailCount = 0;
    phys->trailStepCounter = 0;
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

// While the ball is near a boost line, push it along the stroke's drawn
// direction — unlike boost zones this steers, not just accelerates
static void ApplyBoostLines(PhysicsWorld *phys)
{
    if (!b2Body_IsValid(phys->ballId)) return;

    Vector2 ball = FromB2(b2Body_GetPosition(phys->ballId));

    // Nearest segment across all lines wins so overlapping lines never stack
    float bestDist = BOOST_LINE_RADIUS;
    Vector2 bestTangent = { 0.0f, 0.0f };
    for (int l = 0; l < MAX_BOOST_LINES; l++)
    {
        const BoostLine *line = &phys->boostLines[l];
        if (!line->active) continue;

        for (int i = 0; i < line->pointCount - 1; i++)
        {
            Vector2 tangent = { 0 };
            float d = PointSegmentDist(ball, line->points[i], line->points[i + 1], &tangent);
            if (d < bestDist)
            {
                bestDist = d;
                bestTangent = tangent;
            }
        }
    }

    if ((bestTangent.x == 0.0f) && (bestTangent.y == 0.0f)) return;

    float mass = b2Body_GetMass(phys->ballId);
    b2Vec2 force = { bestTangent.x * BOOST_LINE_ACCEL * mass, bestTangent.y * BOOST_LINE_ACCEL * mass };
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
        ApplyBoostZones(phys);
        ApplyBoostLines(phys);
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

int PhysicsCreateDrawnBody(PhysicsWorld *phys, const Vector2 *worldPoints, int count, Color color)
{
    if (!phys->valid || (count < 2)) return -1;

    int slot = AllocDrawnSlot(phys);
    if (slot < 0) return -1;

    // Resample along the stroke if needed so capsule count stays bounded
    Vector2 path[MAX_STROKE_POINTS];
    int pathCount = count;
    if (pathCount > MAX_STROKE_POINTS) pathCount = MAX_STROKE_POINTS;

    if (count <= (MAX_STROKE_CAPSULES + 1))
    {
        for (int i = 0; i < pathCount; i++) path[i] = worldPoints[i];
    }
    else
    {
        pathCount = MAX_STROKE_CAPSULES + 1;
        for (int i = 0; i < pathCount; i++)
        {
            int src = (i * (count - 1)) / (pathCount - 1);
            path[i] = worldPoints[src];
        }
    }

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
        float dx = path[i + 1].x - path[i].x;
        float dy = path[i + 1].y - path[i].y;
        float segLen = sqrtf(dx * dx + dy * dy);
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

typedef struct EraseQuery
{
    PhysicsWorld *phys;
    b2BodyId hitBody;
} EraseQuery;

static bool EraseOverlapCallback(b2ShapeId shapeId, void *context)
{
    EraseQuery *query = (EraseQuery *)context;
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    intptr_t tag = (intptr_t)b2Body_GetUserData(bodyId);
    if (tag == BALL_USER_TAG) return true; // continue — never erase the ball

    if ((tag >= 1) && (tag <= MAX_DRAWN_BODIES))
    {
        int slot = (int)tag - 1;
        if (query->phys->drawn[slot].active &&
            B2_ID_EQUALS(query->phys->drawn[slot].bodyId, bodyId))
        {
            query->hitBody = bodyId;
            return false; // stop
        }
    }

    return true;
}

bool PhysicsEraseAtPoint(PhysicsWorld *phys, Vector2 worldPoint)
{
    if (!phys->valid) return false;

    const float pad = 8.0f;
    b2AABB aabb;
    aabb.lowerBound = (b2Vec2){ worldPoint.x - pad, worldPoint.y - pad };
    aabb.upperBound = (b2Vec2){ worldPoint.x + pad, worldPoint.y + pad };

    EraseQuery query = { 0 };
    query.phys = phys;
    query.hitBody = b2_nullBodyId;

    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_OverlapAABB(phys->worldId, aabb, filter, EraseOverlapCallback, &query);

    if (!b2Body_IsValid(query.hitBody)) return false;

    void *userData = b2Body_GetUserData(query.hitBody);
    int slot = (int)(intptr_t)userData - 1;
    if ((slot < 0) || (slot >= MAX_DRAWN_BODIES)) return false;
    if (!phys->drawn[slot].active) return false;

    // Record the erased geometry (world space) so undo can redraw it
    UndoAction *undo = PushUndo(phys, UNDO_ERASE_STROKE);
    if (undo)
    {
        const DrawnBody *drawn = &phys->drawn[slot];
        b2Transform xf = b2Body_GetTransform(drawn->bodyId);
        undo->pointCount = drawn->pointCount;
        undo->color = drawn->crayonColor;
        for (int p = 0; p < drawn->pointCount; p++)
        {
            b2Vec2 world = b2TransformPoint(xf, ToB2(drawn->localPoints[p]));
            undo->points[p] = FromB2(world);
        }
    }

    b2DestroyBody(query.hitBody);
    phys->drawn[slot].active = false;
    phys->drawn[slot].bodyId = b2_nullBodyId;
    phys->drawn[slot].pointCount = 0;
    return true;
}

//----------------------------------------------------------------------------------
// Boost lines and cannons (build resources — no Box2D bodies)
//----------------------------------------------------------------------------------
int PhysicsCreateBoostLine(PhysicsWorld *phys, const Vector2 *worldPoints, int count)
{
    if (!phys->valid || (count < 2)) return -1;

    for (int i = 0; i < MAX_BOOST_LINES; i++)
    {
        BoostLine *line = &phys->boostLines[i];
        if (line->active) continue;

        line->active = true;
        line->pointCount = (count < MAX_STROKE_POINTS) ? count : MAX_STROKE_POINTS;
        for (int p = 0; p < line->pointCount; p++) line->points[p] = worldPoints[p];

        UndoAction *undo = PushUndo(phys, UNDO_DRAW_BOOST);
        if (undo) undo->slot = i;

        return i;
    }

    TraceLog(LOG_WARNING, "PHYSICS: more than %d boost lines", MAX_BOOST_LINES);
    return -1;
}

bool PhysicsEraseBoostLineAt(PhysicsWorld *phys, Vector2 worldPoint)
{
    const float pad = STROKE_PHYSICS_RADIUS + 8.0f;
    for (int i = 0; i < MAX_BOOST_LINES; i++)
    {
        BoostLine *line = &phys->boostLines[i];
        if (!line->active) continue;

        for (int p = 0; p < line->pointCount - 1; p++)
        {
            Vector2 tangent = { 0 };
            if (PointSegmentDist(worldPoint, line->points[p], line->points[p + 1], &tangent) <= pad)
            {
                UndoAction *undo = PushUndo(phys, UNDO_ERASE_BOOST);
                if (undo)
                {
                    undo->pointCount = line->pointCount;
                    for (int q = 0; q < line->pointCount; q++) undo->points[q] = line->points[q];
                }

                line->active = false;
                line->pointCount = 0;
                return true;
            }
        }
    }
    return false;
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
            DrawnBody *drawn = &phys->drawn[action->slot];
            if (drawn->active)
            {
                b2DestroyBody(drawn->bodyId);
                drawn->active = false;
                drawn->bodyId = b2_nullBodyId;
                drawn->pointCount = 0;
            }
        } break;
        case UNDO_DRAW_BOOST:
        {
            phys->boostLines[action->slot].active = false;
            phys->boostLines[action->slot].pointCount = 0;
        } break;
        case UNDO_ADD_CANNON:
        {
            phys->cannons[action->slot].active = false;
        } break;
        case UNDO_ERASE_STROKE:
        {
            PhysicsCreateDrawnBody(phys, action->points, action->pointCount, action->color);
        } break;
        case UNDO_ERASE_BOOST:
        {
            PhysicsCreateBoostLine(phys, action->points, action->pointCount);
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
        used += PolylineLength(drawn->localPoints, drawn->pointCount);
    }
    return used;
}

float PhysicsBoostInkUsed(const PhysicsWorld *phys)
{
    float used = 0.0f;
    for (int i = 0; i < MAX_BOOST_LINES; i++)
    {
        const BoostLine *line = &phys->boostLines[i];
        if (!line->active) continue;
        used += PolylineLength(line->points, line->pointCount);
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
