/*******************************************************************************************
*
*   physics.h - Box2D world wrapper for hex-magical
*
********************************************************************************************/

#ifndef PHYSICS_H
#define PHYSICS_H

#include "box2d/box2d.h"
#include "levels.h"
#include "raylib.h"

#include <stdbool.h>

#define MAX_DRAWN_BODIES 64
#define MAX_STROKE_POINTS 256
#define PHYSICS_HZ 60.0f
#define PHYSICS_SUBSTEPS 4
// Half-width of drawn crayon ink — capsules use this so colliders match the stroke
#define STROKE_PHYSICS_RADIUS 5.0f
// Static track is cheap — allow one capsule per smoothed stroke segment
#define MAX_STROKE_CAPSULES (MAX_STROKE_POINTS - 1)

// Admin-tunable ball physics (defaults restored via PhysicsTunablesDefaults)
#define TUNE_BALL_DENSITY_DEFAULT     12.0f
#define TUNE_BALL_DENSITY_MIN         0.25f
#define TUNE_BALL_DENSITY_MAX         40.0f
#define TUNE_BALL_RESTITUTION_DEFAULT 0.25f
#define TUNE_BALL_RESTITUTION_MIN     0.0f
#define TUNE_BALL_RESTITUTION_MAX     0.95f
#define TUNE_DROP_FORCE_DEFAULT       0.0f
#define TUNE_DROP_FORCE_MIN           0.0f
#define TUNE_DROP_FORCE_MAX           2500.0f

typedef struct PhysicsTunables
{
    float ballDensity;     // ball "weight" — mass via shape density
    float ballRestitution; // bounciness 0..~1
    float dropForce;       // initial downward velocity kick applied at drop
} PhysicsTunables;

typedef struct DrawnBody
{
    bool active;
    b2BodyId bodyId;
    Vector2 localPoints[MAX_STROKE_POINTS]; // stroke points in body-local space
    int pointCount;
    Color crayonColor;
} DrawnBody;

typedef struct PhysicsWorld
{
    b2WorldId worldId;
    bool valid;

    b2BodyId ballId;
    float ballRadius;
    Vector2 ballSpawn; // level spawn — Stop resets the ball here

    PolyZone finishLine;

    const PolyZone *pits;   // ball inside = game over
    int pitCount;
    const PolyZone *boosts; // ball inside gets a speed boost each step
    int boostCount;

    DrawnBody drawn[MAX_DRAWN_BODIES];
    int drawnCount;

    const StaticBox *staticBoxes;
    int staticBoxCount;

    PhysicsTunables tunables; // persists across level loads

    float accumulator;
    bool simulating; // false = build phase (ball disabled; strokes are static track)
} PhysicsWorld;

void PhysicsTunablesDefaults(PhysicsTunables *t);
// Live-apply density/restitution to the current ball body (mass recomputed)
void PhysicsApplyBallTunables(PhysicsWorld *phys);

void PhysicsInit(PhysicsWorld *phys);
void PhysicsShutdown(PhysicsWorld *phys);
void PhysicsLoadLevel(PhysicsWorld *phys, const LevelDef *level);
void PhysicsStartSimulation(PhysicsWorld *phys); // enable + drop the ball
void PhysicsStopSimulation(PhysicsWorld *phys);  // ball back to spawn, world back to build phase
void PhysicsStep(PhysicsWorld *phys, float dt);
bool PhysicsIsSimulating(const PhysicsWorld *phys);

Vector2 PhysicsGetBallPos(const PhysicsWorld *phys);
float PhysicsGetBallAngle(const PhysicsWorld *phys);
bool PhysicsCheckWin(const PhysicsWorld *phys);
bool PhysicsCheckPit(const PhysicsWorld *phys); // ball fell into a pit = game over

// Create a static capsule-chain track from world-space stroke points. Returns drawn index or -1.
int PhysicsCreateDrawnBody(PhysicsWorld *phys, const Vector2 *worldPoints, int count, Color color);

// Destroy drawn body under world-space point. Returns true if something was erased.
bool PhysicsEraseAtPoint(PhysicsWorld *phys, Vector2 worldPoint);

DrawnBody *PhysicsGetDrawn(PhysicsWorld *phys, int index);
b2Transform PhysicsGetBodyTransform(b2BodyId bodyId);

#endif // PHYSICS_H
