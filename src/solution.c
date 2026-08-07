/*******************************************************************************************
*
*   solution.c - Saved solutions: capture, text save/load, replay into a physics world
*
********************************************************************************************/

#include "solution.h"

#include "box2d/box2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Same ink color as sketch.c — solutions don't store color (single crayon today)
static const Color SOLUTION_CRAYON = { 40, 90, 200, 255 };

static bool StrokeHasAnyBoost(const SolutionStroke *stroke)
{
    if (!stroke->hasBoostMask || (stroke->pointCount < 2)) return false;
    for (int i = 0; i < stroke->pointCount - 1; i++)
    {
        if (stroke->boostSeg[i]) return true;
    }
    return false;
}

// Encode boost bits as lowercase hex (LSB of first byte = segment 0)
static void FormatBoostMask(const uint8_t *boostSeg, int segCount, char *out, int outSize)
{
    int byteCount = (segCount + 7) / 8;
    if (byteCount * 2 + 1 > outSize)
    {
        out[0] = '\0';
        return;
    }
    for (int b = 0; b < byteCount; b++)
    {
        unsigned int byte = 0;
        for (int bit = 0; bit < 8; bit++)
        {
            int seg = b * 8 + bit;
            if ((seg < segCount) && boostSeg[seg]) byte |= (1u << bit);
        }
        snprintf(out + b * 2, 3, "%02x", byte);
    }
}

static bool ParseBoostMask(const char *hex, uint8_t *boostSeg, int segCount)
{
    memset(boostSeg, 0, (size_t)segCount);
    int hexLen = (int)strlen(hex);
    if ((hexLen <= 0) || ((hexLen % 2) != 0)) return false;

    int byteCount = hexLen / 2;
    for (int b = 0; b < byteCount; b++)
    {
        unsigned int byte = 0;
        if (sscanf(hex + b * 2, "%2x", &byte) != 1) return false;
        for (int bit = 0; bit < 8; bit++)
        {
            int seg = b * 8 + bit;
            if (seg >= segCount) break;
            if (byte & (1u << bit)) boostSeg[seg] = 1;
        }
    }
    return true;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void SolutionCapture(Solution *sol, const PhysicsWorld *phys, const char *levelFile)
{
    memset(sol, 0, sizeof(*sol));
    snprintf(sol->levelFile, sizeof(sol->levelFile), "%s", levelFile);
    sol->tunables = phys->tunables;

    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        const DrawnBody *drawn = &phys->drawn[i];
        if (!drawn->active) continue;
        if (sol->strokeCount >= SOLUTION_MAX_STROKES) break;

        SolutionStroke *stroke = &sol->strokes[sol->strokeCount++];
        b2Transform xf = b2Body_GetTransform(drawn->bodyId);
        stroke->pointCount = drawn->pointCount;
        for (int p = 0; p < drawn->pointCount; p++)
        {
            b2Vec2 world = b2TransformPoint(xf, (b2Vec2){ drawn->localPoints[p].x, drawn->localPoints[p].y });
            stroke->points[p] = (Vector2){ world.x, world.y };
        }
        memcpy(stroke->boostSeg, drawn->boostSeg, sizeof(stroke->boostSeg));
        stroke->hasBoostMask = false;
        for (int s = 0; s < drawn->pointCount - 1; s++)
        {
            if (drawn->boostSeg[s]) { stroke->hasBoostMask = true; break; }
        }
    }

    for (int i = 0; i < MAX_CANNONS; i++)
    {
        const Cannon *cannon = &phys->cannons[i];
        if (!cannon->active) continue;
        if (sol->cannonCount >= SOLUTION_MAX_CANNONS) break;

        sol->cannons[sol->cannonCount++] = (SolutionCannon){ cannon->pos, cannon->angleRad };
    }
}

bool SolutionSave(const Solution *sol, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        fprintf(stderr, "SOLUTION: cannot open %s for writing\n", path);
        return false;
    }

    fprintf(f, "version %d\n", SOLUTION_VERSION);
    fprintf(f, "level %s\n", sol->levelFile);
    fprintf(f, "tunables density=%.4f restitution=%.4f dropforce=%.4f boostrate=%.4f boostmax=%.4f\n",
            sol->tunables.ballDensity, sol->tunables.ballRestitution, sol->tunables.dropForce,
            sol->tunables.boostVelRate, sol->tunables.boostVelMax);

    for (int i = 0; i < sol->strokeCount; i++)
    {
        const SolutionStroke *stroke = &sol->strokes[i];
        fprintf(f, "stroke");
        for (int p = 0; p < stroke->pointCount; p++)
        {
            fprintf(f, " %.4f,%.4f", stroke->points[p].x, stroke->points[p].y);
        }
        if (StrokeHasAnyBoost(stroke))
        {
            char maskHex[MAX_STROKE_SEGS / 4 + 8];
            FormatBoostMask(stroke->boostSeg, stroke->pointCount - 1, maskHex, (int)sizeof(maskHex));
            fprintf(f, " mask %s", maskHex);
        }
        fprintf(f, "\n");
    }

    for (int i = 0; i < sol->cannonCount; i++)
    {
        fprintf(f, "cannon %.4f,%.4f %.6f\n",
                sol->cannons[i].pos.x, sol->cannons[i].pos.y, sol->cannons[i].angleRad);
    }

    fclose(f);
    return true;
}

// Parse "stroke x,y x,y ... [mask HEX]". Returns false on any malformed pair.
static bool ParseStrokeLine(const char *line, SolutionStroke *stroke)
{
    memset(stroke, 0, sizeof(*stroke));
    const char *cursor = line;

    while (*cursor)
    {
        while ((*cursor == ' ') || (*cursor == '\t')) cursor++;
        if ((*cursor == '\0') || (*cursor == '\n') || (*cursor == '\r')) break;

        // Optional trailing mask token
        if (strncmp(cursor, "mask", 4) == 0)
        {
            cursor += 4;
            while ((*cursor == ' ') || (*cursor == '\t')) cursor++;
            char hex[MAX_STROKE_SEGS / 4 + 8];
            int n = 0;
            while (cursor[n] && (cursor[n] != ' ') && (cursor[n] != '\t')
                   && (cursor[n] != '\n') && (cursor[n] != '\r')
                   && (n < (int)sizeof(hex) - 1))
            {
                hex[n] = cursor[n];
                n++;
            }
            hex[n] = '\0';
            if (stroke->pointCount < 2) return false;
            if (!ParseBoostMask(hex, stroke->boostSeg, stroke->pointCount - 1)) return false;
            stroke->hasBoostMask = true;
            break;
        }

        char *end = NULL;
        float x = strtof(cursor, &end);
        if ((end == cursor) || (*end != ',')) return false;
        cursor = end + 1;

        float y = strtof(cursor, &end);
        if (end == cursor) return false;
        cursor = end;

        if (stroke->pointCount >= MAX_STROKE_POINTS) return false;
        stroke->points[stroke->pointCount++] = (Vector2){ x, y };
    }

    return (stroke->pointCount >= 2);
}

bool SolutionLoad(Solution *sol, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "SOLUTION: cannot open %s\n", path);
        return false;
    }

    memset(sol, 0, sizeof(*sol));
    bool haveVersion = false;
    bool haveLevel = false;
    bool haveTunables = false;

    char line[8192];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f))
    {
        lineNo++;
        // Skip blank lines and comments
        char *start = line;
        while ((*start == ' ') || (*start == '\t')) start++;
        if ((*start == '\0') || (*start == '\n') || (*start == '\r') || (*start == '#')) continue;

        if (strncmp(start, "version ", 8) == 0)
        {
            int version = atoi(start + 8);
            if (version != SOLUTION_VERSION)
            {
                fprintf(stderr, "SOLUTION: %s:%d unsupported version %d (expected %d)\n",
                        path, lineNo, version, SOLUTION_VERSION);
                fclose(f);
                return false;
            }
            haveVersion = true;
        }
        else if (strncmp(start, "level ", 6) == 0)
        {
            if (sscanf(start + 6, "%255s", sol->levelFile) != 1)
            {
                fprintf(stderr, "SOLUTION: %s:%d malformed level line\n", path, lineNo);
                fclose(f);
                return false;
            }
            haveLevel = true;
        }
        else if (strncmp(start, "tunables ", 9) == 0)
        {
            if (sscanf(start + 9, "density=%f restitution=%f dropforce=%f boostrate=%f boostmax=%f",
                       &sol->tunables.ballDensity, &sol->tunables.ballRestitution,
                       &sol->tunables.dropForce, &sol->tunables.boostVelRate,
                       &sol->tunables.boostVelMax) != 5)
            {
                fprintf(stderr, "SOLUTION: %s:%d malformed tunables line\n", path, lineNo);
                fclose(f);
                return false;
            }
            haveTunables = true;
        }
        else if (strncmp(start, "stroke", 6) == 0)
        {
            if (sol->strokeCount >= SOLUTION_MAX_STROKES)
            {
                fprintf(stderr, "SOLUTION: %s:%d too many strokes (max %d)\n",
                        path, lineNo, SOLUTION_MAX_STROKES);
                fclose(f);
                return false;
            }
            if (!ParseStrokeLine(start + 6, &sol->strokes[sol->strokeCount]))
            {
                fprintf(stderr, "SOLUTION: %s:%d malformed stroke line\n", path, lineNo);
                fclose(f);
                return false;
            }
            sol->strokeCount++;
        }
        else if (strncmp(start, "boost", 5) == 0)
        {
            // Standalone boost lines were removed in version 4
            fprintf(stderr, "SOLUTION: %s:%d 'boost' lines are no longer supported (use stroke ... mask)\n",
                    path, lineNo);
            fclose(f);
            return false;
        }
        else if (strncmp(start, "cannon ", 7) == 0)
        {
            if (sol->cannonCount >= SOLUTION_MAX_CANNONS)
            {
                fprintf(stderr, "SOLUTION: %s:%d too many cannons (max %d)\n",
                        path, lineNo, SOLUTION_MAX_CANNONS);
                fclose(f);
                return false;
            }
            SolutionCannon *cannon = &sol->cannons[sol->cannonCount];
            if (sscanf(start + 7, "%f,%f %f", &cannon->pos.x, &cannon->pos.y, &cannon->angleRad) != 3)
            {
                fprintf(stderr, "SOLUTION: %s:%d malformed cannon line\n", path, lineNo);
                fclose(f);
                return false;
            }
            sol->cannonCount++;
        }
        else
        {
            fprintf(stderr, "SOLUTION: %s:%d unknown directive: %s", path, lineNo, start);
            fclose(f);
            return false;
        }
    }

    fclose(f);

    if (!haveVersion || !haveLevel || !haveTunables)
    {
        fprintf(stderr, "SOLUTION: %s missing required header (version/level/tunables)\n", path);
        return false;
    }

    return true;
}

void SolutionApply(const Solution *sol, PhysicsWorld *phys)
{
    for (int i = 0; i < sol->strokeCount; i++)
    {
        const SolutionStroke *stroke = &sol->strokes[i];
        const uint8_t *mask = stroke->hasBoostMask ? stroke->boostSeg : NULL;
        int slot = PhysicsCreateDrawnBody(phys, stroke->points, stroke->pointCount,
                                          SOLUTION_CRAYON, mask);
        if (slot < 0)
        {
            fprintf(stderr, "SOLUTION: failed to recreate stroke %d/%d\n", i + 1, sol->strokeCount);
        }
    }

    for (int i = 0; i < sol->cannonCount; i++)
    {
        if (PhysicsAddCannon(phys, sol->cannons[i].pos, sol->cannons[i].angleRad) < 0)
        {
            fprintf(stderr, "SOLUTION: failed to recreate cannon %d/%d\n", i + 1, sol->cannonCount);
        }
    }
}
