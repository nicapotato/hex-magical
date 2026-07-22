/*******************************************************************************************
*
*   tiled.c - Tiled (.tmx) level loading for hex-magical
*
*   Minimal TMX reader for the exact subset this project uses (see tiled.h).
*   Fails loud: any structural surprise aborts the load with a TraceLog error
*   and the previous level state is kept.
*
********************************************************************************************/

#include "tiled.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOLID_GID 15   // gid on the "prototype" layer that means collision
#define WALL_THICKNESS 24.0f

//----------------------------------------------------------------------------------
// Tiny XML-attribute helpers (attribute search scoped to one tag)
//----------------------------------------------------------------------------------
static bool ParseFloatAttr(const char *tag, const char *attr, float *out)
{
    // attr passed as e.g. "x=\"" — search only inside this tag
    const char *end = strchr(tag, '>');
    const char *p = strstr(tag, attr);
    if ((p == NULL) || ((end != NULL) && (p > end))) return false;
    *out = strtof(p + strlen(attr), NULL);
    return true;
}

static bool ParseIntAttr(const char *tag, const char *attr, int *out)
{
    float v = 0.0f;
    if (!ParseFloatAttr(tag, attr, &v)) return false;
    *out = (int)v;
    return true;
}

// Parse CSV ints between <data encoding="csv"> and </data>. Returns count.
static int ParseCsv(const char *dataStart, int *out, int maxCount)
{
    const char *p = strchr(dataStart, '>');
    if (p == NULL) return 0;
    p++;

    const char *end = strstr(p, "</data>");
    if (end == NULL) return 0;

    int count = 0;
    while ((p < end) && (count < maxCount))
    {
        while ((p < end) && ((*p < '0') || (*p > '9'))) p++;
        if (p >= end) break;
        char *next = NULL;
        out[count++] = (int)strtol(p, &next, 10);
        p = next;
    }
    return count;
}

// Find `<layer ... name="<name>" ...>` and parse its CSV data
static bool ParseTileLayer(const char *xml, const char *layerName, int *out, int expectedCount)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "name=\"%s\"", layerName);

    const char *layer = strstr(xml, needle);
    if (layer == NULL)
    {
        TraceLog(LOG_ERROR, "TILED: layer \"%s\" not found", layerName);
        return false;
    }

    const char *data = strstr(layer, "<data");
    const char *dataTagEnd = (data != NULL) ? strchr(data, '>') : NULL;
    const char *csv = (data != NULL) ? strstr(data, "encoding=\"csv\"") : NULL;
    if ((dataTagEnd == NULL) || (csv == NULL) || (csv > dataTagEnd))
    {
        TraceLog(LOG_ERROR, "TILED: layer \"%s\" has no CSV <data> (set map tile layer format to CSV)", layerName);
        return false;
    }

    int count = ParseCsv(data, out, expectedCount);
    if (count != expectedCount)
    {
        TraceLog(LOG_ERROR, "TILED: layer \"%s\" has %d tiles, expected %d", layerName, count, expectedCount);
        return false;
    }
    return true;
}

// Objects are written as <object ... name="..." x="..." y="...">; attrs precede the
// name, so back up to the tag start before reading them
static const char *FindObjectTag(const char *xml, const char *objectName)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "name=\"%s\"", objectName);

    const char *at = strstr(xml, needle);
    if (at == NULL) return NULL;

    while ((at > xml) && (*at != '<')) at--;
    return at;
}

static bool ParsePointObject(const char *xml, const char *objectName, Vector2 *out)
{
    const char *obj = FindObjectTag(xml, objectName);
    if (obj == NULL) return false;

    if (!ParseFloatAttr(obj, " x=\"", &out->x) || !ParseFloatAttr(obj, " y=\"", &out->y))
    {
        TraceLog(LOG_ERROR, "TILED: object \"%s\" missing x/y", objectName);
        return false;
    }
    return true;
}

// Parse every object named "no-build" (polygon or rect) into zones (map coords)
static int ParseNoBuildZones(const char *xml, NoBuildZone *zones, int maxZones)
{
    int zoneCount = 0;
    const char *cursor = xml;

    while (zoneCount < maxZones)
    {
        const char *match = strstr(cursor, "name=\"no-build\"");
        if (match == NULL) break;
        cursor = match + 1; // continue search past this attribute next iteration

        const char *obj = match;
        while ((obj > xml) && (*obj != '<')) obj--;

        float ox = 0.0f, oy = 0.0f;
        if (!ParseFloatAttr(obj, " x=\"", &ox) || !ParseFloatAttr(obj, " y=\"", &oy))
        {
            TraceLog(LOG_ERROR, "TILED: no-build object missing x/y — skipped");
            continue;
        }

        NoBuildZone *zone = &zones[zoneCount];
        zone->pointCount = 0;

        const char *objEnd = strstr(obj, "</object>");
        const char *selfClose = strstr(obj, "/>");
        if ((objEnd == NULL) || ((selfClose != NULL) && (selfClose < objEnd))) objEnd = selfClose;

        const char *poly = strstr(obj, "<polygon points=\"");
        if ((poly != NULL) && (objEnd != NULL) && (poly < objEnd))
        {
            // Polygon: points are "x0,y0 x1,y1 ..." relative to the object origin
            poly += strlen("<polygon points=\"");
            const char *q = strchr(poly, '"');
            while ((poly < q) && (zone->pointCount < TILED_MAX_NOBUILD_POINTS))
            {
                char *next = NULL;
                float px = strtof(poly, &next);
                if ((next == poly) || (*next != ',')) break;
                poly = next + 1;
                float py = strtof(poly, &next);
                if (next == poly) break;
                poly = next;
                zone->points[zone->pointCount++] = (Vector2){ ox + px, oy + py };
            }
        }
        else
        {
            // Rectangle object: width/height attrs, origin at top-left
            float w = 0.0f, h = 0.0f;
            if (!ParseFloatAttr(obj, " width=\"", &w) || !ParseFloatAttr(obj, " height=\"", &h))
            {
                TraceLog(LOG_ERROR, "TILED: no-build object has neither polygon nor width/height — skipped");
                continue;
            }
            zone->points[0] = (Vector2){ ox, oy };
            zone->points[1] = (Vector2){ ox + w, oy };
            zone->points[2] = (Vector2){ ox + w, oy + h };
            zone->points[3] = (Vector2){ ox, oy + h };
            zone->pointCount = 4;
        }

        if (zone->pointCount >= 3) zoneCount++;
        else TraceLog(LOG_ERROR, "TILED: no-build polygon needs >= 3 points — skipped");
    }

    return zoneCount;
}

// Ray-cast point-in-polygon (handles concave outlines)
static bool PolygonContains(const Vector2 *pts, int count, Vector2 p)
{
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++)
    {
        if (((pts[i].y > p.y) != (pts[j].y > p.y)) &&
            (p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x))
        {
            inside = !inside;
        }
    }
    return inside;
}

//----------------------------------------------------------------------------------
// Collision: greedy-merge solid tiles into rectangles
//----------------------------------------------------------------------------------
static int MergeSolidTiles(const int *gids, int w, int h, int solidGid,
                           StaticBox *boxes, int maxBoxes,
                           float tileW, float tileH, float scale, Vector2 offset)
{
    static bool used[TILED_MAX_W * TILED_MAX_H];
    memset(used, 0, sizeof(used));

    int boxCount = 0;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int i = y * w + x;
            if (used[i] || (gids[i] != solidGid)) continue;

            // Extend right
            int runW = 1;
            while ((x + runW < w) && !used[i + runW] && (gids[i + runW] == solidGid)) runW++;

            // Extend down while the full row segment is solid and unused
            int runH = 1;
            while (y + runH < h)
            {
                bool rowOk = true;
                for (int k = 0; k < runW; k++)
                {
                    int j = (y + runH) * w + x + k;
                    if (used[j] || (gids[j] != solidGid)) { rowOk = false; break; }
                }
                if (!rowOk) break;
                runH++;
            }

            for (int yy = 0; yy < runH; yy++)
                for (int xx = 0; xx < runW; xx++)
                    used[(y + yy) * w + x + xx] = true;

            if (boxCount >= maxBoxes)
            {
                TraceLog(LOG_ERROR, "TILED: more than %d collision boxes after merging", maxBoxes);
                return -1;
            }

            float px = offset.x + (float)x * tileW * scale;
            float py = offset.y + (float)y * tileH * scale;
            float pw = (float)runW * tileW * scale;
            float ph = (float)runH * tileH * scale;
            boxes[boxCount++] = (StaticBox){ px + pw * 0.5f, py + ph * 0.5f, pw * 0.5f, ph * 0.5f, 0.0f };
        }
    }
    return boxCount;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
bool TiledLevelLoad(TiledLevel *lvl, const char *tmxPath)
{
    char *xml = LoadFileText(tmxPath);
    if (xml == NULL)
    {
        TraceLog(LOG_WARNING, "TILED: cannot read %s", tmxPath);
        return false;
    }

    // Parse into a temp so a bad edit during hot reload keeps the old level
    static TiledLevel tmp;
    memset(&tmp, 0, sizeof(tmp));
    snprintf(tmp.tmxPath, sizeof(tmp.tmxPath), "%s", tmxPath);

    bool ok = true;
    const char *mapTag = strstr(xml, "<map ");
    ok = ok && (mapTag != NULL);
    ok = ok && ParseIntAttr(mapTag, " width=\"", &tmp.mapWidth);
    ok = ok && ParseIntAttr(mapTag, " height=\"", &tmp.mapHeight);
    ok = ok && ParseIntAttr(mapTag, " tilewidth=\"", &tmp.tileWidth);
    ok = ok && ParseIntAttr(mapTag, " tileheight=\"", &tmp.tileHeight);

    if (ok && ((tmp.mapWidth > TILED_MAX_W) || (tmp.mapHeight > TILED_MAX_H)))
    {
        TraceLog(LOG_ERROR, "TILED: map %dx%d exceeds max %dx%d", tmp.mapWidth, tmp.mapHeight, TILED_MAX_W, TILED_MAX_H);
        ok = false;
    }

    int tileCount = ok ? tmp.mapWidth * tmp.mapHeight : 0;
    static int prototypeGids[TILED_MAX_W * TILED_MAX_H];

    ok = ok && ParseTileLayer(xml, "prototype", prototypeGids, tileCount);
    ok = ok && ParseTileLayer(xml, "terrain", tmp.terrainGids, tileCount);

    Vector2 spawnMap = { 0 }, goalMap = { 0 };
    if (ok && !ParsePointObject(xml, "ball-spawn", &spawnMap) && !ParsePointObject(xml, "ball", &spawnMap))
    {
        TraceLog(LOG_ERROR, "TILED: no spawn point object (name it \"ball-spawn\" or \"ball\")");
        ok = false;
    }
    if (ok && !ParsePointObject(xml, "level-goal", &goalMap))
    {
        TraceLog(LOG_ERROR, "TILED: point object \"level-goal\" not found");
        ok = false;
    }

    if (ok) tmp.noBuildCount = ParseNoBuildZones(xml, tmp.noBuild, TILED_MAX_NOBUILD);

    UnloadFileText(xml);
    if (!ok) return false;

    // Fit the map into the game canvas (letterboxed, centered)
    float mapPxW = (float)(tmp.mapWidth * tmp.tileWidth);
    float mapPxH = (float)(tmp.mapHeight * tmp.tileHeight);
    tmp.scale = ((float)GAME_SCREEN_WIDTH / mapPxW < (float)GAME_SCREEN_HEIGHT / mapPxH)
              ? (float)GAME_SCREEN_WIDTH / mapPxW
              : (float)GAME_SCREEN_HEIGHT / mapPxH;
    tmp.offset = (Vector2){
        ((float)GAME_SCREEN_WIDTH - mapPxW * tmp.scale) * 0.5f,
        ((float)GAME_SCREEN_HEIGHT - mapPxH * tmp.scale) * 0.5f
    };

    tmp.boxCount = MergeSolidTiles(prototypeGids, tmp.mapWidth, tmp.mapHeight, SOLID_GID,
                                   tmp.boxes, TILED_MAX_BOXES - 4,
                                   (float)tmp.tileWidth, (float)tmp.tileHeight, tmp.scale, tmp.offset);
    if (tmp.boxCount <= 0)
    {
        TraceLog(LOG_ERROR, "TILED: no solid tiles (gid %d) on the prototype layer", SOLID_GID);
        return false;
    }

    // Boundary walls framing the map so the ball can't escape off an open edge
    float left = tmp.offset.x, top = tmp.offset.y;
    float right = tmp.offset.x + mapPxW * tmp.scale, bottom = tmp.offset.y + mapPxH * tmp.scale;
    float midX = (left + right) * 0.5f, midY = (top + bottom) * 0.5f;
    float halfW = (right - left) * 0.5f + WALL_THICKNESS, halfH = (bottom - top) * 0.5f + WALL_THICKNESS;
    tmp.boxes[tmp.boxCount++] = (StaticBox){ left - WALL_THICKNESS * 0.5f, midY, WALL_THICKNESS * 0.5f, halfH, 0.0f };
    tmp.boxes[tmp.boxCount++] = (StaticBox){ right + WALL_THICKNESS * 0.5f, midY, WALL_THICKNESS * 0.5f, halfH, 0.0f };
    tmp.boxes[tmp.boxCount++] = (StaticBox){ midX, top - WALL_THICKNESS * 0.5f, halfW, WALL_THICKNESS * 0.5f, 0.0f };
    tmp.boxes[tmp.boxCount++] = (StaticBox){ midX, bottom + WALL_THICKNESS * 0.5f, halfW, WALL_THICKNESS * 0.5f, 0.0f };

    // Tileset: <dir>/tileset.tsx -> columns/tilecount + image file
    const char *dir = GetDirectoryPath(tmxPath);
    char tsxPath[512];
    snprintf(tsxPath, sizeof(tsxPath), "%s/tileset.tsx", dir);
    char *tsx = LoadFileText(tsxPath);
    if (tsx == NULL)
    {
        TraceLog(LOG_ERROR, "TILED: cannot read %s", tsxPath);
        return false;
    }
    const char *tilesetTag = strstr(tsx, "<tileset");
    ok = (tilesetTag != NULL)
      && ParseIntAttr(tilesetTag, " columns=\"", &tmp.tilesetColumns)
      && ParseIntAttr(tilesetTag, " tilecount=\"", &tmp.tilesetCount);
    char imageName[128] = { 0 };
    const char *img = strstr(tsx, "<image source=\"");
    if (ok && (img != NULL))
    {
        img += strlen("<image source=\"");
        const char *q = strchr(img, '"');
        if ((q != NULL) && ((size_t)(q - img) < sizeof(imageName)))
        {
            memcpy(imageName, img, (size_t)(q - img));
        }
        else ok = false;
    }
    else ok = false;
    UnloadFileText(tsx);
    if (!ok)
    {
        TraceLog(LOG_ERROR, "TILED: failed to parse %s", tsxPath);
        return false;
    }

    char pngPath[512];
    snprintf(pngPath, sizeof(pngPath), "%s/%s", dir, imageName);
    Texture2D tex = LoadTexture(pngPath);
    if (tex.id == 0)
    {
        TraceLog(LOG_ERROR, "TILED: failed to load tileset image %s", pngPath);
        return false;
    }

    // Canvas coords for spawn/goal/no-build
    Vector2 spawn = { tmp.offset.x + spawnMap.x * tmp.scale, tmp.offset.y + spawnMap.y * tmp.scale };
    Vector2 goal = { tmp.offset.x + goalMap.x * tmp.scale, tmp.offset.y + goalMap.y * tmp.scale };
    for (int z = 0; z < tmp.noBuildCount; z++)
    {
        for (int i = 0; i < tmp.noBuild[z].pointCount; i++)
        {
            tmp.noBuild[z].points[i].x = tmp.offset.x + tmp.noBuild[z].points[i].x * tmp.scale;
            tmp.noBuild[z].points[i].y = tmp.offset.y + tmp.noBuild[z].points[i].y * tmp.scale;
        }
    }

    // Commit: replace previous state
    if (lvl->loaded) UnloadTexture(lvl->tileset);
    *lvl = tmp;
    lvl->tileset = tex;
    lvl->modTime = GetFileModTime(tmxPath);
    lvl->loaded = true;
    snprintf(lvl->name, sizeof(lvl->name), "Tiled: %s", GetFileNameWithoutExt(tmxPath));

    lvl->def = (LevelDef){
        .name = lvl->name,
        .ballSpawn = spawn,
        .ballRadius = 18.0f,
        .starPos = goal,
        .starRadius = 22.0f,
        .boxes = lvl->boxes,
        .boxCount = lvl->boxCount,
    };

    TraceLog(LOG_INFO, "TILED: loaded %s (%dx%d tiles, %d collision boxes, %d no-build zones)",
             tmxPath, lvl->mapWidth, lvl->mapHeight, lvl->boxCount, lvl->noBuildCount);
    return true;
}

void TiledLevelUnload(TiledLevel *lvl)
{
    if (lvl->loaded) UnloadTexture(lvl->tileset);
    lvl->loaded = false;
}

bool TiledLevelFileChanged(const TiledLevel *lvl)
{
    if (!lvl->loaded) return false;
    return GetFileModTime(lvl->tmxPath) != lvl->modTime;
}

bool TiledLevelNoBuildContains(const TiledLevel *lvl, Vector2 p)
{
    if (!lvl->loaded) return false;
    for (int z = 0; z < lvl->noBuildCount; z++)
    {
        if (PolygonContains(lvl->noBuild[z].points, lvl->noBuild[z].pointCount, p)) return true;
    }
    return false;
}

static void DrawNoBuildZones(const TiledLevel *lvl)
{
    const Color fill = { 210, 50, 50, 28 };     // faint red wash
    const Color outline = { 210, 50, 50, 110 }; // soft crayon boundary

    for (int z = 0; z < lvl->noBuildCount; z++)
    {
        const NoBuildZone *zone = &lvl->noBuild[z];

        // Fan fill (fine for the convex-ish zones Tiled produces; outline carries the true shape)
        for (int i = 1; i < zone->pointCount - 1; i++)
        {
            DrawTriangle(zone->points[0], zone->points[i + 1], zone->points[i], fill);
            DrawTriangle(zone->points[0], zone->points[i], zone->points[i + 1], fill);
        }

        for (int i = 0; i < zone->pointCount; i++)
        {
            Vector2 a = zone->points[i];
            Vector2 b = zone->points[(i + 1) % zone->pointCount];
            DrawLineEx(a, b, 3.0f, outline);
        }

        // Center label so the meaning is readable at a glance
        Vector2 c = { 0 };
        for (int i = 0; i < zone->pointCount; i++)
        {
            c.x += zone->points[i].x;
            c.y += zone->points[i].y;
        }
        c.x /= (float)zone->pointCount;
        c.y /= (float)zone->pointCount;
        const char *label = "no build";
        int tw = MeasureText(label, 14);
        DrawText(label, (int)(c.x - (float)tw * 0.5f), (int)c.y - 7, 14, outline);
    }
}

void RenderTiledLevel(const TiledLevel *lvl)
{
    if (!lvl->loaded) return;

    float tw = (float)lvl->tileWidth;
    float th = (float)lvl->tileHeight;

    // Soft crayon fill under collision so solids read even where terrain art is sparse.
    // Last 4 boxes are the off-canvas boundary walls — skip them.
    for (int i = 0; i < lvl->boxCount - 4; i++)
    {
        const StaticBox *b = &lvl->boxes[i];
        DrawRectangle((int)(b->x - b->halfWidth), (int)(b->y - b->halfHeight),
                      (int)(b->halfWidth * 2.0f), (int)(b->halfHeight * 2.0f),
                      (Color){ 120, 80, 50, 36 });
    }

    for (int y = 0; y < lvl->mapHeight; y++)
    {
        for (int x = 0; x < lvl->mapWidth; x++)
        {
            int gid = lvl->terrainGids[y * lvl->mapWidth + x];
            if ((gid <= 0) || (gid > lvl->tilesetCount)) continue; // 0 = empty; > tilecount = automap internals

            Rectangle src = {
                (float)((gid - 1) % lvl->tilesetColumns) * tw,
                (float)((gid - 1) / lvl->tilesetColumns) * th,
                tw, th
            };
            Rectangle dest = {
                lvl->offset.x + (float)x * tw * lvl->scale,
                lvl->offset.y + (float)y * th * lvl->scale,
                tw * lvl->scale, th * lvl->scale
            };
            DrawTexturePro(lvl->tileset, src, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    DrawNoBuildZones(lvl);
}
