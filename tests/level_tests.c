/*******************************************************************************************
*
*   level_tests.c - Headless solution replay runner
*
*   For every .solution file (resources/solutions/ by default, or files passed as
*   args): load the level it names, recreate its strokes, Start, and step Box2D at
*   the game's fixed 60 Hz until the ball reaches the finish line or the step budget runs
*   out. No window, no rendering, no sleeping — the whole suite runs in milliseconds.
*
*   Output is one line per solution, built for humans and agents alike:
*       PASS map-2.solution (won in 214 steps, 3.57 sim-seconds)
*       WARN map-2.solution: level 'map-2.tmx' not solved after 900 steps (ball ended at 412.3, 655.0)
*   Exit code: 0 = all passed, 1 = at least one failure.
*
********************************************************************************************/

#include "physics.h"
#include "solution.h"
#include "tiled.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

// 15 sim-seconds at 60 Hz — generous for any intended solution
#define MAX_TEST_STEPS 900

// Big structs stay off the stack
static Solution solution = { 0 };
static Solution recaptured = { 0 };
static TiledLevel level = { 0 };
static PhysicsWorld physics = { 0 };

// -v: print the ball position once per sim-second — trajectory at a glance
static bool verbose = false;

// Same candidate dirs as the game: repo root and beside the binary.
// Solutions name their level by .tmx basename; maps live in act subfolders
// (resources/act-1/map-2.tmx), so search the tree for a basename match.
static bool FindLevelTmx(const char *levelFile, char *out, int outSize)
{
    const char *dirs[] = { "resources", "../../resources" };
    for (int i = 0; i < 2; i++)
    {
        if (!DirectoryExists(dirs[i])) continue;

        FilePathList files = LoadDirectoryFilesEx(dirs[i], ".tmx", true);
        bool found = false;
        for (unsigned int f = 0; f < files.count; f++)
        {
            if (strcmp(GetFileName(files.paths[f]), levelFile) != 0) continue;
            snprintf(out, outSize, "%s", files.paths[f]);
            found = true;
            break;
        }
        UnloadDirectoryFiles(files);
        if (found) return true;
    }
    return false;
}

static bool RunSolution(const char *path)
{
    const char *name = GetFileName(path);

    if (!SolutionLoad(&solution, path))
    {
        printf("FAIL %s: unreadable or malformed solution file\n", name);
        return false;
    }

    // A renamed/deleted level must break its test — that's the point
    char tmxPath[512];
    if (!FindLevelTmx(solution.levelFile, tmxPath, sizeof(tmxPath)))
    {
        printf("FAIL %s: level '%s' not found in resources/\n", name, solution.levelFile);
        return false;
    }

    memset(&level, 0, sizeof(level));
    if (!TiledLevelLoad(&level, tmxPath))
    {
        printf("FAIL %s: could not parse %s\n", name, tmxPath);
        return false;
    }

    PhysicsInit(&physics);
    physics.tunables = solution.tunables; // ball is built with the recorded knobs
    PhysicsLoadLevel(&physics, &level.def);
    SolutionApply(&solution, &physics);

    // Round-trip check: capturing back from the world (the in-game F5 path) must
    // reproduce the strokes we just applied — guards the save/load/capture pipeline
    SolutionCapture(&recaptured, &physics, solution.levelFile);
    if (recaptured.strokeCount != solution.strokeCount)
    {
        printf("FAIL %s: capture round-trip lost strokes (%d applied, %d captured)\n",
               name, solution.strokeCount, recaptured.strokeCount);
        PhysicsShutdown(&physics);
        TiledLevelUnload(&level);
        return false;
    }
    if ((recaptured.boostCount != solution.boostCount) ||
        (recaptured.cannonCount != solution.cannonCount))
    {
        printf("FAIL %s: capture round-trip lost builds (boosts %d->%d, cannons %d->%d)\n",
               name, solution.boostCount, recaptured.boostCount,
               solution.cannonCount, recaptured.cannonCount);
        PhysicsShutdown(&physics);
        TiledLevelUnload(&level);
        return false;
    }
    for (int s = 0; s < solution.strokeCount; s++)
    {
        for (int p = 0; p < solution.strokes[s].pointCount; p++)
        {
            float dx = recaptured.strokes[s].points[p].x - solution.strokes[s].points[p].x;
            float dy = recaptured.strokes[s].points[p].y - solution.strokes[s].points[p].y;
            if ((dx*dx + dy*dy) > (0.01f*0.01f))
            {
                printf("FAIL %s: capture round-trip moved stroke %d point %d by (%.4f, %.4f)\n",
                       name, s, p, dx, dy);
                PhysicsShutdown(&physics);
                TiledLevelUnload(&level);
                return false;
            }
        }
    }

    PhysicsStartSimulation(&physics);

    const float step = 1.0f / PHYSICS_HZ;
    bool won = false;
    int steps = 0;
    for (steps = 1; steps <= MAX_TEST_STEPS; steps++)
    {
        PhysicsStep(&physics, step);
        if (verbose && ((steps % (int)PHYSICS_HZ) == 0))
        {
            Vector2 ball = PhysicsGetBallPos(&physics);
            printf("  t=%2ds ball=(%.1f, %.1f)\n", steps / (int)PHYSICS_HZ, ball.x, ball.y);
        }
        if (PhysicsCheckWin(&physics))
        {
            won = true;
            break;
        }
        if (PhysicsCheckPit(&physics))
        {
            Vector2 ball = PhysicsGetBallPos(&physics);
            printf("WARN %s: ball fell into a pit at (%.1f, %.1f) after %d steps\n",
                   name, ball.x, ball.y, steps);
            break;
        }
    }

    if (won)
    {
        printf("PASS %s (won in %d steps, %.2f sim-seconds)\n", name, steps, steps * step);
    }
    else
    {
        Vector2 ball = PhysicsGetBallPos(&physics);
        printf("WARN %s: level '%s' not solved after %d steps (ball ended at %.1f, %.1f)\n",
               name, solution.levelFile, MAX_TEST_STEPS, ball.x, ball.y);
    }

    PhysicsShutdown(&physics);
    TiledLevelUnload(&level);
    return won;
}

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING); // quiet raylib/tiled info spam; errors still surface

    int total = 0;
    int failures = 0;

    int firstFile = 1;
    if ((argc > 1) && (strcmp(argv[1], "-v") == 0))
    {
        verbose = true;
        firstFile = 2;
    }

    if (argc > firstFile)
    {
        // Explicit file args: agent/dev checking specific solutions
        for (int i = firstFile; i < argc; i++)
        {
            total++;
            if (!RunSolution(argv[i])) failures++;
        }
    }
    else
    {
        const char *dirs[] = { "resources/solutions", "../../resources/solutions" };
        const char *dir = NULL;
        for (int i = 0; i < 2; i++)
        {
            if (DirectoryExists(dirs[i])) { dir = dirs[i]; break; }
        }
        if (dir == NULL)
        {
            printf("level-tests: no resources/solutions directory — nothing to test\n");
            return 0;
        }

        FilePathList files = LoadDirectoryFilesEx(dir, ".solution", false);

        // Sort by path for a stable run order (filesystem order is arbitrary)
        for (unsigned int i = 1; i < files.count; i++)
        {
            char *key = files.paths[i];
            int j = (int)i - 1;
            while ((j >= 0) && (strcmp(files.paths[j], key) > 0))
            {
                files.paths[j + 1] = files.paths[j];
                j--;
            }
            files.paths[j + 1] = key;
        }

        for (unsigned int i = 0; i < files.count; i++)
        {
            total++;
            if (!RunSolution(files.paths[i])) failures++;
        }
        UnloadDirectoryFiles(files);

        if (total == 0)
        {
            printf("level-tests: no .solution files in %s — nothing to test\n", dir);
            return 0;
        }
    }

    printf("level-tests: %d/%d passed\n", total - failures, total);
    return (failures > 0) ? 1 : 0;
}
