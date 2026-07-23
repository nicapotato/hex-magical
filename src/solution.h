/*******************************************************************************************
*
*   solution.h - Saved solutions: snapshot of drawn strokes + tunables for a level
*
*   Because the world is fully fixed at Start (strokes are static, ball disabled),
*   a solution is a snapshot, not an action log. Replaying it — create strokes,
*   Start, step at fixed 60 Hz — reproduces the run deterministically.
*
*   Text format (resources/solutions/<level>.solution), diffable and hand-editable:
*
*       version 2
*       level map-2.tmx
*       tunables density=2.5 restitution=0.25 dropforce=0.0
*       stroke 142.0,310.5 198.2,300.1 240.7,315.9
*       stroke 300.0,400.0 350.0,390.0
*       boost 120.0,500.0 180.0,470.0
*       cannon 420.0,610.0 -1.5708
*
********************************************************************************************/

#ifndef SOLUTION_H
#define SOLUTION_H

#include "physics.h"
#include "raylib.h"

#include <stdbool.h>

#define SOLUTION_VERSION 2
#define SOLUTION_MAX_STROKES MAX_DRAWN_BODIES
#define SOLUTION_MAX_BOOSTS MAX_BOOST_LINES
#define SOLUTION_MAX_CANNONS MAX_CANNONS

typedef struct SolutionStroke
{
    Vector2 points[MAX_STROKE_POINTS]; // world space, post-smoothing
    int pointCount;
} SolutionStroke;

typedef struct SolutionCannon
{
    Vector2 pos;
    float angleRad;
} SolutionCannon;

typedef struct Solution
{
    char levelFile[256];      // .tmx basename — the stable level identity
    PhysicsTunables tunables; // physics knobs active when the solution was made
    SolutionStroke strokes[SOLUTION_MAX_STROKES];
    int strokeCount;
    SolutionStroke boosts[SOLUTION_MAX_BOOSTS]; // boost line placements
    int boostCount;
    SolutionCannon cannons[SOLUTION_MAX_CANNONS];
    int cannonCount;
} Solution;

// Snapshot the currently drawn strokes (world space) and active tunables
void SolutionCapture(Solution *sol, const PhysicsWorld *phys, const char *levelFile);

// Save/load the text format. Fail loud: any I/O or parse problem logs to stderr
// and returns false — malformed solution files must never half-load silently.
bool SolutionSave(const Solution *sol, const char *path);
bool SolutionLoad(Solution *sol, const char *path);

// Recreate strokes into a freshly loaded level (build phase). Tunables are NOT
// applied here — assign phys->tunables before PhysicsLoadLevel so the ball is
// built with the recorded values.
void SolutionApply(const Solution *sol, PhysicsWorld *phys);

#endif // SOLUTION_H
