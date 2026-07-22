/*******************************************************************************************
*
*   tiled.h - Tiled (.tmx) level loading for hex-magical
*
*   Conventions expected in the .tmx (strict — load fails loud otherwise):
*     - Tile layer "prototype": gid 15 = solid collision, anything else = air
*     - Tile layer "terrain":   visual tiles drawn from tileset.png (same dir as .tmx)
*     - Point objects named "ball-spawn" and "level-goal"
*     - CSV-encoded layer data, non-infinite map
*
********************************************************************************************/

#ifndef TILED_H
#define TILED_H

#include "levels.h"
#include "raylib.h"

#include <stdbool.h>

#define TILED_MAX_W 64
#define TILED_MAX_H 64
#define TILED_MAX_BOXES 256

typedef struct TiledLevel
{
    bool loaded;
    char tmxPath[512];
    long modTime;

    int mapWidth;      // tiles
    int mapHeight;
    int tileWidth;     // map pixels
    int tileHeight;

    float scale;       // map pixels -> game canvas pixels
    Vector2 offset;    // letterbox offset inside the canvas

    int terrainGids[TILED_MAX_W * TILED_MAX_H];
    Texture2D tileset;
    int tilesetColumns;
    int tilesetCount;

    // Greedy-merged collision rectangles from the prototype layer
    StaticBox boxes[TILED_MAX_BOXES];
    int boxCount;

    LevelDef def;      // ready to hand to PhysicsLoadLevel (boxes points into this struct)
} TiledLevel;

// Parse + (re)load. Safe to call on an already-loaded level (hot reload):
// on parse failure the previous state is kept.
bool TiledLevelLoad(TiledLevel *lvl, const char *tmxPath);
void TiledLevelUnload(TiledLevel *lvl);

// True when the .tmx on disk is newer than what is loaded
bool TiledLevelFileChanged(const TiledLevel *lvl);

// Draw the terrain tile layer (canvas coordinates)
void RenderTiledLevel(const TiledLevel *lvl);

#endif // TILED_H
