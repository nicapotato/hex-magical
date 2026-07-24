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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALL_THICKNESS 24.0f
#define TILED_MAX_TILE_TYPES 512
#define TILED_MAX_SHAPES_PER_TILE 4

typedef struct TileCollisionShape
{
    Vector2 points[STATIC_POLYGON_MAX_POINTS];
    int pointCount;
} TileCollisionShape;

typedef struct TileCollision
{
    TileCollisionShape shapes[TILED_MAX_SHAPES_PER_TILE];
    int shapeCount;
} TileCollision;

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

// Find `<property name="<propName>" ... value="...">` anywhere in the document
// (Tiled property names are unique per map in this project) and read its value.
// Required properties fail loud so a map missing its parameters never half-loads.
static bool ParsePropertyFloat(const char *xml, const char *propName, float *out)
{
    char needle[96];
    snprintf(needle, sizeof(needle), "<property name=\"%s\"", propName);

    const char *tag = strstr(xml, needle);
    if (tag == NULL)
    {
        TraceLog(LOG_ERROR, "TILED: required custom property \"%s\" not found (add it in Tiled)", propName);
        return false;
    }
    if (!ParseFloatAttr(tag, " value=\"", out))
    {
        TraceLog(LOG_ERROR, "TILED: custom property \"%s\" has no value attribute", propName);
        return false;
    }
    return true;
}

static bool ParsePropertyInt(const char *xml, const char *propName, int *out)
{
    float v = 0.0f;
    if (!ParsePropertyFloat(xml, propName, &v)) return false;
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

// Parse every object with the given name (polygon or rect) into zones (map coords)
// End of an <object> element. Nested self-closing tags (e.g. <property .../>)
// must not be mistaken for the object terminator.
static const char *ObjectEnd(const char *obj)
{
    const char *openEnd = strchr(obj, '>');
    if (openEnd == NULL) return NULL;
    if ((openEnd > obj) && (*(openEnd - 1) == '/')) return openEnd; // <object .../>
    return strstr(openEnd, "</object>");
}

static int ParseZones(const char *xml, const char *objectName, PolyZone *zones, int maxZones)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "name=\"%s\"", objectName);

    int zoneCount = 0;
    const char *cursor = xml;

    while (true)
    {
        const char *match = strstr(cursor, needle);
        if (match == NULL) break;
        cursor = match + 1; // continue search past this attribute next iteration

        if (zoneCount >= maxZones)
        {
            TraceLog(LOG_ERROR, "TILED: more than %d \"%s\" objects", maxZones, objectName);
            return -1;
        }

        const char *obj = match;
        while ((obj > xml) && (*obj != '<')) obj--;

        float ox = 0.0f, oy = 0.0f;
        if (!ParseFloatAttr(obj, " x=\"", &ox) || !ParseFloatAttr(obj, " y=\"", &oy))
        {
            TraceLog(LOG_ERROR, "TILED: \"%s\" object missing x/y — skipped", objectName);
            continue;
        }

        PolyZone *zone = &zones[zoneCount];
        zone->pointCount = 0;

        const char *objEnd = ObjectEnd(obj);

        const char *poly = strstr(obj, "<polygon points=\"");
        if ((poly != NULL) && (objEnd != NULL) && (poly < objEnd))
        {
            // Polygon: points are "x0,y0 x1,y1 ..." relative to the object origin
            poly += strlen("<polygon points=\"");
            const char *q = strchr(poly, '"');
            while ((poly < q) && (zone->pointCount < POLY_ZONE_MAX_POINTS))
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
                TraceLog(LOG_ERROR, "TILED: \"%s\" object has neither polygon nor width/height — skipped", objectName);
                continue;
            }
            zone->points[0] = (Vector2){ ox, oy };
            zone->points[1] = (Vector2){ ox + w, oy };
            zone->points[2] = (Vector2){ ox + w, oy + h };
            zone->points[3] = (Vector2){ ox, oy + h };
            zone->pointCount = 4;
        }

        if (zone->pointCount >= 3) zoneCount++;
        else TraceLog(LOG_ERROR, "TILED: \"%s\" polygon needs >= 3 points — skipped", objectName);
    }

    return zoneCount;
}

// Read a float custom property scoped to one object (fail if missing).
static bool ParseObjectPropertyFloat(const char *obj, const char *objEnd,
                                     const char *propName, float *out)
{
    char needle[96];
    snprintf(needle, sizeof(needle), "<property name=\"%s\"", propName);
    const char *tag = strstr(obj, needle);
    if ((tag == NULL) || ((objEnd != NULL) && (tag >= objEnd)))
    {
        TraceLog(LOG_ERROR, "TILED: object missing required custom property \"%s\"", propName);
        return false;
    }
    if (!ParseFloatAttr(tag, " value=\"", out))
    {
        TraceLog(LOG_ERROR, "TILED: custom property \"%s\" has no value attribute", propName);
        return false;
    }
    return true;
}

// anti-gravity zones require a polygon/rect plus gravity-angle (degrees clockwise).
static int ParseGravityZones(const char *xml, GravityZone *zones, int maxZones)
{
    const char *objectName = "anti-gravity";
    char needle[64];
    snprintf(needle, sizeof(needle), "name=\"%s\"", objectName);

    int zoneCount = 0;
    const char *cursor = xml;

    while (true)
    {
        const char *match = strstr(cursor, needle);
        if (match == NULL) break;
        cursor = match + 1;

        if (zoneCount >= maxZones)
        {
            TraceLog(LOG_ERROR, "TILED: more than %d \"%s\" objects", maxZones, objectName);
            return -1;
        }

        const char *obj = match;
        while ((obj > xml) && (*obj != '<')) obj--;

        float ox = 0.0f, oy = 0.0f;
        if (!ParseFloatAttr(obj, " x=\"", &ox) || !ParseFloatAttr(obj, " y=\"", &oy))
        {
            TraceLog(LOG_ERROR, "TILED: \"%s\" object missing x/y", objectName);
            return -1;
        }

        const char *objEnd = ObjectEnd(obj);
        if (objEnd == NULL)
        {
            TraceLog(LOG_ERROR, "TILED: \"%s\" object is not closed", objectName);
            return -1;
        }

        GravityZone *zone = &zones[zoneCount];
        memset(zone, 0, sizeof(*zone));
        if (!ParseObjectPropertyFloat(obj, objEnd, "gravity-angle", &zone->gravityAngleDeg))
        {
            return -1;
        }

        const char *poly = strstr(obj, "<polygon points=\"");
        if ((poly != NULL) && (poly < objEnd))
        {
            poly += strlen("<polygon points=\"");
            const char *q = strchr(poly, '"');
            while ((poly < q) && (zone->zone.pointCount < POLY_ZONE_MAX_POINTS))
            {
                char *next = NULL;
                float px = strtof(poly, &next);
                if ((next == poly) || (*next != ',')) break;
                poly = next + 1;
                float py = strtof(poly, &next);
                if (next == poly) break;
                poly = next;
                zone->zone.points[zone->zone.pointCount++] = (Vector2){ ox + px, oy + py };
            }
        }
        else
        {
            float w = 0.0f, h = 0.0f;
            if (!ParseFloatAttr(obj, " width=\"", &w) || !ParseFloatAttr(obj, " height=\"", &h))
            {
                TraceLog(LOG_ERROR, "TILED: \"%s\" object has neither polygon nor width/height", objectName);
                return -1;
            }
            zone->zone.points[0] = (Vector2){ ox, oy };
            zone->zone.points[1] = (Vector2){ ox + w, oy };
            zone->zone.points[2] = (Vector2){ ox + w, oy + h };
            zone->zone.points[3] = (Vector2){ ox, oy + h };
            zone->zone.pointCount = 4;
        }

        if (zone->zone.pointCount < 3)
        {
            TraceLog(LOG_ERROR, "TILED: \"%s\" polygon needs >= 3 points", objectName);
            return -1;
        }
        zoneCount++;
    }

    return zoneCount;
}

// Map coords -> game canvas coords for authored zones
static void MapZonesToCanvas(PolyZone *zones, int count, float scale, Vector2 offset)
{
    for (int z = 0; z < count; z++)
    {
        for (int i = 0; i < zones[z].pointCount; i++)
        {
            zones[z].points[i].x = offset.x + zones[z].points[i].x * scale;
            zones[z].points[i].y = offset.y + zones[z].points[i].y * scale;
        }
    }
}

static void MapGravityZonesToCanvas(GravityZone *zones, int count, float scale, Vector2 offset)
{
    for (int z = 0; z < count; z++)
    {
        MapZonesToCanvas(&zones[z].zone, 1, scale, offset);
    }
}

static bool ParseCollisionPolygon(const char *points, Vector2 origin,
                                  TileCollisionShape *shape)
{
    const char *end = strchr(points, '"');
    if (end == NULL) return false;

    shape->pointCount = 0;
    while ((points < end) && (shape->pointCount < STATIC_POLYGON_MAX_POINTS))
    {
        char *next = NULL;
        float x = strtof(points, &next);
        if ((next == points) || (*next != ',')) return false;
        points = next + 1;

        float y = strtof(points, &next);
        if (next == points) return false;
        points = next;
        shape->points[shape->pointCount++] = (Vector2){ origin.x + x, origin.y + y };
    }

    while ((points < end) && ((*points == ' ') || (*points == '\t'))) points++;
    return (points == end) && (shape->pointCount >= 3);
}

static bool ParseTilesetCollisions(const char *tsx, TileCollision *tiles, int tileCapacity)
{
    memset(tiles, 0, (size_t)tileCapacity * sizeof(*tiles));
    const char *cursor = tsx;

    while ((cursor = strstr(cursor, "<tile ")) != NULL)
    {
        int tileId = -1;
        if (!ParseIntAttr(cursor, " id=\"", &tileId)
            || (tileId < 0) || (tileId >= tileCapacity))
        {
            TraceLog(LOG_ERROR, "TILED: collision tile has invalid or unsupported id");
            return false;
        }

        const char *tileEnd = strstr(cursor, "</tile>");
        if (tileEnd == NULL)
        {
            TraceLog(LOG_ERROR, "TILED: tile %d is not closed", tileId);
            return false;
        }

        TileCollision *collision = &tiles[tileId];
        const char *object = cursor;
        while ((object = strstr(object, "<object ")) != NULL && object < tileEnd)
        {
            if (collision->shapeCount >= TILED_MAX_SHAPES_PER_TILE)
            {
                TraceLog(LOG_ERROR, "TILED: tile %d exceeds %d collision shapes",
                         tileId, TILED_MAX_SHAPES_PER_TILE);
                return false;
            }

            Vector2 origin = { 0 };
            if (!ParseFloatAttr(object, " x=\"", &origin.x)
                || !ParseFloatAttr(object, " y=\"", &origin.y))
            {
                TraceLog(LOG_ERROR, "TILED: collision object on tile %d missing x/y", tileId);
                return false;
            }

            TileCollisionShape *shape = &collision->shapes[collision->shapeCount];
            const char *objectClose = strstr(object, "</object>");
            const char *selfClose = strstr(object, "/>");
            const char *objectEnd = objectClose;
            if ((objectEnd == NULL) || ((selfClose != NULL) && (selfClose < objectEnd)))
                objectEnd = selfClose;
            if ((objectEnd == NULL) || (objectEnd > tileEnd)) return false;

            const char *polygon = strstr(object, "<polygon points=\"");
            if ((polygon != NULL) && (polygon < objectEnd))
            {
                polygon += strlen("<polygon points=\"");
                if (!ParseCollisionPolygon(polygon, origin, shape))
                {
                    TraceLog(LOG_ERROR, "TILED: malformed collision polygon on tile %d", tileId);
                    return false;
                }
            }
            else
            {
                float width = 0.0f, height = 0.0f;
                if (!ParseFloatAttr(object, " width=\"", &width)
                    || !ParseFloatAttr(object, " height=\"", &height)
                    || (width <= 0.0f) || (height <= 0.0f))
                {
                    TraceLog(LOG_ERROR, "TILED: collision object on tile %d must be a polygon or rectangle", tileId);
                    return false;
                }
                shape->points[0] = origin;
                shape->points[1] = (Vector2){ origin.x + width, origin.y };
                shape->points[2] = (Vector2){ origin.x + width, origin.y + height };
                shape->points[3] = (Vector2){ origin.x, origin.y + height };
                shape->pointCount = 4;
            }

            collision->shapeCount++;
            object = objectEnd + 2;
        }

        cursor = tileEnd + strlen("</tile>");
    }
    return true;
}

static bool NearlyEqual(float a, float b)
{
    float difference = a - b;
    return (difference >= -0.01f) && (difference <= 0.01f);
}

static bool IsFullTileCollision(const TileCollision *collision, float tileWidth, float tileHeight)
{
    if (collision->shapeCount != 1) return false;
    const TileCollisionShape *shape = &collision->shapes[0];
    if (shape->pointCount != 4) return false;

    return NearlyEqual(shape->points[0].x, 0.0f)
        && NearlyEqual(shape->points[0].y, 0.0f)
        && NearlyEqual(shape->points[1].x, tileWidth)
        && NearlyEqual(shape->points[1].y, 0.0f)
        && NearlyEqual(shape->points[2].x, tileWidth)
        && NearlyEqual(shape->points[2].y, tileHeight)
        && NearlyEqual(shape->points[3].x, 0.0f)
        && NearlyEqual(shape->points[3].y, tileHeight);
}

//----------------------------------------------------------------------------------
// Collision: greedy-merge solid tiles into rectangles
//----------------------------------------------------------------------------------
static bool ResolveGid(const TiledTileset *tilesets, int tilesetCount, int gid,
                       int *outTsIndex, int *outLocalId);

static int MergeSolidTiles(const bool *solid, int w, int h,
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
            if (used[i] || !solid[i]) continue;

            // Extend right
            int runW = 1;
            while ((x + runW < w) && !used[i + runW] && solid[i + runW]) runW++;

            // Extend down while the full row segment is solid and unused
            int runH = 1;
            while (y + runH < h)
            {
                bool rowOk = true;
                for (int k = 0; k < runW; k++)
                {
                    int j = (y + runH) * w + x + k;
                    if (used[j] || !solid[j]) { rowOk = false; break; }
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

static int BuildCustomTilePolygons(const int *gids, int w, int h,
                                   const TiledTileset *tilesets, int tilesetCount,
                                   TileCollision (*collisions)[TILED_MAX_TILE_TYPES],
                                   StaticPolygon *polygons, int maxPolygons,
                                   float tileW, float tileH, float scale, Vector2 offset)
{
    int polygonCount = 0;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int gid = gids[y * w + x];
            int tsIndex = -1;
            int tileId = -1;
            if (!ResolveGid(tilesets, tilesetCount, gid, &tsIndex, &tileId)) continue;

            const TileCollision *collision = &collisions[tsIndex][tileId];
            if (IsFullTileCollision(collision, tileW, tileH)) continue;

            for (int s = 0; s < collision->shapeCount; s++)
            {
                if (polygonCount >= maxPolygons)
                {
                    TraceLog(LOG_ERROR, "TILED: more than %d custom terrain collision polygons", maxPolygons);
                    return -1;
                }

                const TileCollisionShape *source = &collision->shapes[s];
                StaticPolygon *dest = &polygons[polygonCount++];
                dest->pointCount = source->pointCount;
                for (int p = 0; p < source->pointCount; p++)
                {
                    dest->points[p] = (Vector2){
                        offset.x + ((float)x * tileW + source->points[p].x) * scale,
                        offset.y + ((float)y * tileH + source->points[p].y) * scale
                    };
                }
            }
        }
    }
    return polygonCount;
}

//----------------------------------------------------------------------------------
// Tileset refs + tile animations
//----------------------------------------------------------------------------------
static bool ResolveGid(const TiledTileset *tilesets, int tilesetCount, int gid,
                       int *outTsIndex, int *outLocalId)
{
    if (gid <= 0) return false;
    for (int i = 0; i < tilesetCount; i++)
    {
        int localId = gid - tilesets[i].firstGid;
        if ((localId >= 0) && (localId < tilesets[i].tileCount))
        {
            *outTsIndex = i;
            *outLocalId = localId;
            return true;
        }
    }
    return false;
}

static bool ParseTilesetAnimations(const char *tsx, TiledTileset *tileset)
{
    tileset->animCount = 0;
    const char *cursor = tsx;

    while ((cursor = strstr(cursor, "<tile ")) != NULL)
    {
        int tileId = -1;
        if (!ParseIntAttr(cursor, " id=\"", &tileId)
            || (tileId < 0) || (tileId >= tileset->tileCount))
        {
            TraceLog(LOG_ERROR, "TILED: animation tile has invalid id in %s", tileset->tsxPath);
            return false;
        }

        const char *tileEnd = strstr(cursor, "</tile>");
        if (tileEnd == NULL)
        {
            TraceLog(LOG_ERROR, "TILED: tile %d is not closed in %s", tileId, tileset->tsxPath);
            return false;
        }

        const char *anim = strstr(cursor, "<animation");
        if ((anim == NULL) || (anim >= tileEnd))
        {
            cursor = tileEnd + strlen("</tile>");
            continue;
        }

        if (tileset->animCount >= TILED_MAX_ANIMATED_TILES)
        {
            TraceLog(LOG_ERROR, "TILED: more than %d animated tiles in %s",
                     TILED_MAX_ANIMATED_TILES, tileset->tsxPath);
            return false;
        }

        TiledTileAnim *out = &tileset->anims[tileset->animCount];
        memset(out, 0, sizeof(*out));
        out->localTileId = tileId;

        const char *frame = anim;
        while ((frame = strstr(frame, "<frame ")) != NULL && frame < tileEnd)
        {
            if (out->frameCount >= TILED_MAX_ANIM_FRAMES)
            {
                TraceLog(LOG_ERROR, "TILED: tile %d exceeds %d animation frames in %s",
                         tileId, TILED_MAX_ANIM_FRAMES, tileset->tsxPath);
                return false;
            }

            int frameTileId = -1;
            int durationMs = 0;
            if (!ParseIntAttr(frame, " tileid=\"", &frameTileId)
                || !ParseIntAttr(frame, " duration=\"", &durationMs)
                || (frameTileId < 0) || (frameTileId >= tileset->tileCount)
                || (durationMs <= 0))
            {
                TraceLog(LOG_ERROR, "TILED: malformed animation frame on tile %d in %s",
                         tileId, tileset->tsxPath);
                return false;
            }

            out->frameTileIds[out->frameCount] = frameTileId;
            out->frameDurationsMs[out->frameCount] = durationMs;
            out->totalDurationMs += durationMs;
            out->frameCount++;
            frame += strlen("<frame ");
        }

        if (out->frameCount == 0)
        {
            TraceLog(LOG_ERROR, "TILED: tile %d has empty <animation> in %s",
                     tileId, tileset->tsxPath);
            return false;
        }

        tileset->animCount++;
        cursor = tileEnd + strlen("</tile>");
    }
    return true;
}

static int ResolveAnimatedLocalId(const TiledTileset *tileset, int localId, double timeSec)
{
    for (int i = 0; i < tileset->animCount; i++)
    {
        const TiledTileAnim *anim = &tileset->anims[i];
        if (anim->localTileId != localId) continue;
        if (anim->totalDurationMs <= 0) return localId;

        int ms = (int)(timeSec * 1000.0) % anim->totalDurationMs;
        if (ms < 0) ms += anim->totalDurationMs;

        int elapsed = 0;
        for (int f = 0; f < anim->frameCount; f++)
        {
            elapsed += anim->frameDurationsMs[f];
            if (ms < elapsed) return anim->frameTileIds[f];
        }
        return anim->frameTileIds[anim->frameCount - 1];
    }
    return localId;
}

// Load every external tileset listed on the map. Skips Tiled-internal
// sources (":/...") such as automap helper tiles.
static bool LoadMapTilesets(const char *xml, const char *mapDir, TiledLevel *lvl,
                            TileCollision (*collisions)[TILED_MAX_TILE_TYPES])
{
    lvl->tilesetCount = 0;
    const char *cursor = xml;

    while ((cursor = strstr(cursor, "<tileset ")) != NULL)
    {
        int firstGid = 0;
        if (!ParseIntAttr(cursor, " firstgid=\"", &firstGid) || (firstGid <= 0))
        {
            TraceLog(LOG_ERROR, "TILED: tileset missing valid firstgid");
            return false;
        }

        const char *sourceAttr = strstr(cursor, " source=\"");
        const char *tagEnd = strchr(cursor, '>');
        if ((sourceAttr == NULL) || ((tagEnd != NULL) && (sourceAttr > tagEnd)))
        {
            TraceLog(LOG_ERROR, "TILED: embedded tilesets are unsupported (use external .tsx)");
            return false;
        }

        sourceAttr += strlen(" source=\"");
        const char *sourceEnd = strchr(sourceAttr, '"');
        if (sourceEnd == NULL) return false;

        char source[256];
        size_t sourceLen = (size_t)(sourceEnd - sourceAttr);
        if (sourceLen >= sizeof(source))
        {
            TraceLog(LOG_ERROR, "TILED: tileset source path too long");
            return false;
        }
        memcpy(source, sourceAttr, sourceLen);
        source[sourceLen] = '\0';

        // Tiled automap helpers — not game art; GIDs from these are skipped at draw.
        if (strncmp(source, ":/", 2) == 0)
        {
            cursor = sourceEnd;
            continue;
        }

        if (lvl->tilesetCount >= TILED_MAX_TILESETS)
        {
            TraceLog(LOG_ERROR, "TILED: more than %d external tilesets", TILED_MAX_TILESETS);
            return false;
        }

        TiledTileset *ts = &lvl->tilesets[lvl->tilesetCount];
        memset(ts, 0, sizeof(*ts));
        ts->firstGid = firstGid;
        snprintf(ts->tsxPath, sizeof(ts->tsxPath), "%s/%s", mapDir, source);

        char *tsx = LoadFileText(ts->tsxPath);
        if (tsx == NULL)
        {
            TraceLog(LOG_ERROR, "TILED: cannot read %s", ts->tsxPath);
            return false;
        }

        const char *tilesetTag = strstr(tsx, "<tileset");
        bool ok = (tilesetTag != NULL)
            && ParseIntAttr(tilesetTag, " columns=\"", &ts->columns)
            && ParseIntAttr(tilesetTag, " tilecount=\"", &ts->tileCount)
            && (ts->columns > 0)
            && (ts->tileCount > 0)
            && (ts->tileCount <= TILED_MAX_TILE_TYPES);

        if (ok) ok = ParseTilesetCollisions(tsx, collisions[lvl->tilesetCount], TILED_MAX_TILE_TYPES);
        if (ok) ok = ParseTilesetAnimations(tsx, ts);

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
            TraceLog(LOG_ERROR, "TILED: failed to parse %s", ts->tsxPath);
            return false;
        }

        char pngPath[512];
        snprintf(pngPath, sizeof(pngPath), "%s/%s", GetDirectoryPath(ts->tsxPath), imageName);

        if (IsWindowReady())
        {
            ts->texture = LoadTexture(pngPath);
            if (ts->texture.id == 0)
            {
                TraceLog(LOG_ERROR, "TILED: failed to load tileset image %s", pngPath);
                return false;
            }
        }

        ts->modTime = GetFileModTime(ts->tsxPath);
        lvl->tilesetCount++;
        cursor = sourceEnd;
    }

    if (lvl->tilesetCount == 0)
    {
        TraceLog(LOG_ERROR, "TILED: map has no loadable external tilesets");
        return false;
    }
    return true;
}

static void UnloadLevelTilesets(TiledLevel *lvl)
{
    if (!lvl->loaded) return;
    for (int i = 0; i < lvl->tilesetCount; i++)
    {
        if (lvl->tilesets[i].texture.id != 0) UnloadTexture(lvl->tilesets[i].texture);
        lvl->tilesets[i].texture = (Texture2D){ 0 };
    }
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
    ok = ok && ParseTileLayer(xml, "terrain", tmp.terrainGids, tileCount);

    // Level parameters (required). Authored in tile-widths of ink; converted to
    // canvas pixels below once the letterbox scale is known.
    float lineCapacityTiles = 0.0f;
    float boostLineCapacityTiles = 0.0f;
    ok = ok && ParsePropertyFloat(xml, "line-capacity", &lineCapacityTiles);
    ok = ok && ParsePropertyFloat(xml, "boost_line-capacity", &boostLineCapacityTiles);
    ok = ok && ParsePropertyInt(xml, "cannon-count", &tmp.cannonCount);
    if (ok && ((lineCapacityTiles < 0.0f) || (boostLineCapacityTiles < 0.0f) || (tmp.cannonCount < 0)))
    {
        TraceLog(LOG_ERROR, "TILED: level parameters must be >= 0");
        ok = false;
    }

    Vector2 spawnMap = { 0 };
    if (ok && !ParsePointObject(xml, "ball-spawn", &spawnMap) && !ParsePointObject(xml, "ball", &spawnMap))
    {
        TraceLog(LOG_ERROR, "TILED: no spawn point object (name it \"ball-spawn\" or \"ball\")");
        ok = false;
    }

    PolyZone finishZones[1] = { 0 };
    if (ok && (ParseZones(xml, "finish-line", finishZones, 1) != 1))
    {
        TraceLog(LOG_ERROR, "TILED: exactly one \"finish-line\" polygon/rect object required");
        ok = false;
    }

    if (ok)
    {
        tmp.noBuildCount = ParseZones(xml, "no-build", tmp.noBuild, TILED_MAX_ZONES);
        tmp.pitCount = ParseZones(xml, "pit", tmp.pits, TILED_MAX_ZONES);
        tmp.boostCount = ParseZones(xml, "boost", tmp.boosts, TILED_MAX_ZONES);
        tmp.antiGravityCount = ParseGravityZones(xml, tmp.antiGravity, TILED_MAX_ZONES);
        if ((tmp.noBuildCount < 0) || (tmp.pitCount < 0) || (tmp.boostCount < 0)
            || (tmp.antiGravityCount < 0)) ok = false;
    }

    // External tilesets listed on the map (supports multi-tileset maps like map-11).
    const char *dir = GetDirectoryPath(tmxPath);
    static TileCollision tileCollisions[TILED_MAX_TILESETS][TILED_MAX_TILE_TYPES];
    if (ok) ok = LoadMapTilesets(xml, dir, &tmp, tileCollisions);

    UnloadFileText(xml);
    if (!ok)
    {
        // LoadMapTilesets may have loaded some textures before failing.
        tmp.loaded = true; // so UnloadLevelTilesets walks the partial list
        UnloadLevelTilesets(&tmp);
        return false;
    }

    // 1:1 with Tiled — map pixels are canvas pixels (no fit-to-screen shrink).
    // Offset centers the map on the design canvas; large maps extend past the
    // view and are navigated with WASD pan / +/- zoom.
    float mapPxW = (float)(tmp.mapWidth * tmp.tileWidth);
    float mapPxH = (float)(tmp.mapHeight * tmp.tileHeight);
    tmp.scale = 1.0f;
    tmp.offset = (Vector2){
        ((float)GAME_SCREEN_WIDTH - mapPxW * tmp.scale) * 0.5f,
        ((float)GAME_SCREEN_HEIGHT - mapPxH * tmp.scale) * 0.5f
    };

    // Ink budgets: authored in tile-widths, spent in canvas pixels
    tmp.lineCapacity = lineCapacityTiles * (float)tmp.tileWidth * tmp.scale;
    tmp.boostLineCapacity = boostLineCapacityTiles * (float)tmp.tileWidth * tmp.scale;

    static bool fullTileCollision[TILED_MAX_W * TILED_MAX_H];
    for (int i = 0; i < tileCount; i++)
    {
        int tsIndex = -1;
        int tileId = -1;
        fullTileCollision[i] = ResolveGid(tmp.tilesets, tmp.tilesetCount, tmp.terrainGids[i],
                                          &tsIndex, &tileId)
                            && IsFullTileCollision(&tileCollisions[tsIndex][tileId],
                                                   (float)tmp.tileWidth, (float)tmp.tileHeight);
    }

    tmp.boxCount = MergeSolidTiles(fullTileCollision, tmp.mapWidth, tmp.mapHeight,
                                   tmp.boxes, TILED_MAX_BOXES - 4,
                                   (float)tmp.tileWidth, (float)tmp.tileHeight, tmp.scale, tmp.offset);
    tmp.polygonCount = BuildCustomTilePolygons(
        tmp.terrainGids, tmp.mapWidth, tmp.mapHeight,
        tmp.tilesets, tmp.tilesetCount, tileCollisions,
        tmp.polygons, TILED_MAX_POLYGONS,
        (float)tmp.tileWidth, (float)tmp.tileHeight, tmp.scale, tmp.offset);
    if ((tmp.boxCount < 0) || (tmp.polygonCount < 0))
    {
        tmp.loaded = true;
        UnloadLevelTilesets(&tmp);
        return false;
    }
    if ((tmp.boxCount == 0) && (tmp.polygonCount == 0))
    {
        TraceLog(LOG_ERROR, "TILED: terrain layer has no tiles with TSX collision objects");
        tmp.loaded = true;
        UnloadLevelTilesets(&tmp);
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

    // Canvas coords for spawn and all authored zones
    Vector2 spawn = { tmp.offset.x + spawnMap.x * tmp.scale, tmp.offset.y + spawnMap.y * tmp.scale };
    tmp.finishLine = finishZones[0];
    MapZonesToCanvas(&tmp.finishLine, 1, tmp.scale, tmp.offset);
    MapZonesToCanvas(tmp.noBuild, tmp.noBuildCount, tmp.scale, tmp.offset);
    MapZonesToCanvas(tmp.pits, tmp.pitCount, tmp.scale, tmp.offset);
    MapZonesToCanvas(tmp.boosts, tmp.boostCount, tmp.scale, tmp.offset);
    MapGravityZonesToCanvas(tmp.antiGravity, tmp.antiGravityCount, tmp.scale, tmp.offset);

    // Commit: replace previous state (textures already live in tmp.tilesets)
    UnloadLevelTilesets(lvl);
    *lvl = tmp;
    lvl->modTime = GetFileModTime(tmxPath);
    lvl->loaded = true;
    snprintf(lvl->name, sizeof(lvl->name), "Tiled: %s", GetFileNameWithoutExt(tmxPath));

    lvl->def = (LevelDef){
        .name = lvl->name,
        .ballSpawn = spawn,
        .ballRadius = 18.0f,
        .lineCapacity = lvl->lineCapacity,
        .boostLineCapacity = lvl->boostLineCapacity,
        .cannonCount = lvl->cannonCount,
        .finishLine = lvl->finishLine,
        .boxes = lvl->boxes,
        .boxCount = lvl->boxCount,
        .polygons = lvl->polygons,
        .polygonCount = lvl->polygonCount,
        .pits = lvl->pits,
        .pitCount = lvl->pitCount,
        .boosts = lvl->boosts,
        .boostCount = lvl->boostCount,
        .antiGravity = lvl->antiGravity,
        .antiGravityCount = lvl->antiGravityCount,
    };

    int animTiles = 0;
    for (int i = 0; i < lvl->tilesetCount; i++) animTiles += lvl->tilesets[i].animCount;
    TraceLog(LOG_INFO, "TILED: loaded %s (%dx%d tiles, %d tilesets, %d animated, %d boxes, %d polygons, %d no-build, %d pits, %d boosts, %d anti-gravity, ink %.0f/%.0f px, %d cannons)",
             tmxPath, lvl->mapWidth, lvl->mapHeight, lvl->tilesetCount, animTiles, lvl->boxCount,
             lvl->polygonCount, lvl->noBuildCount, lvl->pitCount, lvl->boostCount, lvl->antiGravityCount,
             lvl->lineCapacity, lvl->boostLineCapacity, lvl->cannonCount);
    return true;
}

void TiledLevelUnload(TiledLevel *lvl)
{
    UnloadLevelTilesets(lvl);
    lvl->loaded = false;
}

bool TiledLevelFileChanged(const TiledLevel *lvl)
{
    if (!lvl->loaded) return false;
    if (GetFileModTime(lvl->tmxPath) != lvl->modTime) return true;
    for (int i = 0; i < lvl->tilesetCount; i++)
    {
        if (GetFileModTime(lvl->tilesets[i].tsxPath) != lvl->tilesets[i].modTime) return true;
    }
    return false;
}

bool TiledLevelNoBuildContains(const TiledLevel *lvl, Vector2 p)
{
    if (!lvl->loaded) return false;
    for (int z = 0; z < lvl->noBuildCount; z++)
    {
        if (PolyZoneContains(&lvl->noBuild[z], p)) return true;
    }
    return false;
}

static void DrawZones(const PolyZone *zones, int count, const char *label, Color base)
{
    Color fill = base;
    fill.a = 28; // faint wash; outline carries the true shape
    Color outline = base;
    outline.a = 110;

    for (int z = 0; z < count; z++)
    {
        const PolyZone *zone = &zones[z];

        // Fan fill (fine for the convex-ish zones Tiled produces)
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
        int tw = MeasureText(label, 14);
        DrawText(label, (int)(c.x - (float)tw * 0.5f), (int)c.y - 7, 14, outline);
    }
}

// Unit vector for gravity after a clockwise rotation from default down (screen Y-down).
static Vector2 GravityDirFromAngle(float angleDeg)
{
    float rad = angleDeg * DEG2RAD;
    return (Vector2){ -sinf(rad), cosf(rad) };
}

static void DrawGravityArrow(Vector2 center, Vector2 dir, float length, Color color)
{
    Vector2 tip = { center.x + dir.x * length * 0.5f, center.y + dir.y * length * 0.5f };
    Vector2 tail = { center.x - dir.x * length * 0.5f, center.y - dir.y * length * 0.5f };
    DrawLineEx(tail, tip, 2.5f, color);

    float head = length * 0.35f;
    Vector2 back = { tip.x - dir.x * head, tip.y - dir.y * head };
    Vector2 normal = { -dir.y * head * 0.55f, dir.x * head * 0.55f };
    DrawLineEx((Vector2){ back.x + normal.x, back.y + normal.y }, tip, 2.5f, color);
    DrawLineEx((Vector2){ back.x - normal.x, back.y - normal.y }, tip, 2.5f, color);
}

static void DrawAntiGravityZones(const GravityZone *zones, int count)
{
    Color base = { 40, 170, 130, 255 };
    Color fill = base;
    fill.a = 32;
    Color outline = base;
    outline.a = 130;
    Color arrow = base;
    arrow.a = 200;

    for (int z = 0; z < count; z++)
    {
        const GravityZone *gz = &zones[z];
        const PolyZone *zone = &gz->zone;

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

        Vector2 centroid = { 0 };
        float minX = zone->points[0].x, maxX = zone->points[0].x;
        float minY = zone->points[0].y, maxY = zone->points[0].y;
        for (int i = 0; i < zone->pointCount; i++)
        {
            centroid.x += zone->points[i].x;
            centroid.y += zone->points[i].y;
            if (zone->points[i].x < minX) minX = zone->points[i].x;
            if (zone->points[i].x > maxX) maxX = zone->points[i].x;
            if (zone->points[i].y < minY) minY = zone->points[i].y;
            if (zone->points[i].y > maxY) maxY = zone->points[i].y;
        }
        centroid.x /= (float)zone->pointCount;
        centroid.y /= (float)zone->pointCount;

        Vector2 dir = GravityDirFromAngle(gz->gravityAngleDeg);
        const float spacing = 36.0f;
        for (float y = minY + spacing * 0.5f; y <= maxY; y += spacing)
        {
            for (float x = minX + spacing * 0.5f; x <= maxX; x += spacing)
            {
                Vector2 p = { x, y };
                if (!PolyZoneContains(zone, p)) continue;
                DrawGravityArrow(p, dir, 18.0f, arrow);
            }
        }

        // Stronger center arrow so the force direction reads immediately
        DrawGravityArrow(centroid, dir, 28.0f, outline);
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
    for (int i = 0; i < lvl->polygonCount; i++)
    {
        const StaticPolygon *polygon = &lvl->polygons[i];
        for (int p = 1; p < polygon->pointCount - 1; p++)
        {
            DrawTriangle(polygon->points[0], polygon->points[p + 1], polygon->points[p],
                         (Color){ 120, 80, 50, 36 });
            DrawTriangle(polygon->points[0], polygon->points[p], polygon->points[p + 1],
                         (Color){ 120, 80, 50, 36 });
        }
    }

    double now = GetTime();
    for (int y = 0; y < lvl->mapHeight; y++)
    {
        for (int x = 0; x < lvl->mapWidth; x++)
        {
            int gid = lvl->terrainGids[y * lvl->mapWidth + x];
            int tsIndex = -1;
            int localId = -1;
            // Unknown GIDs (e.g. skipped automap tilesets) are left blank.
            if (!ResolveGid(lvl->tilesets, lvl->tilesetCount, gid, &tsIndex, &localId)) continue;

            const TiledTileset *ts = &lvl->tilesets[tsIndex];
            if (ts->texture.id == 0) continue;

            int drawId = ResolveAnimatedLocalId(ts, localId, now);
            Rectangle src = {
                (float)(drawId % ts->columns) * tw,
                (float)(drawId / ts->columns) * th,
                tw, th
            };
            Rectangle dest = {
                lvl->offset.x + (float)x * tw * lvl->scale,
                lvl->offset.y + (float)y * th * lvl->scale,
                tw * lvl->scale, th * lvl->scale
            };
            DrawTexturePro(ts->texture, src, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    DrawZones(lvl->noBuild, lvl->noBuildCount, "no build", (Color){ 210, 50, 50, 255 });
    DrawZones(lvl->pits, lvl->pitCount, "pit", (Color){ 70, 50, 40, 255 });
    DrawZones(lvl->boosts, lvl->boostCount, "boost", (Color){ 40, 160, 220, 255 });
    DrawAntiGravityZones(lvl->antiGravity, lvl->antiGravityCount);
}
