/*******************************************************************************************
*
*   levels.h - Hard-coded Crayon Physics levels (no level editor)
*
********************************************************************************************/

#ifndef LEVELS_H
#define LEVELS_H

#include "raylib.h"

#define LEVEL_COUNT 3
#define MAX_STATIC_BOXES 16

typedef struct StaticBox
{
    float x;          // center x
    float y;          // center y
    float halfWidth;
    float halfHeight;
    float angleDeg;   // degrees, clockwise (screen Y-down)
} StaticBox;

typedef struct LevelDef
{
    const char *name;
    Vector2 ballSpawn;
    float ballRadius;
    Vector2 starPos;
    float starRadius;
    const StaticBox *boxes;
    int boxCount;
} LevelDef;

// Level 1: Gap — two plateaus, draw a bridge
static const StaticBox gapBoxes[] = {
    { 120.0f, 520.0f, 120.0f, 40.0f, 0.0f },   // left plateau
    { 600.0f, 520.0f, 120.0f, 40.0f, 0.0f },   // right plateau
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // floor (catch)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },  // left wall
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },   // right wall
};

// Level 2: Climb — star on a high ledge
static const StaticBox climbBoxes[] = {
    { 360.0f, 660.0f, 340.0f, 40.0f, 0.0f },   // ground
    { 560.0f, 420.0f, 100.0f, 20.0f, 0.0f },   // mid ledge
    { 620.0f, 220.0f, 80.0f, 20.0f, 0.0f },    // high ledge (star)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 3: Pit — ball in a well, star on a shelf above
static const StaticBox pitBoxes[] = {
    { 360.0f, 680.0f, 340.0f, 30.0f, 0.0f },   // floor
    { 200.0f, 520.0f, 20.0f, 140.0f, 0.0f },   // left pit wall
    { 360.0f, 520.0f, 20.0f, 140.0f, 0.0f },   // right pit wall
    { 280.0f, 640.0f, 100.0f, 20.0f, 0.0f },   // pit floor
    { 520.0f, 300.0f, 120.0f, 20.0f, 0.0f },   // star shelf
    { 100.0f, 400.0f, 80.0f, 16.0f, 0.0f },    // fulcrum platform (for lever)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

static const LevelDef LEVELS[LEVEL_COUNT] = {
    {
        .name = "Gap",
        // High above left plateau — build a bridge/ramp under the drop
        .ballSpawn = { 120.0f, 140.0f },
        .ballRadius = 18.0f,
        .starPos = { 600.0f, 460.0f },
        .starRadius = 22.0f,
        .boxes = gapBoxes,
        .boxCount = (int)(sizeof(gapBoxes) / sizeof(gapBoxes[0])),
    },
    {
        .name = "Climb",
        // High left — build under the fall to climb toward the star
        .ballSpawn = { 100.0f, 120.0f },
        .ballRadius = 18.0f,
        .starPos = { 620.0f, 170.0f },
        .starRadius = 22.0f,
        .boxes = climbBoxes,
        .boxCount = (int)(sizeof(climbBoxes) / sizeof(climbBoxes[0])),
    },
    {
        .name = "Pit",
        // High above the left fulcrum — build under, then drop
        .ballSpawn = { 100.0f, 120.0f },
        .ballRadius = 18.0f,
        .starPos = { 520.0f, 250.0f },
        .starRadius = 22.0f,
        .boxes = pitBoxes,
        .boxCount = (int)(sizeof(pitBoxes) / sizeof(pitBoxes[0])),
    },
};

#endif // LEVELS_H
