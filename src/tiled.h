/*******************************************************************************************
*
*   tiled.h - Tiled (.tmx) level loading for hex-magical
*
*   Conventions expected in the .tmx (strict — load fails loud otherwise):
*     - Tile layer "terrain": visual tiles whose TSX collision objects define physics
*     - Point object named "ball-spawn" (or "ball")
*     - Polygon/rect object named "finish-line": ball touching it wins
*     - Optional polygon/rect objects named "no-build": players cannot sketch inside
*     - Optional polygon/rect objects named "pit": ball inside = game over
*     - Optional polygon/rect objects named "boost": ball inside gets a speed boost
    *     - Required custom properties on the map (Map > Custom Properties in Tiled):
    *         "line-capacity" (float, tile-widths of crayon ink)
    *         "boost_line-capacity" (float, tile-widths of boost line ink)
    *         "cannon-count" (int, placeable cannons)
    *       Zero disables the resource and hides it from the player HUD.
*     - CSV-encoded layer data, non-infinite map
*
********************************************************************************************/

#ifndef TILED_H
#define TILED_H

#include "levels.h"
#include "raylib.h"

#include <stdbool.h>

#define TILED_MAX_W 256
#define TILED_MAX_H 256
#define TILED_MAX_BOXES 1024
#define TILED_MAX_POLYGONS 2048
#define TILED_MAX_ZONES 30

typedef struct TiledLevel
{
    bool loaded;
    char tmxPath[512];
    char name[64];
    long modTime;
    long tilesetModTime;

    int mapWidth;      // tiles
    int mapHeight;
    int tileWidth;     // map pixels
    int tileHeight;

    float scale;       // map pixels -> game canvas pixels (1.0 = Tiled 1:1)
    Vector2 offset;    // centers the map on the design canvas

    // Build resources from TMX custom properties, converted to canvas pixels
    float lineCapacity;
    float boostLineCapacity;
    int cannonCount;

    int terrainGids[TILED_MAX_W * TILED_MAX_H];
    Texture2D tileset;
    int tilesetColumns;
    int tilesetCount;

    // Full terrain tiles are merged into boxes; custom TSX shapes remain polygons.
    StaticBox boxes[TILED_MAX_BOXES];
    int boxCount;
    StaticPolygon polygons[TILED_MAX_POLYGONS];
    int polygonCount;

    PolyZone finishLine;
    PolyZone noBuild[TILED_MAX_ZONES];
    int noBuildCount;
    PolyZone pits[TILED_MAX_ZONES];
    int pitCount;
    PolyZone boosts[TILED_MAX_ZONES];
    int boostCount;

    LevelDef def;      // ready to hand to PhysicsLoadLevel (geometry points into this struct)
} TiledLevel;

// Parse + (re)load. Safe to call on an already-loaded level (hot reload):
// on parse failure the previous state is kept.
bool TiledLevelLoad(TiledLevel *lvl, const char *tmxPath);
void TiledLevelUnload(TiledLevel *lvl);

// True when the .tmx on disk is newer than what is loaded
bool TiledLevelFileChanged(const TiledLevel *lvl);

// True when the canvas-space point sits inside any no-build zone
bool TiledLevelNoBuildContains(const TiledLevel *lvl, Vector2 p);

// Draw the terrain tile layer + no-build overlays (canvas coordinates)
void RenderTiledLevel(const TiledLevel *lvl);

#endif // TILED_H
