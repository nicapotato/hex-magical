/*******************************************************************************************
*
*   levels.h - Level data types. Levels are authored in Tiled (.tmx in resources/)
*   and loaded at runtime by tiled.c — there are no hard-coded levels.
*
********************************************************************************************/

#ifndef LEVELS_H
#define LEVELS_H

#include "raylib.h"

#include <stdbool.h>

typedef struct StaticBox
{
    float x;          // center x
    float y;          // center y
    float halfWidth;
    float halfHeight;
    float angleDeg;   // degrees, clockwise (screen Y-down)
} StaticBox;

#define STATIC_POLYGON_MAX_POINTS 8

typedef struct StaticPolygon
{
    Vector2 points[STATIC_POLYGON_MAX_POINTS]; // world coordinates, convex
    int pointCount;
} StaticPolygon;

// Gameplay area authored in Tiled as a polygon or rectangle object
// (no-build, pit, boost, finish-line). Canvas coordinates, closed polygon.
#define POLY_ZONE_MAX_POINTS 32

typedef struct PolyZone
{
    Vector2 points[POLY_ZONE_MAX_POINTS];
    int pointCount;
} PolyZone;

// Ray-cast point-in-polygon (handles concave outlines)
static inline bool PolyZoneContains(const PolyZone *zone, Vector2 p)
{
    bool inside = false;
    for (int i = 0, j = zone->pointCount - 1; i < zone->pointCount; j = i++)
    {
        const Vector2 *a = &zone->points[i];
        const Vector2 *b = &zone->points[j];
        if (((a->y > p.y) != (b->y > p.y)) &&
            (p.x < (b->x - a->x) * (p.y - a->y) / (b->y - a->y) + a->x))
        {
            inside = !inside;
        }
    }
    return inside;
}

typedef struct LevelDef
{
    const char *name;
    Vector2 ballSpawn;
    float ballRadius;
    PolyZone finishLine;
    const StaticBox *boxes;
    int boxCount;
    const StaticPolygon *polygons;
    int polygonCount;
    const PolyZone *pits;   // ball inside = game over
    int pitCount;
    const PolyZone *boosts; // ball inside = speed boost
    int boostCount;
} LevelDef;

#endif // LEVELS_H
