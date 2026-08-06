/*******************************************************************************************
*
*   sketch.h - Build tools: stroke capture (crayon), boost paint on existing lines,
*   cannon placement, checkpoint flag. Strokes are simplified and spawned as physics bodies.
*
********************************************************************************************/

#ifndef SKETCH_H
#define SKETCH_H

#include "physics.h"
#include "raylib.h"

#include <stdbool.h>

// Build-phase tools. Availability comes from the level's TMX parameters —
// a zero-capacity resource never appears in the HUD.
typedef enum BuildTool
{
    TOOL_CRAYON = 0,  // solid crayon track (line-capacity budget)
    TOOL_BOOST_LINE,  // paint boost onto existing crayon segments (boost_line-capacity)
    TOOL_CANNON,      // press-drag-aim cannon placement (cannon-count budget)
    TOOL_FLAG,        // checkpoint flag on the ghost trail
    TOOL_ERASER,      // erase mode: LMB (click or drag) carves builds under the cursor
    TOOL_COUNT
} BuildTool;

typedef struct SketchState
{
    BuildTool tool;
    bool drawing;
    Vector2 points[MAX_STROKE_POINTS];
    int pointCount;
    Color crayonColor;

    float inkUsedThisStroke; // canvas px consumed by the in-progress stroke

    // Cannon placement drag: press = base position, drag direction = barrel aim
    bool aimingCannon;
    Vector2 cannonAnchor;
    float cannonAngle;

    // Boost paint gesture: LMB paints, RMB unpaints; one undo entry per stroke touched
    bool paintingBoost;
    bool paintBoostApply; // true = paint, false = clear
    bool paintUndoRecorded[MAX_DRAWN_BODIES];
} SketchState;

void SketchInit(SketchState *sketch);
// inNoBuild: cursor currently inside a no-build zone — ink pauses there and the
// stroke splits into separate segments on either side of the zone.
void SketchUpdate(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse,
                  bool lmbDown, bool lmbPressed, bool rmbDown, bool rmbPressed, bool inNoBuild);
void SketchCancel(SketchState *sketch);

#endif // SKETCH_H
