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
    phys->accumulator = 0.0f;
    phys->simulating = false;

    b2WorldDef worldDef = b2DefaultWorldDef();
    // Gravity is always on — drawn strokes are static (Line Rider style) and the
    // ball is disabled until Start, so nothing can move during the build phase.
    // Screen Y grows downward — gravity points "down" the screen.
    worldDef.gravity = (b2Vec2){ 0.0f, WORLD_GRAVITY_Y };
    worldDef.maximumLinearSpeed = WORLD_MAX_SPEED;
    phys->worldId = b2CreateWorld(&worldDef);
    phys->valid = true;

    CreateStaticBoxes(phys, level);
    CreateBall(phys, level);

    phys->starPos = level->starPos;
    phys->starRadius = level->starRadius;
}

void PhysicsStartSimulation(PhysicsWorld *phys)
{
    if (!phys->valid || phys->simulating) return;

    phys->simulating = true;

    if (b2Body_IsValid(phys->ballId))
    {
        // Ball was created disabled — Start is the only mutation the world ever sees,
        // so the post-Start run is a pure function of (level, strokes, tunables).
        b2Body_Enable(phys->ballId);
        // Initial downward kick on top of gravity (admin tunable)
        if (phys->tunables.dropForce > 0.0f)
        {
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

void PhysicsStep(PhysicsWorld *phys, float dt)
{
    if (!phys->valid || !phys->simulating) return;

    if (dt > 0.05f) dt = 0.05f;

    const float step = 1.0f / PHYSICS_HZ;
    phys->accumulator += dt;

    while (phys->accumulator >= step)
    {
        b2World_Step(phys->worldId, step, PHYSICS_SUBSTEPS);
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
    Vector2 ball = PhysicsGetBallPos(phys);
    float dx = ball.x - phys->starPos.x;
    float dy = ball.y - phys->starPos.y;
    float reach = phys->ballRadius + phys->starRadius;
    return (dx * dx + dy * dy) <= (reach * reach);
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

    b2DestroyBody(query.hitBody);
    phys->drawn[slot].active = false;
    phys->drawn[slot].bodyId = b2_nullBodyId;
    phys->drawn[slot].pointCount = 0;
    return true;
}
