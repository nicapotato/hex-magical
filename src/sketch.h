/*******************************************************************************************
*
*   sketch.h - Mouse stroke capture, simplify, spawn/erase bodies
*
********************************************************************************************/

#ifndef SKETCH_H
#define SKETCH_H

#include "physics.h"
#include "raylib.h"

#include <stdbool.h>

typedef struct SketchState
{
    bool drawing;
    Vector2 points[MAX_STROKE_POINTS];
    int pointCount;
    Color crayonColor;
} SketchState;

void SketchInit(SketchState *sketch);
// inNoBuild: cursor currently inside a no-build zone — ink pauses there and the
// stroke splits into separate segments on either side of the zone.
void SketchUpdate(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool rmbPressed, bool inNoBuild);
void SketchCancel(SketchState *sketch);

#endif // SKETCH_H
