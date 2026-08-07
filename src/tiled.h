/*******************************************************************************************
*
*   tiled.h - Tiled (.tmx) level loading for hex-magical
*
*   Conventions expected in the .tmx (strict — load fails loud otherwise):
*     - One or more external tilesets (<tileset firstgid source="...tsx"/>);
*       Tiled tile animations (<animation>/<frame>) are played at draw time.
*       Collection-of-images tilesets (columns="0", per-tile <image>) are supported
*       for decorative backgrounds — they never contribute terrain collision.
*     - Tile layer "terrain": visual + collision tiles (TSX collision objects)
*     - Optional extra tile layers "terrain-*" and "sprites": visual only (drawn
*       above terrain, below art). Collision always comes from "terrain" alone.
*     - Point object named "ball-spawn" (or "ball")
*     - Polygon/rect object named "finish-line": ball touching it wins
*     - Optional polygon/rect objects named "no-build": players cannot sketch inside
*     - Optional polygon/rect objects named "pit": ball inside = game over
*     - Optional polygon/rect objects named "boost": ball inside gets a speed boost
*     - Optional polygon/rect objects named "anti-gravity" with required custom
*       property "gravity-angle" (float degrees): ball inside gets gravity rotated
*       that many degrees clockwise from default down
*     - Optional polygon object named "sun-track": closed sky path for a day/night
*       cycle (sun travels one half, moon the other). Also enables drifting
*       clouds + sun lighting/god-rays. Sprites from
*       resources/spritesheet/isolated/{sun,moon,cloud-1,cloud-2}.png.
*     - Optional rect/polygon named "clouds": spawn band for drifting clouds
*       (falls back to the sun-track sky bounds when absent).
*     - Optional decorative tile objects / image layers (no physics):
*         * Object layer "background" (or any name other than "art"): drawn behind terrain
*         * Object layer "art": tile objects drawn above terrain (z above the tile layer)
*         * Image layers (<imagelayer> with an <image> child): always behind terrain
*         * Layer Parallax Factor in Tiled (parallaxx / parallaxy) is honored —
*           1 = locked to the world, 0 = locked to the camera
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
#define TILED_MAX_TILESETS 8
#define TILED_MAX_ANIM_FRAMES 8
#define TILED_MAX_ANIMATED_TILES 32
#define TILED_MAX_TILE_IMAGES 128 // per collection-of-images tileset
#define TILED_MAX_DECORS 128      // background images / tile objects per map
#define TILED_CLOUD_TEX_COUNT 2   // cloud-1.png, cloud-2.png
#define TILED_MAX_VIS_LAYERS 6    // extra visual tile layers (terrain-*, sprites)

// Per-frame celestial snapshot for lighting (world canvas coords).
typedef struct CelestialFrame
{
    bool active;
    Vector2 bodyWorld; // sun or moon position on the track
    float sunIntensity; // 0..1 — drives light / god-rays
    float night;        // 0..1
} CelestialFrame;

// One tile's animation cycle from the .tsx (<animation>/<frame>).
typedef struct TiledTileAnim
{
    int localTileId;
    int frameCount;
    int totalDurationMs;
    int frameTileIds[TILED_MAX_ANIM_FRAMES];
    int frameDurationsMs[TILED_MAX_ANIM_FRAMES];
} TiledTileAnim;

// Per-tile image for a collection-of-images tileset (columns="0").
typedef struct TiledTileImage
{
    Texture2D texture;
    int width;
    int height;
} TiledTileImage;

// External tileset referenced by a map (<tileset firstgid source>).
typedef struct TiledTileset
{
    int firstGid;
    int tileCount;
    int columns;            // 0 = collection-of-images (per-tile textures)
    bool imageCollection;
    Texture2D texture;      // atlas — unused when imageCollection
    TiledTileImage tileImages[TILED_MAX_TILE_IMAGES]; // indexed by local id
    long modTime;
    char tsxPath[512];
    TiledTileAnim anims[TILED_MAX_ANIMATED_TILES];
    int animCount;
} TiledTileset;

// Decorative image (tile object or imagelayer). No physics.
typedef struct TiledDecor
{
    Texture2D texture;
    bool ownsTexture; // true for imagelayer images; false when borrowed from a tileset
    bool aboveTerrain; // true for object layer "art" (drawn after terrain tiles)
    Rectangle src;
    Rectangle dest;   // canvas coords at parallax 1,1 (tile-object y converted to top-left)
    float parallaxX;  // Tiled layer parallax factor (1 = world-locked)
    float parallaxY;
    float opacity;    // Tiled layer/object opacity (0..1), applied as tint alpha
} TiledDecor;

typedef struct TiledLevel
{
    bool loaded;
    char tmxPath[512];
    char name[64];
    long modTime;

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

    // Heap-sized to mapW*mapH (not the TILED_MAX_* caps) — freed on unload.
    int *terrainGids;
    // Extra visual-only tile layers (terrain-2, sprites, …), document order.
    int *visGids[TILED_MAX_VIS_LAYERS];
    char visLayerNames[TILED_MAX_VIS_LAYERS][32];
    int visLayerCount;
    TiledTileset tilesets[TILED_MAX_TILESETS];
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
    GravityZone antiGravity[TILED_MAX_ZONES];
    int antiGravityCount;

    // Optional day/night celestial path (act-3 map-1 pilot). Decorative only.
    bool hasSunTrack;
    PolyZone sunTrack;
    bool hasCloudBand;
    PolyZone cloudBand; // optional "clouds" rect — spawn band for drifting clouds
    Texture2D sunTex;
    Texture2D moonTex;
    Texture2D cloudTex[TILED_CLOUD_TEX_COUNT];

    TiledDecor decors[TILED_MAX_DECORS]; // prop images; aboveTerrain selects draw pass
    int decorCount;

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

// Draw backgrounds, terrain, art (above terrain), and zone overlays.
// viewPan is the camera target offset from level center (same value as game.c).
// When the level has a sun-track, also fills the lighting cloud-mask RT.
void RenderTiledLevel(const TiledLevel *lvl, Vector2 viewPan);

// Latest celestial sample from the most recent RenderTiledLevel call.
CelestialFrame TiledLevelGetCelestialFrame(void);

// Draw drifting clouds into the current target (used for the lighting cloud-mask pass).
// Call while Mode2D is active. No-op when the level has no sun-track.
void TiledLevelRenderCloudMask(const TiledLevel *lvl);

#endif // TILED_H

