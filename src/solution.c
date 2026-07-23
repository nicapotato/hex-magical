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
    }

    // Boost lines and cannons are part of the built track — snapshot them too
    for (int i = 0; i < MAX_BOOST_LINES; i++)
    {
        const BoostLine *line = &phys->boostLines[i];
        if (!line->active) continue;
        if (sol->boostCount >= SOLUTION_MAX_BOOSTS) break;

        SolutionStroke *boost = &sol->boosts[sol->boostCount++];
        boost->pointCount = line->pointCount;
        for (int p = 0; p < line->pointCount; p++) boost->points[p] = line->points[p];
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
    fprintf(f, "tunables density=%.4f restitution=%.4f dropforce=%.4f\n",
            sol->tunables.ballDensity, sol->tunables.ballRestitution, sol->tunables.dropForce);

    for (int i = 0; i < sol->strokeCount; i++)
    {
        const SolutionStroke *stroke = &sol->strokes[i];
        fprintf(f, "stroke");
        for (int p = 0; p < stroke->pointCount; p++)
        {
            fprintf(f, " %.4f,%.4f", stroke->points[p].x, stroke->points[p].y);
        }
        fprintf(f, "\n");
    }

    for (int i = 0; i < sol->boostCount; i++)
    {
        const SolutionStroke *boost = &sol->boosts[i];
        fprintf(f, "boost");
        for (int p = 0; p < boost->pointCount; p++)
        {
            fprintf(f, " %.4f,%.4f", boost->points[p].x, boost->points[p].y);
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

// Parse one "stroke x,y x,y ..." line. Returns false on any malformed pair.
static bool ParseStrokeLine(const char *line, SolutionStroke *stroke)
{
    stroke->pointCount = 0;
    const char *cursor = line;

    while (*cursor)
    {
        while ((*cursor == ' ') || (*cursor == '\t')) cursor++;
        if ((*cursor == '\0') || (*cursor == '\n') || (*cursor == '\r')) break;

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
            if (sscanf(start + 9, "density=%f restitution=%f dropforce=%f",
                       &sol->tunables.ballDensity, &sol->tunables.ballRestitution,
                       &sol->tunables.dropForce) != 3)
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
            if (sol->boostCount >= SOLUTION_MAX_BOOSTS)
            {
                fprintf(stderr, "SOLUTION: %s:%d too many boost lines (max %d)\n",
                        path, lineNo, SOLUTION_MAX_BOOSTS);
                fclose(f);
                return false;
            }
            if (!ParseStrokeLine(start + 5, &sol->boosts[sol->boostCount]))
            {
                fprintf(stderr, "SOLUTION: %s:%d malformed boost line\n", path, lineNo);
                fclose(f);
                return false;
            }
            sol->boostCount++;
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
        int slot = PhysicsCreateDrawnBody(phys, stroke->points, stroke->pointCount, SOLUTION_CRAYON);
        if (slot < 0)
        {
            fprintf(stderr, "SOLUTION: failed to recreate stroke %d/%d\n", i + 1, sol->strokeCount);
        }
    }

    for (int i = 0; i < sol->boostCount; i++)
    {
        const SolutionStroke *boost = &sol->boosts[i];
        if (PhysicsCreateBoostLine(phys, boost->points, boost->pointCount) < 0)
        {
            fprintf(stderr, "SOLUTION: failed to recreate boost line %d/%d\n", i + 1, sol->boostCount);
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
