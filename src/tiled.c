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

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Filled by RenderCelestialCycle for the lighting pass in game.c.
static CelestialFrame gCelestialFrame = { 0 };

#define WALL_THICKNESS 24.0f
#define TILED_MAX_TILE_TYPES 2048 // atlas tile ids (nature-sprites-2-medium is 1408)
#define TILED_MAX_SHAPES_PER_TILE 4
#define CELESTIAL_PERIOD_SEC 48.0f // full day+night loop along sun-track
#define CELESTIAL_SUN_TIME_FRAC (3.0f / 4.0f) // sun arc lasts 3× the moon arc
#define CELESTIAL_CLOUD_COUNT 7   // concurrent drifting clouds on sun-track maps

// Tiled global-tile-id flip flags (top 4 bits). See docs.mapeditor.org global-tile-ids.
#define TILED_FLIPPED_HORIZONTALLY 0x80000000u
#define TILED_FLIPPED_VERTICALLY   0x40000000u
#define TILED_FLIPPED_DIAGONALLY   0x20000000u
#define TILED_FLIPPED_HEX_120      0x10000000u
#define TILED_GID_MASK             0x0FFFFFFFu

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
        // GIDs are unsigned 32-bit (flip flags in the high bits). Accept digits only;
        // Tiled CSV writes the full unsigned value, never a leading '-'.
        while ((p < end) && ((*p < '0') || (*p > '9'))) p++;
        if (p >= end) break;
        char *next = NULL;
        out[count++] = (int)strtoul(p, &next, 10);
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
// GID helpers (flip flags live in the high bits of the 32-bit global tile id)
//----------------------------------------------------------------------------------
typedef struct TiledGidFlags
{
    bool flipH;
    bool flipV;
    bool flipD;
} TiledGidFlags;

static TiledGidFlags DecodeGidFlags(int gid)
{
    unsigned int raw = (unsigned int)gid;
    return (TiledGidFlags){
        .flipH = (raw & TILED_FLIPPED_HORIZONTALLY) != 0,
        .flipV = (raw & TILED_FLIPPED_VERTICALLY) != 0,
        .flipD = (raw & TILED_FLIPPED_DIAGONALLY) != 0
    };
}

// Apply Tiled orthogonal flips in documented order: diagonal (axis swap), then H, then V.
static Vector2 TransformTileLocalPoint(Vector2 p, float tileW, float tileH, TiledGidFlags flags)
{
    float x = p.x;
    float y = p.y;
    if (flags.flipD)
    {
        float tmp = x;
        x = y;
        y = tmp;
    }
    if (flags.flipH) x = tileW - x;
    if (flags.flipV) y = tileH - y;
    return (Vector2){ x, y };
}

// Odd number of reflections reverses winding — Box2D wants a consistent order.
static bool GidFlagsReverseWinding(TiledGidFlags flags)
{
    int reflections = (flags.flipD ? 1 : 0) + (flags.flipH ? 1 : 0) + (flags.flipV ? 1 : 0);
    return (reflections % 2) != 0;
}

// Draw a terrain tile with Tiled H/V/D flags. Diagonal is remapped the same way
// Tiled's CellRenderer does: 90° rotation, then H' = V, V' = !H.
static void DrawTiledTile(Texture2D tex, Rectangle src, Rectangle dest, TiledGidFlags flags)
{
    float rotation = 0.0f;
    bool flipH = flags.flipH;
    bool flipV = flags.flipV;

    if (flags.flipD)
    {
        rotation = 90.0f;
        bool oldH = flipH;
        flipH = flipV;
        flipV = !oldH;
    }

    if (flipH) src.width = -src.width;
    if (flipV) src.height = -src.height;

    Vector2 origin = { dest.width * 0.5f, dest.height * 0.5f };
    Rectangle centered = {
        dest.x + dest.width * 0.5f,
        dest.y + dest.height * 0.5f,
        dest.width,
        dest.height
    };
    DrawTexturePro(tex, src, centered, origin, rotation, WHITE);
}

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

            TiledGidFlags flags = DecodeGidFlags(gid);
            bool reverseWinding = GidFlagsReverseWinding(flags);

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
                    int srcIndex = reverseWinding ? (source->pointCount - 1 - p) : p;
                    Vector2 local = TransformTileLocalPoint(source->points[srcIndex],
                                                            tileW, tileH, flags);
                    dest->points[p] = (Vector2){
                        offset.x + ((float)x * tileW + local.x) * scale,
                        offset.y + ((float)y * tileH + local.y) * scale
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
// Strip Tiled flip flags before resolving against firstgid ranges.
// GIDs with the horizontal flip bit set are negative when stored in a signed int —
// never reject those with `gid <= 0`; only the tile-id nibble being zero means empty.
static bool ResolveGid(const TiledTileset *tilesets, int tilesetCount, int gid,
                       int *outTsIndex, int *outLocalId)
{
    unsigned int tileId = (unsigned int)gid & TILED_GID_MASK;
    if (tileId == 0) return false;
    for (int i = 0; i < tilesetCount; i++)
    {
        int localId = (int)tileId - tilesets[i].firstGid;
        if ((localId >= 0) && (localId < tilesets[i].tileCount))
        {
            *outTsIndex = i;
            *outLocalId = localId;
            return true;
        }
    }
    return false;
}

// Resolve a tileset local id to the texture + source rect used for drawing.
static bool TilesetTileSource(const TiledTileset *ts, int localId, int tileW, int tileH,
                              Texture2D *outTex, Rectangle *outSrc)
{
    if ((localId < 0) || (localId >= ts->tileCount)) return false;

    if (ts->imageCollection)
    {
        if (localId >= TILED_MAX_TILE_IMAGES) return false;
        const TiledTileImage *img = &ts->tileImages[localId];
        if (img->texture.id == 0) return false;
        *outTex = img->texture;
        *outSrc = (Rectangle){ 0, 0, (float)img->width, (float)img->height };
        return true;
    }

    if ((ts->texture.id == 0) || (ts->columns <= 0)) return false;
    *outTex = ts->texture;
    *outSrc = (Rectangle){
        (float)(localId % ts->columns) * (float)tileW,
        (float)(localId / ts->columns) * (float)tileH,
        (float)tileW, (float)tileH
    };
    return true;
}

// Gameplay object names must never be treated as decorative tile images.
static bool IsGameplayObjectName(const char *name)
{
    if ((name == NULL) || (name[0] == '\0')) return false;
    return (strcmp(name, "ball-spawn") == 0)
        || (strcmp(name, "ball") == 0)
        || (strcmp(name, "finish-line") == 0)
        || (strcmp(name, "no-build") == 0)
        || (strcmp(name, "pit") == 0)
        || (strcmp(name, "boost") == 0)
        || (strcmp(name, "anti-gravity") == 0)
        || (strcmp(name, "sun-track") == 0)
        || (strcmp(name, "clouds") == 0);
}

static bool ParseQuotedAttr(const char *tag, const char *attr, char *out, size_t outSize)
{
    const char *end = strchr(tag, '>');
    const char *p = strstr(tag, attr);
    if ((p == NULL) || ((end != NULL) && (p > end))) return false;
    p += strlen(attr);
    const char *q = strchr(p, '"');
    if ((q == NULL) || ((size_t)(q - p) >= outSize)) return false;
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = '\0';
    return true;
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
            && (ts->columns >= 0)
            && (ts->tileCount > 0);

        if (ok && (ts->tileCount > TILED_MAX_TILE_TYPES))
        {
            TraceLog(LOG_ERROR, "TILED: tileset %s has %d tiles (max %d)",
                     ts->tsxPath, ts->tileCount, TILED_MAX_TILE_TYPES);
            ok = false;
        }

        ts->imageCollection = (ts->columns == 0);
        if (ok && ts->imageCollection && (ts->tileCount > TILED_MAX_TILE_IMAGES))
        {
            TraceLog(LOG_ERROR, "TILED: image-collection tileset %s has %d tiles (max %d)",
                     ts->tsxPath, ts->tileCount, TILED_MAX_TILE_IMAGES);
            ok = false;
        }

        // Collection tilesets are decoration-only — clear the collision slot so a
        // stale static entry from a previous load can never leak into terrain.
        memset(collisions[lvl->tilesetCount], 0, sizeof(collisions[0]));
        if (ok && !ts->imageCollection)
        {
            ok = ParseTilesetCollisions(tsx, collisions[lvl->tilesetCount], TILED_MAX_TILE_TYPES);
            if (ok) ok = ParseTilesetAnimations(tsx, ts);
        }

        if (ok && ts->imageCollection)
        {
            // Each <tile id> carries its own <image source="...">.
            const char *tileCursor = tsx;
            while (ok && ((tileCursor = strstr(tileCursor, "<tile ")) != NULL))
            {
                int tileId = -1;
                if (!ParseIntAttr(tileCursor, " id=\"", &tileId)
                    || (tileId < 0) || (tileId >= ts->tileCount))
                {
                    TraceLog(LOG_ERROR, "TILED: image-collection tile has invalid id in %s",
                             ts->tsxPath);
                    ok = false;
                    break;
                }

                const char *tileEnd = strstr(tileCursor, "</tile>");
                const char *img = strstr(tileCursor, "<image source=\"");
                if ((tileEnd == NULL) || (img == NULL) || (img >= tileEnd))
                {
                    // Self-closing tile with no image — skip (empty slot)
                    tileCursor += strlen("<tile ");
                    continue;
                }

                char imageName[128] = { 0 };
                int imgW = 0, imgH = 0;
                if (!ParseQuotedAttr(img, "source=\"", imageName, sizeof(imageName))
                    || !ParseIntAttr(img, " width=\"", &imgW)
                    || !ParseIntAttr(img, " height=\"", &imgH)
                    || (imgW <= 0) || (imgH <= 0))
                {
                    TraceLog(LOG_ERROR, "TILED: malformed <image> on tile %d in %s",
                             tileId, ts->tsxPath);
                    ok = false;
                    break;
                }

                char pngPath[512];
                snprintf(pngPath, sizeof(pngPath), "%s/%s", GetDirectoryPath(ts->tsxPath), imageName);
                TiledTileImage *slot = &ts->tileImages[tileId];
                slot->width = imgW;
                slot->height = imgH;
                if (IsWindowReady())
                {
                    slot->texture = LoadTexture(pngPath);
                    if (slot->texture.id == 0)
                    {
                        TraceLog(LOG_ERROR, "TILED: failed to load tile image %s", pngPath);
                        ok = false;
                        break;
                    }
                }

                tileCursor = tileEnd + strlen("</tile>");
            }
        }
        else if (ok)
        {
            char imageName[128] = { 0 };
            const char *img = strstr(tsx, "<image source=\"");
            if ((img == NULL) || !ParseQuotedAttr(img, "source=\"", imageName, sizeof(imageName)))
            {
                TraceLog(LOG_ERROR, "TILED: tileset %s has no atlas <image source>", ts->tsxPath);
                ok = false;
            }
            else
            {
                char pngPath[512];
                snprintf(pngPath, sizeof(pngPath), "%s/%s", GetDirectoryPath(ts->tsxPath), imageName);
                if (IsWindowReady())
                {
                    ts->texture = LoadTexture(pngPath);
                    if (ts->texture.id == 0)
                    {
                        TraceLog(LOG_ERROR, "TILED: failed to load tileset image %s", pngPath);
                        ok = false;
                    }
                }
            }
        }

        UnloadFileText(tsx);
        if (!ok)
        {
            TraceLog(LOG_ERROR, "TILED: failed to parse %s", ts->tsxPath);
            // Partial tile-image loads are cleaned up by the caller via UnloadLevelTilesets
            lvl->tilesetCount++; // include this slot so unload walks it
            return false;
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
        TiledTileset *ts = &lvl->tilesets[i];
        if (ts->texture.id != 0) UnloadTexture(ts->texture);
        ts->texture = (Texture2D){ 0 };
        if (ts->imageCollection)
        {
            for (int t = 0; t < ts->tileCount && t < TILED_MAX_TILE_IMAGES; t++)
            {
                if (ts->tileImages[t].texture.id != 0) UnloadTexture(ts->tileImages[t].texture);
                ts->tileImages[t].texture = (Texture2D){ 0 };
            }
        }
    }
}

static void UnloadLevelDecors(TiledLevel *lvl)
{
    for (int i = 0; i < lvl->decorCount; i++)
    {
        if (lvl->decors[i].ownsTexture && (lvl->decors[i].texture.id != 0))
        {
            UnloadTexture(lvl->decors[i].texture);
        }
        lvl->decors[i].texture = (Texture2D){ 0 };
        lvl->decors[i].ownsTexture = false;
    }
    lvl->decorCount = 0;
}

static void UnloadCelestial(TiledLevel *lvl)
{
    if (lvl->sunTex.id != 0) UnloadTexture(lvl->sunTex);
    if (lvl->moonTex.id != 0) UnloadTexture(lvl->moonTex);
    lvl->sunTex = (Texture2D){ 0 };
    lvl->moonTex = (Texture2D){ 0 };
    for (int i = 0; i < TILED_CLOUD_TEX_COUNT; i++)
    {
        if (lvl->cloudTex[i].id != 0) UnloadTexture(lvl->cloudTex[i]);
        lvl->cloudTex[i] = (Texture2D){ 0 };
    }
    lvl->hasSunTrack = false;
    lvl->sunTrack = (PolyZone){ 0 };
    lvl->hasCloudBand = false;
    lvl->cloudBand = (PolyZone){ 0 };
}

static void FreeLevelGids(TiledLevel *lvl)
{
    free(lvl->terrainGids);
    lvl->terrainGids = NULL;
    for (int i = 0; i < TILED_MAX_VIS_LAYERS; i++)
    {
        free(lvl->visGids[i]);
        lvl->visGids[i] = NULL;
    }
    lvl->visLayerCount = 0;
}

static int *AllocGidLayer(int tileCount)
{
    assert(tileCount > 0);
    int *gids = (int *)calloc((size_t)tileCount, sizeof(int));
    if (gids == NULL)
    {
        TraceLog(LOG_ERROR, "TILED: out of memory allocating %d tile GIDs", tileCount);
    }
    return gids;
}

// Optional visual tile layers: any "terrain-*" (not the collision "terrain") or "sprites".
static bool LoadVisualTileLayers(const char *xml, TiledLevel *tmp, int tileCount)
{
    tmp->visLayerCount = 0;
    const char *p = xml;
    while ((p = strstr(p, "<layer")) != NULL)
    {
        const char *tagEnd = strchr(p, '>');
        if (tagEnd == NULL) break;

        char name[32] = { 0 };
        if (!ParseQuotedAttr(p, "name=\"", name, sizeof(name)))
        {
            p = tagEnd + 1;
            continue;
        }

        bool isTerrainFamily = (strncmp(name, "terrain", 7) == 0);
        bool isSprites = (strcmp(name, "sprites") == 0);
        if ((!isTerrainFamily && !isSprites) || (strcmp(name, "terrain") == 0))
        {
            p = tagEnd + 1;
            continue;
        }

        if (tmp->visLayerCount >= TILED_MAX_VIS_LAYERS)
        {
            TraceLog(LOG_ERROR, "TILED: more than %d visual tile layers", TILED_MAX_VIS_LAYERS);
            return false;
        }

        int slot = tmp->visLayerCount;
        tmp->visGids[slot] = AllocGidLayer(tileCount);
        if (tmp->visGids[slot] == NULL) return false;
        if (!ParseTileLayer(xml, name, tmp->visGids[slot], tileCount)) return false;
        snprintf(tmp->visLayerNames[slot], sizeof(tmp->visLayerNames[slot]), "%s", name);
        tmp->visLayerCount++;
        p = tagEnd + 1;
    }
    return true;
}

// resources/<act>/map.tmx → resources/spritesheet/isolated/<name>.png
static bool LoadCelestialTexture(const char *mapDir, const char *name, Texture2D *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/../spritesheet/isolated/%s.png", mapDir, name);
    if (!IsWindowReady())
    {
        *out = (Texture2D){ 0 };
        return true; // parse-only / headless — textures load once the window exists
    }
    *out = LoadTexture(path);
    if (out->id == 0)
    {
        TraceLog(LOG_ERROR, "TILED: sun-track present but failed to load %s", path);
        return false;
    }
    return true;
}

static bool PushDecor(TiledLevel *lvl, Texture2D texture, bool ownsTexture, bool aboveTerrain,
                      Rectangle src, Rectangle dest, float parallaxX, float parallaxY,
                      float opacity)
{
    if (lvl->decorCount >= TILED_MAX_DECORS)
    {
        TraceLog(LOG_ERROR, "TILED: more than %d background images/objects", TILED_MAX_DECORS);
        if (ownsTexture && (texture.id != 0)) UnloadTexture(texture);
        return false;
    }
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    lvl->decors[lvl->decorCount++] = (TiledDecor){
        .texture = texture,
        .ownsTexture = ownsTexture,
        .aboveTerrain = aboveTerrain,
        .src = src,
        .dest = dest,
        .parallaxX = parallaxX,
        .parallaxY = parallaxY,
        .opacity = opacity,
    };
    return true;
}

static void DrawDecorsPass(const TiledLevel *lvl, Vector2 viewPan, bool aboveTerrain)
{
    for (int i = 0; i < lvl->decorCount; i++)
    {
        const TiledDecor *d = &lvl->decors[i];
        if (d->aboveTerrain != aboveTerrain) continue;
        if (d->texture.id == 0) continue;
        Rectangle dest = d->dest;
        dest.x += viewPan.x * (1.0f - d->parallaxX);
        dest.y += viewPan.y * (1.0f - d->parallaxY);
        Color tint = WHITE;
        tint.a = (unsigned char)(d->opacity * 255.0f + 0.5f);
        DrawTexturePro(d->texture, d->src, dest, (Vector2){ 0, 0 }, 0.0f, tint);
    }
}

// Tile objects (gid=) and imagelayers become decorative images.
// Object layer "art" draws above terrain; everything else draws behind.
// Layer parallaxx/parallaxy from Tiled are preserved (default 1,1).
static bool ParseBackgroundDecors(const char *xml, const char *mapDir, TiledLevel *lvl)
{
    lvl->decorCount = 0;
    const char *p = xml;

    while ((p = strstr(p, "<")) != NULL)
    {
        if (strncmp(p, "<objectgroup ", 13) == 0)
        {
            float parallaxX = 1.0f, parallaxY = 1.0f;
            float layerOffX = 0.0f, layerOffY = 0.0f;
            float layerOpacity = 1.0f;
            char layerName[64] = { 0 };
            ParseFloatAttr(p, " parallaxx=\"", &parallaxX);
            ParseFloatAttr(p, " parallaxy=\"", &parallaxY);
            ParseFloatAttr(p, " offsetx=\"", &layerOffX);
            ParseFloatAttr(p, " offsety=\"", &layerOffY);
            ParseFloatAttr(p, " opacity=\"", &layerOpacity);
            ParseQuotedAttr(p, "name=\"", layerName, sizeof(layerName));
            bool aboveTerrain = (strcmp(layerName, "art") == 0);

            const char *groupEnd = strstr(p, "</objectgroup>");
            if (groupEnd == NULL)
            {
                TraceLog(LOG_ERROR, "TILED: unclosed <objectgroup>");
                return false;
            }

            const char *obj = p;
            while ((obj = strstr(obj, "<object ")) != NULL && obj < groupEnd)
            {
                const char *objEnd = ObjectEnd(obj);
                if (objEnd == NULL || objEnd > groupEnd)
                {
                    TraceLog(LOG_ERROR, "TILED: malformed <object> inside objectgroup");
                    return false;
                }

                // Only tile objects (gid) are decorative images.
                // Flip-flagged GIDs are negative as signed ints — empty is tile-id 0.
                int gid = 0;
                if (!ParseIntAttr(obj, " gid=\"", &gid)
                    || (((unsigned int)gid & TILED_GID_MASK) == 0))
                {
                    obj = objEnd;
                    continue;
                }

                char name[64] = { 0 };
                ParseQuotedAttr(obj, "name=\"", name, sizeof(name));
                if (IsGameplayObjectName(name))
                {
                    obj = objEnd;
                    continue;
                }

                float ox = 0.0f, oy = 0.0f, ow = 0.0f, oh = 0.0f;
                float objOpacity = 1.0f;
                if (!ParseFloatAttr(obj, " x=\"", &ox) || !ParseFloatAttr(obj, " y=\"", &oy)
                    || !ParseFloatAttr(obj, " width=\"", &ow) || !ParseFloatAttr(obj, " height=\"", &oh)
                    || (ow <= 0.0f) || (oh <= 0.0f))
                {
                    TraceLog(LOG_ERROR, "TILED: tile object missing x/y/width/height");
                    return false;
                }
                ParseFloatAttr(obj, " opacity=\"", &objOpacity);

                int tsIndex = -1, localId = -1;
                if (!ResolveGid(lvl->tilesets, lvl->tilesetCount, gid, &tsIndex, &localId))
                {
                    TraceLog(LOG_ERROR, "TILED: tile object gid %d matches no tileset", gid);
                    return false;
                }

                Texture2D tex = { 0 };
                Rectangle src = { 0 };
                if (!TilesetTileSource(&lvl->tilesets[tsIndex], localId,
                                       lvl->tileWidth, lvl->tileHeight, &tex, &src))
                {
                    TraceLog(LOG_ERROR, "TILED: tile object gid %d has no drawable image", gid);
                    return false;
                }

                // Tiled tile-object origin is bottom-left — convert to top-left dest.
                Rectangle dest = {
                    lvl->offset.x + (layerOffX + ox) * lvl->scale,
                    lvl->offset.y + (layerOffY + oy - oh) * lvl->scale,
                    ow * lvl->scale,
                    oh * lvl->scale
                };
                if (!PushDecor(lvl, tex, false, aboveTerrain, src, dest, parallaxX, parallaxY,
                               layerOpacity * objOpacity)) return false;
                obj = objEnd;
            }

            p = groupEnd + strlen("</objectgroup>");
            continue;
        }

        if (strncmp(p, "<imagelayer ", 12) == 0)
        {
            float parallaxX = 1.0f, parallaxY = 1.0f;
            float layerOffX = 0.0f, layerOffY = 0.0f;
            float layerOpacity = 1.0f;
            ParseFloatAttr(p, " parallaxx=\"", &parallaxX);
            ParseFloatAttr(p, " parallaxy=\"", &parallaxY);
            ParseFloatAttr(p, " offsetx=\"", &layerOffX);
            ParseFloatAttr(p, " offsety=\"", &layerOffY);
            ParseFloatAttr(p, " opacity=\"", &layerOpacity);

            const char *layerEnd = strstr(p, "</imagelayer>");
            // Empty imagelayers may be self-closing: <imagelayer .../>
            const char *tagEnd = strchr(p, '>');
            bool selfClose = (tagEnd != NULL) && (tagEnd > p) && (*(tagEnd - 1) == '/');

            if (selfClose)
            {
                // Empty image layer (e.g. act-1 map-3 "test") — ignore quietly.
                p = tagEnd + 1;
                continue;
            }
            if (layerEnd == NULL)
            {
                TraceLog(LOG_ERROR, "TILED: unclosed <imagelayer>");
                return false;
            }

            const char *img = strstr(p, "<image source=\"");
            if ((img == NULL) || (img >= layerEnd))
            {
                // Layer present but no image yet — ignore (authoring in progress)
                p = layerEnd + strlen("</imagelayer>");
                continue;
            }

            char imageName[128] = { 0 };
            int imgW = 0, imgH = 0;
            if (!ParseQuotedAttr(img, "source=\"", imageName, sizeof(imageName))
                || !ParseIntAttr(img, " width=\"", &imgW)
                || !ParseIntAttr(img, " height=\"", &imgH)
                || (imgW <= 0) || (imgH <= 0))
            {
                TraceLog(LOG_ERROR, "TILED: malformed imagelayer <image>");
                return false;
            }

            char pngPath[512];
            snprintf(pngPath, sizeof(pngPath), "%s/%s", mapDir, imageName);

            Texture2D tex = { 0 };
            if (IsWindowReady())
            {
                tex = LoadTexture(pngPath);
                if (tex.id == 0)
                {
                    TraceLog(LOG_ERROR, "TILED: failed to load imagelayer %s", pngPath);
                    return false;
                }
            }

            Rectangle src = { 0, 0, (float)imgW, (float)imgH };
            Rectangle dest = {
                lvl->offset.x + layerOffX * lvl->scale,
                lvl->offset.y + layerOffY * lvl->scale,
                (float)imgW * lvl->scale,
                (float)imgH * lvl->scale
            };
            if (!PushDecor(lvl, tex, true, false, src, dest, parallaxX, parallaxY, layerOpacity)) return false;

            p = layerEnd + strlen("</imagelayer>");
            continue;
        }

        p++;
    }

    return true;
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
    if (ok)
    {
        tmp.terrainGids = AllocGidLayer(tileCount);
        ok = (tmp.terrainGids != NULL);
    }
    ok = ok && ParseTileLayer(xml, "terrain", tmp.terrainGids, tileCount);
    ok = ok && LoadVisualTileLayers(xml, &tmp, tileCount);

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

        // Optional sun-track: at most one closed sky path for the day/night pilot.
        if (ok)
        {
            PolyZone sunTracks[1] = { 0 };
            int sunTrackCount = ParseZones(xml, "sun-track", sunTracks, 1);
            if (sunTrackCount < 0)
            {
                ok = false;
            }
            else if (sunTrackCount == 1)
            {
                tmp.hasSunTrack = true;
                tmp.sunTrack = sunTracks[0];
            }
        }

        // Optional cloud spawn band (rect/polygon named "clouds").
        if (ok)
        {
            PolyZone cloudBands[1] = { 0 };
            int cloudBandCount = ParseZones(xml, "clouds", cloudBands, 1);
            if (cloudBandCount < 0)
            {
                ok = false;
            }
            else if (cloudBandCount == 1)
            {
                tmp.hasCloudBand = true;
                tmp.cloudBand = cloudBands[0];
            }
        }
    }

    // External tilesets listed on the map (supports multi-tileset maps like map-11).
    // Keep xml alive until backgrounds are parsed (tile objects + imagelayers).
    // Copy the map dir: raylib's GetDirectoryPath returns a reused static buffer.
    char mapDir[512];
    snprintf(mapDir, sizeof(mapDir), "%s", GetDirectoryPath(tmxPath));
    static TileCollision tileCollisions[TILED_MAX_TILESETS][TILED_MAX_TILE_TYPES];
    if (ok) ok = LoadMapTilesets(xml, mapDir, &tmp, tileCollisions);

    // 1:1 with Tiled — map pixels are canvas pixels (no fit-to-screen shrink).
    // Offset centers the map on the design canvas; large maps extend past the
    // view and are navigated with WASD pan / +/- zoom.
    float mapPxW = (float)(tmp.mapWidth * tmp.tileWidth);
    float mapPxH = (float)(tmp.mapHeight * tmp.tileHeight);
    if (ok)
    {
        tmp.scale = 1.0f;
        tmp.offset = (Vector2){
            ((float)GAME_SCREEN_WIDTH - mapPxW * tmp.scale) * 0.5f,
            ((float)GAME_SCREEN_HEIGHT - mapPxH * tmp.scale) * 0.5f
        };
        // Ink budgets: authored in tile-widths, spent in canvas pixels
        tmp.lineCapacity = lineCapacityTiles * (float)tmp.tileWidth * tmp.scale;
        tmp.boostLineCapacity = boostLineCapacityTiles * (float)tmp.tileWidth * tmp.scale;
        ok = ParseBackgroundDecors(xml, mapDir, &tmp);
        if (ok && tmp.hasSunTrack)
        {
            ok = LoadCelestialTexture(mapDir, "sun", &tmp.sunTex)
              && LoadCelestialTexture(mapDir, "moon", &tmp.moonTex)
              && LoadCelestialTexture(mapDir, "cloud-1", &tmp.cloudTex[0])
              && LoadCelestialTexture(mapDir, "cloud-2", &tmp.cloudTex[1]);
        }
    }

    UnloadFileText(xml);
    if (!ok)
    {
        // LoadMapTilesets / ParseBackgroundDecors may have loaded textures before failing.
        tmp.loaded = true; // so UnloadLevelTilesets walks the partial list
        FreeLevelGids(&tmp);
        UnloadCelestial(&tmp);
        UnloadLevelDecors(&tmp);
        UnloadLevelTilesets(&tmp);
        return false;
    }

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
        FreeLevelGids(&tmp);
        UnloadCelestial(&tmp);
        UnloadLevelDecors(&tmp);
        UnloadLevelTilesets(&tmp);
        return false;
    }
    if ((tmp.boxCount == 0) && (tmp.polygonCount == 0))
    {
        TraceLog(LOG_ERROR, "TILED: terrain layer has no tiles with TSX collision objects");
        tmp.loaded = true;
        FreeLevelGids(&tmp);
        UnloadCelestial(&tmp);
        UnloadLevelDecors(&tmp);
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
    if (tmp.hasSunTrack) MapZonesToCanvas(&tmp.sunTrack, 1, tmp.scale, tmp.offset);
    if (tmp.hasCloudBand) MapZonesToCanvas(&tmp.cloudBand, 1, tmp.scale, tmp.offset);

    // Commit: replace previous state (textures + GID heaps already live in tmp)
    FreeLevelGids(lvl);
    UnloadCelestial(lvl);
    UnloadLevelDecors(lvl);
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
    TraceLog(LOG_INFO, "TILED: loaded %s (%dx%d tiles, %d tilesets, %d animated, %d boxes, %d polygons, %d vis-layers, %d no-build, %d pits, %d boosts, %d anti-gravity, sun-track %s, ink %.0f/%.0f px, %d cannons)",
             tmxPath, lvl->mapWidth, lvl->mapHeight, lvl->tilesetCount, animTiles, lvl->boxCount,
             lvl->polygonCount, lvl->visLayerCount, lvl->noBuildCount, lvl->pitCount, lvl->boostCount,
             lvl->antiGravityCount, lvl->hasSunTrack ? "yes" : "no",
             lvl->lineCapacity, lvl->boostLineCapacity, lvl->cannonCount);
    return true;
}

void TiledLevelUnload(TiledLevel *lvl)
{
    FreeLevelGids(lvl);
    UnloadCelestial(lvl);
    UnloadLevelDecors(lvl);
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

// Sample a closed polyline by normalized arc-length parameter u in [0, 1).
static Vector2 PolyZoneSampleLoop(const PolyZone *zone, float u)
{
    assert(zone != NULL);
    assert(zone->pointCount >= 2);

    u -= floorf(u); // wrap to [0, 1)

    float total = 0.0f;
    float segLen[POLY_ZONE_MAX_POINTS];
    for (int i = 0; i < zone->pointCount; i++)
    {
        Vector2 a = zone->points[i];
        Vector2 b = zone->points[(i + 1) % zone->pointCount];
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        segLen[i] = sqrtf(dx * dx + dy * dy);
        total += segLen[i];
    }
    assert(total > 0.0f);

    float target = u * total;
    for (int i = 0; i < zone->pointCount; i++)
    {
        bool last = (i == zone->pointCount - 1);
        if ((target <= segLen[i]) || last)
        {
            float t = (segLen[i] > 0.0f) ? (target / segLen[i]) : 0.0f;
            if (t > 1.0f) t = 1.0f;
            Vector2 a = zone->points[i];
            Vector2 b = zone->points[(i + 1) % zone->pointCount];
            return (Vector2){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
        }
        target -= segLen[i];
    }

    return zone->points[0];
}

static void DrawCelestialBody(Texture2D tex, Vector2 center, float alpha)
{
    if ((tex.id == 0) || (alpha <= 0.01f)) return;
    if (alpha > 1.0f) alpha = 1.0f;

    Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
    Rectangle dest = {
        center.x - (float)tex.width * 0.5f,
        center.y - (float)tex.height * 0.5f,
        (float)tex.width,
        (float)tex.height
    };
    Color tint = { 255, 255, 255, (unsigned char)(alpha * 255.0f) };
    DrawTexturePro(tex, src, dest, (Vector2){ 0.0f, 0.0f }, 0.0f, tint);
}

static void CloudBandBounds(const TiledLevel *lvl, float *outLeft, float *outTop,
                            float *outW, float *outH)
{
    const PolyZone *band = NULL;
    if (lvl->hasCloudBand && (lvl->cloudBand.pointCount >= 3))
    {
        band = &lvl->cloudBand;
    }
    else
    {
        band = &lvl->sunTrack;
    }

    float left = band->points[0].x;
    float right = band->points[0].x;
    float top = band->points[0].y;
    float bot = band->points[0].y;
    for (int i = 1; i < band->pointCount; i++)
    {
        float x = band->points[i].x;
        float y = band->points[i].y;
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bot) bot = y;
    }
    *outLeft = left;
    *outTop = top;
    *outW = right - left;
    *outH = bot - top;
}

// Horizontal drifting clouds. tintOverride.a > 0 forces a solid tint (cloud mask).
static void RenderClouds(const TiledLevel *lvl, float night, Color tintOverride)
{
    assert(lvl->hasSunTrack);

    float bandLeft = 0.0f, bandTop = 0.0f, bandW = 0.0f, bandH = 0.0f;
    CloudBandBounds(lvl, &bandLeft, &bandTop, &bandW, &bandH);
    if ((bandW < 1.0f) || (bandH < 1.0f)) return;

    float alpha = 0.92f - night * 0.35f;
    double now = GetTime();
    bool maskPass = (tintOverride.a > 0);

    for (int i = 0; i < CELESTIAL_CLOUD_COUNT; i++)
    {
        const Texture2D *tex = &lvl->cloudTex[i % TILED_CLOUD_TEX_COUNT];
        if (tex->id == 0) continue;

        float speed = 14.0f + (float)((i * 11) % 18);
        float yFrac = (float)((i * 37 + 13) % 100) / 100.0f;
        float scale = 0.85f + (float)((i * 19) % 40) / 100.0f;
        float phase = (float)(i * 173);

        float w = (float)tex->width * scale;
        float h = (float)tex->height * scale;
        float travel = bandW + w * 2.0f;
        float x = bandLeft - w + (float)fmod(now * (double)speed + (double)phase, (double)travel);
        float y = bandTop + bandH * (0.12f + yFrac * 0.76f);

        Rectangle src = { 0.0f, 0.0f, (float)tex->width, (float)tex->height };
        Rectangle dest = { x, y - h * 0.5f, w, h };
        Color tint = maskPass ? tintOverride
                              : (Color){ 255, 255, 255, (unsigned char)(alpha * 255.0f) };
        DrawTexturePro(*tex, src, dest, (Vector2){ 0.0f, 0.0f }, 0.0f, tint);
    }
}

static void RenderCelestialCycle(const TiledLevel *lvl)
{
    gCelestialFrame = (CelestialFrame){ 0 };
    if (!lvl->hasSunTrack || (lvl->sunTrack.pointCount < 3)) return;

    // Time phase 0..1 over the full day+night. Path halves stay equal
    // (day arc = first half of the polygon, night = second), but the sun
    // spends CELESTIAL_SUN_TIME_FRAC of the period on its arc (3× the moon).
    float t = (float)fmod(GetTime() / (double)CELESTIAL_PERIOD_SEC, 1.0);
    if (t < 0.0f) t += 1.0f;

    float pathU = 0.0f;
    float sunAlpha = 0.0f;
    float moonAlpha = 0.0f;
    if (t < CELESTIAL_SUN_TIME_FRAC)
    {
        float local = t / CELESTIAL_SUN_TIME_FRAC; // 0..1 along day arc
        pathU = local * 0.5f;
        sunAlpha = sinf(local * (float)PI); // fade at both horizons
    }
    else
    {
        float local = (t - CELESTIAL_SUN_TIME_FRAC) / (1.0f - CELESTIAL_SUN_TIME_FRAC);
        pathU = 0.5f + local * 0.5f;
        moonAlpha = sinf(local * (float)PI);
    }

    Vector2 pos = PolyZoneSampleLoop(&lvl->sunTrack, pathU);
    float night = 1.0f - sunAlpha;

    gCelestialFrame = (CelestialFrame){
        .active = true,
        .bodyWorld = pos,
        .sunIntensity = sunAlpha,
        .night = night,
    };

    // Soft night wash when the sun is down — keeps gameplay readable.
    if (night > 0.02f)
    {
        float mapPxW = (float)(lvl->mapWidth * lvl->tileWidth) * lvl->scale;
        float mapPxH = (float)(lvl->mapHeight * lvl->tileHeight) * lvl->scale;
        Color wash = { 18, 28, 64, (unsigned char)(night * 55.0f) };
        DrawRectangle((int)lvl->offset.x, (int)lvl->offset.y,
                      (int)mapPxW, (int)mapPxH, wash);
    }

    DrawCelestialBody(lvl->sunTex, pos, sunAlpha);
    DrawCelestialBody(lvl->moonTex, pos, moonAlpha);
    // Visible clouds (mask pass is done separately in game.c — nested RTs would
    // unbind the main view target).
    RenderClouds(lvl, night, (Color){ 0, 0, 0, 0 });
}

void TiledLevelRenderCloudMask(const TiledLevel *lvl)
{
    if (!lvl->loaded || !lvl->hasSunTrack) return;
    RenderClouds(lvl, gCelestialFrame.night, (Color){ 255, 255, 255, 255 });
}

static void DrawTileGidLayer(const TiledLevel *lvl, const int *gids, float tw, float th, double now)
{
    for (int y = 0; y < lvl->mapHeight; y++)
    {
        for (int x = 0; x < lvl->mapWidth; x++)
        {
            int gid = gids[y * lvl->mapWidth + x];
            int tsIndex = -1;
            int localId = -1;
            if (!ResolveGid(lvl->tilesets, lvl->tilesetCount, gid, &tsIndex, &localId)) continue;

            const TiledTileset *ts = &lvl->tilesets[tsIndex];
            int drawId = ResolveAnimatedLocalId(ts, localId, now);
            Texture2D tex = { 0 };
            Rectangle src = { 0 };
            if (!TilesetTileSource(ts, drawId, (int)tw, (int)th, &tex, &src)) continue;

            Rectangle dest = {
                lvl->offset.x + (float)x * tw * lvl->scale,
                lvl->offset.y + (float)y * th * lvl->scale,
                tw * lvl->scale, th * lvl->scale
            };
            DrawTiledTile(tex, src, dest, DecodeGidFlags(gid));
        }
    }
}

void RenderTiledLevel(const TiledLevel *lvl, Vector2 viewPan)
{
    if (!lvl->loaded) return;

    float tw = (float)lvl->tileWidth;
    float th = (float)lvl->tileHeight;

    // Behind-terrain props / imagelayers first.
    // Parallax: Mode2D already subtracts viewPan, so we add pan * (1 - factor)
    // here — net motion is pan * factor (0 = screen-locked).
    DrawDecorsPass(lvl, viewPan, false);

    // Day/night celestial bodies sit in the sky behind terrain and gameplay.
    RenderCelestialCycle(lvl);

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
    DrawTileGidLayer(lvl, lvl->terrainGids, tw, th, now);
    for (int v = 0; v < lvl->visLayerCount; v++)
    {
        DrawTileGidLayer(lvl, lvl->visGids[v], tw, th, now);
    }

    // Object layer "art" — above terrain tiles, below zone overlays / gameplay.
    DrawDecorsPass(lvl, viewPan, true);

    DrawZones(lvl->noBuild, lvl->noBuildCount, "no build", (Color){ 210, 50, 50, 255 });
    DrawZones(lvl->pits, lvl->pitCount, "pit", (Color){ 70, 50, 40, 255 });
    DrawZones(lvl->boosts, lvl->boostCount, "boost", (Color){ 40, 160, 220, 255 });
    DrawAntiGravityZones(lvl->antiGravity, lvl->antiGravityCount);
}

CelestialFrame TiledLevelGetCelestialFrame(void)
{
    return gCelestialFrame;
}
