/*******************************************************************************************
*
*   levels.h - Level data types. Levels are authored in Tiled (.tmx in resources/)
*   and loaded at runtime by tiled.c — there are no hard-coded levels.
*
********************************************************************************************/

#ifndef LEVELS_H
#define LEVELS_H

#include "raylib.h"

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

#endif // LEVELS_H
