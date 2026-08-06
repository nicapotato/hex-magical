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
#include <stdint.h>

#define MAX_DRAWN_BODIES 64
#define MAX_STROKE_POINTS 256
#define PHYSICS_HZ 60.0f
#define PHYSICS_SUBSTEPS 4
// Half-width of drawn crayon ink — capsules use this so colliders match the stroke
#define STROKE_PHYSICS_RADIUS 5.0f
// Static track is cheap — allow one capsule per smoothed stroke segment
#define MAX_STROKE_CAPSULES (MAX_STROKE_POINTS - 1)
#define MAX_STROKE_SEGS MAX_STROKE_CAPSULES

#define MAX_CANNONS 8

// Boosted segments amplify the ball's current velocity while nearby (no steering)
#define BOOST_LINE_RADIUS 30.0f
// Brush radius when painting / unpainting boost on existing crayon strokes
#define BOOST_PAINT_RADIUS 24.0f
// Eraser circle — carves stroke geometry under the cursor
#define ERASE_RADIUS (STROKE_PHYSICS_RADIUS + 8.0f)
// Surviving erase crumbs shorter than this are discarded
#define ERASE_MIN_PIECE_LEN 10.0f
// Subdivide long straight spans so paint/erase have useful segment granularity
#define STROKE_MAX_SEG_LEN 15.0f

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
// Boost: acceleration along current velocity builds while on a boosted segment
#define TUNE_BOOST_VEL_RATE_DEFAULT   10000.0f
#define TUNE_BOOST_VEL_RATE_MIN       0.0f
#define TUNE_BOOST_VEL_RATE_MAX       40000.0f
#define TUNE_BOOST_VEL_MAX_DEFAULT    6000.0f
#define TUNE_BOOST_VEL_MAX_MIN        0.0f
#define TUNE_BOOST_VEL_MAX_MAX        20000.0f

typedef struct PhysicsTunables
{
    float ballDensity;     // ball "weight" — mass via shape density
    float ballRestitution; // bounciness 0..~1
    float dropForce;       // initial downward velocity kick applied at drop
    float boostVelRate;    // accel/sec gained while on a boost segment
    float boostVelMax;     // cap on boost acceleration along current velocity
} PhysicsTunables;

// Build-phase undo: every draw/erase/place/paint action is recorded so Alt+Z can
// revert it. Erased geometry is stored inline so undo can recreate it.
#define UNDO_MAX_ACTIONS 32
#define UNDO_MAX_PIECES 16

typedef enum UndoKind
{
    UNDO_NONE = 0,
    UNDO_DRAW_STROKE,  // undo = erase the drawn slot
    UNDO_ADD_CANNON,   // undo = clear the cannon slot
    UNDO_SPLIT_STROKE, // undo = destroy pieces, recreate original stroke
    UNDO_PAINT_BOOST,  // undo = restore previous boost mask on a stroke
    UNDO_ERASE_CANNON  // undo = re-place the cannon from stored pos/angle
} UndoKind;

typedef struct UndoAction
{
    UndoKind kind;
    int slot;                          // draw/paint/add: which slot
    Vector2 points[MAX_STROKE_POINTS]; // split: world-space geometry to recreate
    int pointCount;
    Color color;                       // split: stroke color
    uint8_t boostSeg[MAX_STROKE_SEGS]; // split / paint: boost mask
    int pieceSlots[UNDO_MAX_PIECES];   // split: slots created from the carve
    int pieceCount;
    Vector2 pos;    // erased cannon position
    float angleRad; // erased cannon barrel angle
} UndoAction;

typedef struct DrawnBody
{
    bool active;
    b2BodyId bodyId;
    Vector2 localPoints[MAX_STROKE_POINTS]; // stroke points in body-local space
    int pointCount;
    Color crayonColor;
    uint8_t boostSeg[MAX_STROKE_SEGS]; // 1 = boosted, one entry per segment
} DrawnBody;

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
    const GravityZone *antiGravity; // ball inside rotates world gravity
    int antiGravityCount;

    DrawnBody drawn[MAX_DRAWN_BODIES];
    int drawnCount;

    Cannon cannons[MAX_CANNONS];

    // Build-phase undo stack (oldest dropped when full). Creation/erase calls
    // record themselves unless undoApplying is set (reverts must not re-record).
    UndoAction undo[UNDO_MAX_ACTIONS];
    int undoCount;
    bool undoApplying;

    // Per-level build budgets (from LevelDef; canvas px of ink / cannon slots).
    // Zero = resource disabled for this level.
    // boostLineCapacity budgets painted boost length on crayon strokes.
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

    // Built-up boost acceleration along current velocity (0 when off a boost)
    float boostLineAccel;

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

// Create a static capsule-chain track from world-space stroke points.
// boostSeg may be NULL (no boost) or length count-1 for the input polyline;
// it is remapped onto the resampled segments. Returns drawn index or -1.
int PhysicsCreateDrawnBody(PhysicsWorld *phys, const Vector2 *worldPoints, int count,
                           Color color, const uint8_t *boostSeg);

// Carve stroke geometry under worldPoint (erase circle). Splits into pieces;
// returns true if a stroke was hit. Full wipe is the zero-pieces case.
bool PhysicsEraseAtPoint(PhysicsWorld *phys, Vector2 worldPoint);

// Paint (apply=true) or clear (apply=false) boost on segments near worldPoint.
// Consumes/refunds boostLineCapacity by segment length. Returns the stroke slot
// whose mask changed, or -1. If the mask changed and prevMask is non-NULL, the
// previous mask is copied there (caller records undo once per gesture/slot).
int PhysicsPaintBoostAt(PhysicsWorld *phys, Vector2 worldPoint, bool apply, uint8_t *prevMask);

// Record a paint-boost undo entry (call once per stroke per paint gesture).
void PhysicsRecordPaintBoostUndo(PhysicsWorld *phys, int slot, const uint8_t *prevMask);

// Cannons: place at pos aiming along angleRad / erase near a point
int PhysicsAddCannon(PhysicsWorld *phys, Vector2 pos, float angleRad);
bool PhysicsEraseCannonAt(PhysicsWorld *phys, Vector2 worldPoint);
int PhysicsActiveCannonCount(const PhysicsWorld *phys);

// Ink spent so far (canvas px) — erase/unpaint refunds by removing geometry/flags
float PhysicsDrawnInkUsed(const PhysicsWorld *phys);
float PhysicsBoostInkUsed(const PhysicsWorld *phys);

// Checkpoint: snap to the ghost sample nearest p (within CHECKPOINT_SNAP_RADIUS).
// A click away from the trail clears the flag. Returns true if a flag is now set.
bool PhysicsSetCheckpointNear(PhysicsWorld *phys, Vector2 p);

// Undo the most recent build action. Build phase only. Returns false when empty.
bool PhysicsUndoLastAction(PhysicsWorld *phys);

DrawnBody *PhysicsGetDrawn(PhysicsWorld *phys, int index);
b2Transform PhysicsGetBodyTransform(b2BodyId bodyId);

#endif // PHYSICS_H
