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

#define MAX_BOOST_LINES 16
#define MAX_CANNONS 8

// Boost lines push the ball along the drawn direction while it is nearby
#define BOOST_LINE_RADIUS 30.0f
#define BOOST_LINE_ACCEL 4200.0f

// Cannons: ball entering the muzzle circle is relaunched along the barrel
#define CANNON_ENTRY_RADIUS 26.0f
#define CANNON_BLAST_SPEED 1400.0f
#define CANNON_COOLDOWN 0.6f
#define CANNON_BARREL_LENGTH 46.0f // shared by render + aim preview

// Ghost trail: one sample every TRAIL_SAMPLE_STRIDE fixed steps (30 Hz),
// TRAIL_MAX_SAMPLES caps the recording at ~2 minutes of run
#define TRAIL_SAMPLE_STRIDE 2
#define TRAIL_MAX_SAMPLES 4096
// Clicks this close to a ghost sample snap a checkpoint onto it
#define CHECKPOINT_SNAP_RADIUS 40.0f

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

// Player-drawn boost line: no collider — while the ball is near the polyline it
// is accelerated along the stroke's drawn direction (tangent of nearest segment)
typedef struct BoostLine
{
    bool active;
    Vector2 points[MAX_STROKE_POINTS]; // world space, post-smoothing
    int pointCount;
} BoostLine;

// Player-placed cannon: entry sensor circle + barrel direction. Ball entering
// the circle is relaunched along the barrel at CANNON_BLAST_SPEED.
typedef struct Cannon
{
    bool active;
    Vector2 pos;
    float angleRad;  // barrel direction (screen Y-down)
    float cooldown;  // seconds until it can fire again
} Cannon;

// One recorded moment of a run — enough state to resume the ball mid-flight
typedef struct TrailSample
{
    Vector2 pos;
    Vector2 vel;
    float angle;
    float angularVel;
} TrailSample;

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

    BoostLine boostLines[MAX_BOOST_LINES];
    Cannon cannons[MAX_CANNONS];

    // Per-level build budgets (from LevelDef; canvas px of ink / cannon slots).
    // Zero = resource disabled for this level.
    float lineCapacity;
    float boostLineCapacity;
    int cannonCapacity;

    // Ghost trail: `trail` records the current run; on Stop it becomes `ghost`,
    // the last completed run shown during the build phase
    TrailSample trail[TRAIL_MAX_SAMPLES];
    int trailCount;
    int trailStepCounter;
    TrailSample ghost[TRAIL_MAX_SAMPLES];
    int ghostCount;

    // Checkpoint: a copied ghost sample — Start resumes the ball from it
    bool checkpointSet;
    TrailSample checkpoint;

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

// Boost lines: create from world-space points / erase near a point
int PhysicsCreateBoostLine(PhysicsWorld *phys, const Vector2 *worldPoints, int count);
bool PhysicsEraseBoostLineAt(PhysicsWorld *phys, Vector2 worldPoint);

// Cannons: place at pos aiming along angleRad / erase near a point
int PhysicsAddCannon(PhysicsWorld *phys, Vector2 pos, float angleRad);
bool PhysicsEraseCannonAt(PhysicsWorld *phys, Vector2 worldPoint);
int PhysicsActiveCannonCount(const PhysicsWorld *phys);

// Ink spent so far (canvas px) — erase refunds simply by removing the geometry
float PhysicsDrawnInkUsed(const PhysicsWorld *phys);
float PhysicsBoostInkUsed(const PhysicsWorld *phys);

// Checkpoint: snap to the ghost sample nearest p (within CHECKPOINT_SNAP_RADIUS).
// A click away from the trail clears the flag. Returns true if a flag is now set.
bool PhysicsSetCheckpointNear(PhysicsWorld *phys, Vector2 p);

DrawnBody *PhysicsGetDrawn(PhysicsWorld *phys, int index);
b2Transform PhysicsGetBodyTransform(b2BodyId bodyId);

#endif // PHYSICS_H
