/*******************************************************************************************
*
*   levels.h - Hard-coded Crayon Physics levels (no level editor)
*
********************************************************************************************/

#ifndef LEVELS_H
#define LEVELS_H

#include "raylib.h"

#define LEVEL_COUNT 13
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

// Level 4: Launch — floating star high right, only reachable by ramp launch
static const StaticBox launchBoxes[] = {
    { 130.0f, 500.0f, 130.0f, 30.0f, 0.0f },   // launch plateau
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // floor (catch)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },  // left wall
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },   // right wall
};

// Level 5: Canyon — leap the chasm from rim to rim
static const StaticBox canyonBoxes[] = {
    { 130.0f, 430.0f, 130.0f, 30.0f, 0.0f },   // left rim
    { 590.0f, 430.0f, 130.0f, 30.0f, 0.0f },   // right rim
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // canyon floor (catch)
    { 260.0f, 560.0f, 20.0f, 110.0f, 0.0f },   // left canyon wall
    { 460.0f, 560.0f, 20.0f, 110.0f, 0.0f },   // right canyon wall
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 6: Tower — star sits atop a tall central spire
static const StaticBox towerBoxes[] = {
    { 360.0f, 460.0f, 30.0f, 240.0f, 0.0f },   // spire
    { 360.0f, 690.0f, 360.0f, 30.0f, 0.0f },   // ground
    { 110.0f, 520.0f, 100.0f, 24.0f, 0.0f },   // launch shelf
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 7: Skip — hop the pillars to a floating star far right
static const StaticBox skipBoxes[] = {
    { 160.0f, 560.0f, 40.0f, 140.0f, 0.0f },   // pillar 1
    { 380.0f, 590.0f, 40.0f, 110.0f, 0.0f },   // pillar 2
    { 600.0f, 560.0f, 40.0f, 140.0f, 0.0f },   // pillar 3
    { 360.0f, 710.0f, 360.0f, 20.0f, 0.0f },   // floor (catch)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 8: Vault — clear the great wall
static const StaticBox vaultBoxes[] = {
    { 360.0f, 470.0f, 24.0f, 250.0f, 0.0f },   // central wall
    { 170.0f, 700.0f, 190.0f, 20.0f, 0.0f },   // left ground
    { 550.0f, 700.0f, 190.0f, 20.0f, 0.0f },   // right ground
    { 120.0f, 540.0f, 110.0f, 24.0f, 0.0f },   // launch shelf
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 9: Chimney — thread the ball up a narrow shaft
static const StaticBox chimneyBoxes[] = {
    { 300.0f, 260.0f, 20.0f, 180.0f, 0.0f },   // left shaft wall
    { 420.0f, 260.0f, 20.0f, 180.0f, 0.0f },   // right shaft wall
    { 360.0f, 690.0f, 360.0f, 30.0f, 0.0f },   // ground
    { 120.0f, 540.0f, 110.0f, 24.0f, 0.0f },   // launch shelf
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 10: Islands — bounce up the floating chain
static const StaticBox islandBoxes[] = {
    { 150.0f, 550.0f, 90.0f, 18.0f, 0.0f },    // low island
    { 360.0f, 430.0f, 80.0f, 18.0f, 0.0f },    // mid island
    { 580.0f, 300.0f, 80.0f, 18.0f, 0.0f },    // high island
    { 360.0f, 710.0f, 360.0f, 20.0f, 0.0f },   // floor (catch)
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 11: Overhang — arc a shot under the roof
static const StaticBox overhangBoxes[] = {
    { 560.0f, 290.0f, 130.0f, 16.0f, 0.0f },   // roof
    { 690.0f, 470.0f, 20.0f, 200.0f, 0.0f },   // wall right of the star
    { 140.0f, 480.0f, 120.0f, 25.0f, 0.0f },   // launch ledge
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // floor
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 12: Basket — lob the ball into the cup
static const StaticBox basketBoxes[] = {
    { 560.0f, 560.0f, 70.0f, 14.0f, 0.0f },    // basket bottom
    { 498.0f, 505.0f, 12.0f, 60.0f, 0.0f },    // basket left lip
    { 622.0f, 505.0f, 12.0f, 60.0f, 0.0f },    // basket right lip
    { 130.0f, 480.0f, 110.0f, 25.0f, 0.0f },   // launch platform
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // floor
    {  -20.0f, 360.0f, 20.0f, 360.0f, 0.0f },
    { 740.0f, 360.0f, 20.0f, 360.0f, 0.0f },
};

// Level 13: Summit — angled slopes form a peak, star hovers above it
static const StaticBox summitBoxes[] = {
    { 240.0f, 560.0f, 170.0f, 20.0f, 30.0f },  // left slope (rises to the right)
    { 480.0f, 560.0f, 170.0f, 20.0f, -30.0f }, // right slope (rises to the left)
    { 360.0f, 700.0f, 360.0f, 40.0f, 0.0f },   // ground
    { 100.0f, 380.0f, 90.0f, 22.0f, 0.0f },    // high start ledge
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
    {
        .name = "Launch",
        // Drop onto the plateau, then draw a ramp to fling the ball at the floating star
        .ballSpawn = { 130.0f, 160.0f },
        .ballRadius = 18.0f,
        .starPos = { 620.0f, 180.0f },
        .starRadius = 22.0f,
        .boxes = launchBoxes,
        .boxCount = (int)(sizeof(launchBoxes) / sizeof(launchBoxes[0])),
    },
    {
        .name = "Canyon",
        // Build a ramp on the left rim and jump the chasm
        .ballSpawn = { 100.0f, 160.0f },
        .ballRadius = 18.0f,
        .starPos = { 620.0f, 370.0f },
        .starRadius = 22.0f,
        .boxes = canyonBoxes,
        .boxCount = (int)(sizeof(canyonBoxes) / sizeof(canyonBoxes[0])),
    },
    {
        .name = "Tower",
        // Launch off the shelf to crest the spire
        .ballSpawn = { 110.0f, 180.0f },
        .ballRadius = 18.0f,
        .starPos = { 360.0f, 180.0f },
        .starRadius = 22.0f,
        .boxes = towerBoxes,
        .boxCount = (int)(sizeof(towerBoxes) / sizeof(towerBoxes[0])),
    },
    {
        .name = "Skip",
        // Ramp off pillar tops — the star floats past the last pillar
        .ballSpawn = { 160.0f, 140.0f },
        .ballRadius = 18.0f,
        .starPos = { 690.0f, 330.0f },
        .starRadius = 22.0f,
        .boxes = skipBoxes,
        .boxCount = (int)(sizeof(skipBoxes) / sizeof(skipBoxes[0])),
    },
    {
        .name = "Vault",
        // Big air over the central wall
        .ballSpawn = { 120.0f, 160.0f },
        .ballRadius = 18.0f,
        .starPos = { 560.0f, 620.0f },
        .starRadius = 22.0f,
        .boxes = vaultBoxes,
        .boxCount = (int)(sizeof(vaultBoxes) / sizeof(vaultBoxes[0])),
    },
    {
        .name = "Chimney",
        // Fling the ball up through the narrow shaft
        .ballSpawn = { 120.0f, 180.0f },
        .ballRadius = 18.0f,
        .starPos = { 360.0f, 120.0f },
        .starRadius = 22.0f,
        .boxes = chimneyBoxes,
        .boxCount = (int)(sizeof(chimneyBoxes) / sizeof(chimneyBoxes[0])),
    },
    {
        .name = "Islands",
        // Chain ramps island to island, climbing right and up
        .ballSpawn = { 150.0f, 160.0f },
        .ballRadius = 18.0f,
        .starPos = { 580.0f, 230.0f },
        .starRadius = 22.0f,
        .boxes = islandBoxes,
        .boxCount = (int)(sizeof(islandBoxes) / sizeof(islandBoxes[0])),
    },
    {
        .name = "Overhang",
        // The star hides under the roof — flatten your launch arc
        .ballSpawn = { 140.0f, 140.0f },
        .ballRadius = 18.0f,
        .starPos = { 600.0f, 380.0f },
        .starRadius = 22.0f,
        .boxes = overhangBoxes,
        .boxCount = (int)(sizeof(overhangBoxes) / sizeof(overhangBoxes[0])),
    },
    {
        .name = "Basket",
        // Lob the ball across the screen into the cup
        .ballSpawn = { 130.0f, 140.0f },
        .ballRadius = 18.0f,
        .starPos = { 560.0f, 520.0f },
        .starRadius = 22.0f,
        .boxes = basketBoxes,
        .boxCount = (int)(sizeof(basketBoxes) / sizeof(basketBoxes[0])),
    },
    {
        .name = "Summit",
        // Ride the slope for speed, then ramp up to the hovering star
        .ballSpawn = { 100.0f, 120.0f },
        .ballRadius = 18.0f,
        .starPos = { 360.0f, 300.0f },
        .starRadius = 22.0f,
        .boxes = summitBoxes,
        .boxCount = (int)(sizeof(summitBoxes) / sizeof(summitBoxes[0])),
    },
};

#endif // LEVELS_H
