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
void SketchUpdate(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool rmbPressed);
void SketchCancel(SketchState *sketch);

#endif // SKETCH_H
