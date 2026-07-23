/*******************************************************************************************
*
*   game.c - Crayon Physics clone: state machine, update, draw
*
********************************************************************************************/

#include "game.h"
#include "admin.h"
#include "levels.h"
#include "platform.h"
#include "physics.h"
#include "render.h"
#include "sketch.h"
#include "solution.h"
#include "tiled.h"

#include "raylib.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//----------------------------------------------------------------------------------
// Global Variables Definition (local to this module)
//----------------------------------------------------------------------------------
static RenderTexture2D target = { 0 };
static GameScreen screen = SCREEN_TITLE;
static int levelIndex = 0;
static PhysicsWorld physics = { 0 };
static SketchState sketch = { 0 };
static bool debugMode = false;
static bool levelMenuOpen = false;
static bool pauseMenuOpen = false; // ESC menu: world frozen while open
static bool winMenuShow = true; // false = admiring the finished run
static bool winSolutionSaved = false;
static int winTrailCount = 0;   // trail samples recorded up to the win moment
static float runTime = 0.0f;    // seconds since Start, frozen at win
static float viewZoom = 1.0f;
static Vector2 viewPan = { 0.0f, 0.0f }; // camera target offset from level center

#define VIEW_ZOOM_MIN 0.15f // zoom out enough to survey large 1:1 Tiled maps
#define VIEW_ZOOM_MAX 4.0f
#define VIEW_ZOOM_RATE 1.5f // exponential zoom speed per second while +/- held
#define VIEW_PAN_SPEED 480.0f // world pixels per second at 1x zoom

#define MAX_TILED_LEVELS 30
static TiledLevel tiledLevels[MAX_TILED_LEVELS] = { 0 };
static int tiledLevelCount = 0;
static float tiledWatchTimer = 0.0f;

// Which resources dir the shipped levels came from
static char resourcesDir[256] = "resources";
// The dir GameInit resolved at startup — "RESET RESOURCE PATH" returns here
static char defaultResourcesDir[256] = "resources";
// Shared scratch (the struct is ~165 KB — keep it off the stack)
static Solution solutionScratch = { 0 };

//----------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------
// Registry: every Tiled level found in resources/ (the only level source)
static TiledLevel *GetTiledLevel(int index)
{
    if ((index < 0) || (index >= tiledLevelCount)) index = 0;
    return &tiledLevels[index];
}

static const LevelDef *GetLevelDef(int index)
{
    return &GetTiledLevel(index)->def;
}

int GameGetLevelCount(void)
{
    return tiledLevelCount;
}

const char *GameGetLevelName(int index)
{
    return GetLevelDef(index)->name;
}

// Natural filename compare: digit runs compare as numbers, so map-2 < map-10.
// Plain strcmp would order map-10/map-11/map-12 before map-2.
static int NaturalCompare(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b))
        {
            long numA = strtol(a, (char **)&a, 10);
            long numB = strtol(b, (char **)&b, 10);
            if (numA != numB) return (numA < numB) ? -1 : 1;
        }
        else
        {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++;
            b++;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// Scan a resources dir for .tmx maps, sorted numerically for a stable level order
static void LoadTiledLevels(const char *dir)
{
    if (!DirectoryExists(dir)) return;

    FilePathList files = LoadDirectoryFilesEx(dir, ".tmx", false);

    // Insertion sort paths by name (LoadDirectoryFilesEx order is filesystem-dependent)
    for (unsigned int i = 1; i < files.count; i++)
    {
        char *key = files.paths[i];
        int j = (int)i - 1;
        while ((j >= 0) && (NaturalCompare(files.paths[j], key) > 0))
        {
            files.paths[j + 1] = files.paths[j];
            j--;
        }
        files.paths[j + 1] = key;
    }

    for (unsigned int i = 0; (i < files.count) && (tiledLevelCount < MAX_TILED_LEVELS); i++)
    {
        // Fail loud to stderr: platform.c sets LOG_NONE in release, which would
        // otherwise swallow TiledLevelLoad TraceLog errors and silently skip maps.
        if (TiledLevelLoad(&tiledLevels[tiledLevelCount], files.paths[i]))
        {
            fprintf(stderr, "LEVEL: loaded [%d] %s (%dx%d)\n",
                    tiledLevelCount,
                    tiledLevels[tiledLevelCount].name,
                    tiledLevels[tiledLevelCount].mapWidth,
                    tiledLevels[tiledLevelCount].mapHeight);
            tiledLevelCount++;
        }
        else
        {
            fprintf(stderr, "LEVEL: skipped %s (see TILED errors above)\n", files.paths[i]);
        }
    }

    UnloadDirectoryFiles(files);
}

// The view keeps the game's vertical size (720) and widens to match the window
// aspect — no stretching, wide windows simply see more world horizontally.
static int GetDesiredViewWidth(void)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    if (sh <= 0.0f) return GAME_SCREEN_WIDTH;

    int w = (int)roundf((float)GAME_SCREEN_HEIGHT * sw / sh);
    if (w < 320) w = 320;
    if (w > 4096) w = 4096;
    return w;
}

int GameGetViewWidth(void)
{
    return target.texture.width;
}

// World rendering goes through this camera: level center + WASD pan, +/- zoom.
static Camera2D GetWorldCamera(void)
{
    return (Camera2D){
        .offset = { (float)target.texture.width * 0.5f, (float)GAME_SCREEN_HEIGHT * 0.5f },
        .target = {
            (float)GAME_SCREEN_WIDTH * 0.5f + viewPan.x,
            (float)GAME_SCREEN_HEIGHT * 0.5f + viewPan.y
        },
        .rotation = 0.0f,
        .zoom = viewZoom
    };
}

// Mouse in view (HUD) coordinates — the render texture fills the whole window
static Vector2 GetViewMouse(void)
{
    Vector2 mouse = GetMousePosition();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    if ((sw <= 0.0f) || (sh <= 0.0f)) return mouse;

    return (Vector2){
        mouse.x * ((float)target.texture.width / sw),
        mouse.y * ((float)GAME_SCREEN_HEIGHT / sh)
    };
}

// Tools available on the current level: zero-capacity TMX resources are hidden
// from the HUD and unselectable. The checkpoint flag and eraser are always free.
static int GetVisibleTools(BuildTool tools[TOOL_COUNT])
{
    const LevelDef *level = GetLevelDef(levelIndex);
    int count = 0;
    if (level->lineCapacity > 0.0f) tools[count++] = TOOL_CRAYON;
    if (level->boostLineCapacity > 0.0f) tools[count++] = TOOL_BOOST_LINE;
    if (level->cannonCount > 0) tools[count++] = TOOL_CANNON;
    tools[count++] = TOOL_FLAG;
    tools[count++] = TOOL_ERASER;
    return count;
}

static void TrySelectTool(BuildTool tool)
{
    BuildTool tools[TOOL_COUNT];
    int count = GetVisibleTools(tools);
    for (int i = 0; i < count; i++)
    {
        if (tools[i] == tool)
        {
            SketchCancel(&sketch);
            sketch.tool = tool;
            return;
        }
    }
}

static void LoadCurrentLevel(void)
{
    PhysicsLoadLevel(&physics, GetLevelDef(levelIndex));
    SketchInit(&sketch);

    // Default to the first tool the level actually offers
    BuildTool tools[TOOL_COUNT];
    GetVisibleTools(tools);
    sketch.tool = tools[0];
}

static const char *GetSolutionsDirectory(void)
{
#if defined(PLATFORM_WEB)
    // Mounted to IndexedDB by web_storage.js; unlike /resources, this survives refresh.
    return "/solutions";
#else
    // Keep desktop development fixtures beside levels so F5-created solutions can
    // be committed and used directly by the headless test runner.
    // 256 (resourcesDir) + "/solutions" + NUL fits provably — keeps GCC's
    // -Wformat-truncation quiet here and in GetCurrentSolutionPath.
    static char directory[288];
    snprintf(directory, sizeof(directory), "%s/solutions", resourcesDir);
    return directory;
#endif
}

// <solution directory>/<level>.solution for the current level
static const char *GetCurrentSolutionPath(void)
{
    // Sized past the directory buffer + "/<name>.solution" so GCC's
    // -Wformat-truncation can prove the snprintf always fits
    static char path[832];
    const char *base = GetFileNameWithoutExt(GetTiledLevel(levelIndex)->tmxPath);
    snprintf(path, sizeof(path), "%s/%s.solution", GetSolutionsDirectory(), base);
    return path;
}

// F5: snapshot the drawn strokes + tunables so players can come back to them
// and devs can commit them as level tests (run via `make test`)
static bool SaveCurrentSolution(void)
{
    const char *solutionsDir = GetSolutionsDirectory();
    if (!DirectoryExists(solutionsDir) && (MakeDirectory(solutionsDir) != 0))
    {
        fprintf(stderr, "SOLUTION: failed to create directory %s\n", solutionsDir);
        return false;
    }

    const char *levelFile = GetFileName(GetTiledLevel(levelIndex)->tmxPath);
    SolutionCapture(&solutionScratch, &physics, levelFile);

    const char *path = GetCurrentSolutionPath();
    if (SolutionSave(&solutionScratch, path))
    {
        fprintf(stderr, "SOLUTION: saved %d strokes, %d boosts, %d cannons to %s\n",
                solutionScratch.strokeCount, solutionScratch.boostCount,
                solutionScratch.cannonCount, path);
        PlatformSyncFiles();
        return true;
    }
    return false;
}

// F9: reset the level and replay the saved strokes — back in build phase, free to edit
static void RestoreCurrentSolution(void)
{
    const char *path = GetCurrentSolutionPath();
    if (!SolutionLoad(&solutionScratch, path)) return; // SolutionLoad already logged why

    // The file names its level — refuse a stale/renamed solution instead of
    // silently replaying strokes authored for different geometry
    const char *levelFile = GetFileName(GetTiledLevel(levelIndex)->tmxPath);
    if (strcmp(solutionScratch.levelFile, levelFile) != 0)
    {
        fprintf(stderr, "SOLUTION: %s is for level '%s' but current level is '%s' — not loading\n",
                path, solutionScratch.levelFile, levelFile);
        return;
    }

    physics.tunables = solutionScratch.tunables; // ball is rebuilt with the recorded knobs
    LoadCurrentLevel();
    SolutionApply(&solutionScratch, &physics);
    fprintf(stderr, "SOLUTION: restored %d strokes, %d boosts, %d cannons from %s\n",
            solutionScratch.strokeCount, solutionScratch.boostCount,
            solutionScratch.cannonCount, path);
}

// F8: delete the saved solution for the current level
static void DeleteCurrentSolution(void)
{
    const char *path = GetCurrentSolutionPath();
    if (!FileExists(path))
    {
        fprintf(stderr, "SOLUTION: nothing to delete — %s does not exist\n", path);
        return;
    }
    if (remove(path) == 0)
    {
        fprintf(stderr, "SOLUTION: deleted %s\n", path);
        PlatformSyncFiles();
    }
    else fprintf(stderr, "SOLUTION: failed to delete %s\n", path);
}

static void StartPlaying(void)
{
    screen = SCREEN_PLAYING;
    LoadCurrentLevel();
}

const char *GameGetResourcesDir(void)
{
    return resourcesDir;
}

// Swap the level registry to a new folder (admin "LOAD ASSET FOLDER").
// The folder needs the same layout as resources/: *.tmx maps + tileset.tsx/.png.
// On failure the previous folder is rescanned so the game never ends up empty.
bool GameSetResourcesDir(const char *dir)
{
    if (!DirectoryExists(dir))
    {
        fprintf(stderr, "RESOURCES: %s is not a directory\n", dir);
        return false;
    }
    if (strlen(dir) >= sizeof(resourcesDir))
    {
        fprintf(stderr, "RESOURCES: path too long (%zu chars, max %zu): %s\n",
                strlen(dir), sizeof(resourcesDir) - 1, dir);
        return false;
    }

    char previous[sizeof(resourcesDir)];
    snprintf(previous, sizeof(previous), "%s", resourcesDir);

    for (int i = 0; i < tiledLevelCount; i++) TiledLevelUnload(&tiledLevels[i]);
    tiledLevelCount = 0;

    LoadTiledLevels(dir);
    bool ok = (tiledLevelCount > 0);
    if (ok)
    {
        snprintf(resourcesDir, sizeof(resourcesDir), "%s", dir);
        fprintf(stderr, "RESOURCES: switched to %s (%d levels)\n", dir, tiledLevelCount);
    }
    else
    {
        fprintf(stderr, "RESOURCES: no loadable .tmx maps in %s (see TILED errors above) — keeping %s\n",
                dir, previous);
        LoadTiledLevels(previous);
        if (tiledLevelCount == 0)
        {
            fprintf(stderr, "FATAL: previous resources dir %s no longer loads either\n", previous);
            abort();
        }
    }

    // Physics geometry points into the old registry slots — rebuild either way
    levelIndex = 0;
    if (screen != SCREEN_TITLE) StartPlaying();
    return ok;
}

static void AdvanceLevel(void)
{
    levelIndex++;
    if (levelIndex >= GameGetLevelCount())
    {
        levelIndex = 0;
        screen = SCREEN_TITLE;
        PhysicsShutdown(&physics);
        return;
    }
    StartPlaying();
}

static void QuitToTitle(void)
{
    screen = SCREEN_TITLE;
    levelMenuOpen = false;
    pauseMenuOpen = false;
    PhysicsShutdown(&physics);
}

// Win screen -> back to the build phase with everything intact. The recording
// is trimmed to the win moment so the ghost trail ends at the star instead of
// including the post-win admire rolling.
static void KeepEditingAfterWin(void)
{
    if ((winTrailCount > 0) && (winTrailCount <= physics.trailCount))
    {
        physics.trailCount = winTrailCount;
    }
    PhysicsStopSimulation(&physics); // archives the trail as the ghost
    screen = SCREEN_PLAYING;
}

// Live strokes on the canvas (drawnCount is a high-water mark, slots can be erased)
static int CountActiveStrokes(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        if (physics.drawn[i].active) count++;
    }
    return count;
}

// Space/Enter or the START/STOP button toggles simulation either way
static bool WantsToggleSimulation(bool lmbPressed, Vector2 uiMouse)
{
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) return true;
    if (lmbPressed && CheckCollisionPointRec(uiMouse, RenderGetStartButtonRect())) return true;
    return false;
}

static bool IsUiClick(Vector2 uiMouse)
{
    if (CheckCollisionPointRec(uiMouse, RenderGetDebugButtonRect())) return true;
    if (CheckCollisionPointRec(uiMouse, AdminGetButtonRect())) return true;
    if (CheckCollisionPointRec(uiMouse, RenderGetLevelMenuHeaderRect())) return true;
    if (CheckCollisionPointRec(uiMouse, RenderGetStartButtonRect())) return true;

    // Tool bar only exists during the build phase
    if (!PhysicsIsSimulating(&physics))
    {
        BuildTool tools[TOOL_COUNT];
        int toolCount = GetVisibleTools(tools);
        for (int i = 0; i < toolCount; i++)
        {
            if (CheckCollisionPointRec(uiMouse, RenderGetToolButtonRect(i))) return true;
        }
    }
    return false;
}

// Number row 1-9 selects levels 1-9, 0 selects level 10; returns -1 if none pressed
static int GetLevelSelectKey(void)
{
    for (int i = 0; i < 9; i++)
    {
        if (IsKeyPressed(KEY_ONE + i)) return (i < GameGetLevelCount()) ? i : -1;
    }
    if (IsKeyPressed(KEY_ZERO) && (GameGetLevelCount() > 9)) return 9;
    return -1;
}

// [ and ] step backward/forward through all levels; returns new index or -1
static int GetLevelStepKey(void)
{
    int count = GameGetLevelCount();
    if (IsKeyPressed(KEY_LEFT_BRACKET)) return (levelIndex + count - 1) % count;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) return (levelIndex + 1) % count;
    return -1;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void GameInit(void)
{
    target = LoadRenderTexture(GetDesiredViewWidth(), GAME_SCREEN_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    viewZoom = 1.0f;
    viewPan = (Vector2){ 0.0f, 0.0f };

    // Tiled levels are the only level source — every .tmx in resources/ joins the registry.
    // Candidate dirs cover repo-root development, the old binary-relative layout,
    // and packaged macOS/Windows apps regardless of their process working directory.
    LoadTiledLevels("resources");
    if (tiledLevelCount == 0)
    {
        LoadTiledLevels("../../resources");
        if (tiledLevelCount > 0) snprintf(resourcesDir, sizeof(resourcesDir), "../../resources");
    }
#if !defined(PLATFORM_WEB)
    if (tiledLevelCount == 0)
    {
        char appResources[512];
        snprintf(appResources, sizeof(appResources), "%sresources", GetApplicationDirectory());
        LoadTiledLevels(appResources);
        if (tiledLevelCount > 0) snprintf(resourcesDir, sizeof(resourcesDir), "%s", appResources);
    }
#endif
    if (tiledLevelCount == 0)
    {
        // Without maps there is no game — abort loudly rather than limping along.
        // (fprintf, not TraceLog: platform.c sets LOG_NONE which would swallow it)
        fprintf(stderr, "FATAL: no loadable .tmx maps found in resources/ — nothing to play\n");
        abort();
    }

    // Whatever dir won the search above is the game's original path —
    // the admin "RESET RESOURCE PATH" button restores it
    snprintf(defaultResourcesDir, sizeof(defaultResourcesDir), "%s", resourcesDir);

    // ESC is used by the win menu (admire toggle) — don't let raylib treat it as quit
    SetExitKey(KEY_NULL);

    PhysicsInit(&physics);
    SketchInit(&sketch);
    screen = SCREEN_TITLE;
    levelIndex = 0;
    debugMode = false;
    levelMenuOpen = false;
}

void GameUpdateDrawFrame(void)
{
    float dt = GetFrameTime();

    // Hot reload: poll the .tmx files every half second; rebuild if saved from Tiled
    tiledWatchTimer += dt;
    if ((tiledLevelCount > 0) && (tiledWatchTimer >= 0.5f))
    {
        tiledWatchTimer = 0.0f;
        for (int i = 0; i < tiledLevelCount; i++)
        {
            if (!TiledLevelFileChanged(&tiledLevels[i])) continue;
            if (!TiledLevelLoad(&tiledLevels[i], tiledLevels[i].tmxPath)) continue;

            if ((GetTiledLevel(levelIndex) == &tiledLevels[i]) && (screen != SCREEN_TITLE))
            {
                LoadCurrentLevel(); // rebuild physics against the edited geometry
            }
        }
    }

#if !defined(PLATFORM_WEB)
    if (IsKeyPressed(KEY_F11) ||
        (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))))
    {
        ToggleBorderlessWindowed();
    }
#endif

    // Follow window aspect: recreate the view texture when its width changes
    int desiredViewWidth = GetDesiredViewWidth();
    if (desiredViewWidth != target.texture.width)
    {
        UnloadRenderTexture(target);
        target = LoadRenderTexture(desiredViewWidth, GAME_SCREEN_HEIGHT);
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    }

    // Hold + / - (or keypad) to zoom the world view in and out
    float zoomDir = 0.0f;
    if (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD)) zoomDir += 1.0f;
    if (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT)) zoomDir -= 1.0f;
    if (zoomDir != 0.0f)
    {
        viewZoom *= expf(zoomDir * VIEW_ZOOM_RATE * dt);
        if (viewZoom < VIEW_ZOOM_MIN) viewZoom = VIEW_ZOOM_MIN;
        if (viewZoom > VIEW_ZOOM_MAX) viewZoom = VIEW_ZOOM_MAX;
    }

    // WASD pans the camera; speed is in screen-space so zoomed-in moves feel the same
    {
        Vector2 panDir = { 0.0f, 0.0f };
        if (IsKeyDown(KEY_A)) panDir.x -= 1.0f;
        if (IsKeyDown(KEY_D)) panDir.x += 1.0f;
        if (IsKeyDown(KEY_W)) panDir.y -= 1.0f;
        if (IsKeyDown(KEY_S)) panDir.y += 1.0f;
        if ((panDir.x != 0.0f) || (panDir.y != 0.0f))
        {
            float len = sqrtf(panDir.x * panDir.x + panDir.y * panDir.y);
            float speed = (VIEW_PAN_SPEED / viewZoom) * dt / len;
            viewPan.x += panDir.x * speed;
            viewPan.y += panDir.y * speed;
        }
    }

    Camera2D camera = GetWorldCamera();
    Vector2 uiMouse = GetViewMouse();                          // HUD hit tests
    Vector2 worldMouse = GetScreenToWorld2D(uiMouse, camera);  // sketching / no-build
    bool lmbDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool lmbPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool rmbPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    // Update
    //----------------------------------------------------------------------------------
    if (screen == SCREEN_TITLE)
    {
        int selected = GetLevelSelectKey();
        if (IsKeyPressed(KEY_SPACE) || lmbPressed)
        {
            StartPlaying();
        }
        else if (selected >= 0) { levelIndex = selected; StartPlaying(); }
    }
    else if ((screen == SCREEN_PLAYING) || (screen == SCREEN_WIN))
    {
        // ESC menu (build/run only — the win screen keeps ESC for admire).
        // Layered like a proper pause: an open dropdown closes first.
        if ((screen == SCREEN_PLAYING) && IsKeyPressed(KEY_ESCAPE))
        {
            if (levelMenuOpen) levelMenuOpen = false;
            else pauseMenuOpen = !pauseMenuOpen;
        }

        // While paused the overlay owns all input — skip the shared UI below
        if (!pauseMenuOpen)
        {
            // Admin UI gets first claim on the mouse
            AdminAction adminAction = AdminHandleInput(&physics, uiMouse, lmbDown, lmbPressed);
            if (adminAction == ADMIN_ACTION_RESPAWN)
            {
                screen = SCREEN_PLAYING;
                LoadCurrentLevel();
            }
            else if (adminAction == ADMIN_ACTION_PICK_FOLDER)
            {
                // Native folder dialog — blocks the loop; PhysicsStep clamps the
                // large dt of the frame that resumes afterwards
                char picked[512];
                if (PlatformPickFolder(picked, sizeof(picked)))
                {
                    GameSetResourcesDir(picked);
                }
            }
            else if (adminAction == ADMIN_ACTION_RESET_FOLDER)
            {
                // Back to the dir resolved at startup (no-op if already there)
                if (strcmp(resourcesDir, defaultResourcesDir) != 0)
                {
                    GameSetResourcesDir(defaultResourcesDir);
                }
            }
            if (adminAction != ADMIN_ACTION_NONE)
            {
                lmbPressed = false;
                lmbDown = false;
            }

            // Levels dropdown
            if (lmbPressed)
            {
                if (CheckCollisionPointRec(uiMouse, RenderGetLevelMenuHeaderRect()))
                {
                    levelMenuOpen = !levelMenuOpen;
                    lmbPressed = false;
                    lmbDown = false;
                }
                else if (levelMenuOpen)
                {
                    for (int i = 0; i < GameGetLevelCount(); i++)
                    {
                        if (CheckCollisionPointRec(uiMouse, RenderGetLevelMenuItemRect(i)))
                        {
                            levelIndex = i;
                            StartPlaying();
                            break;
                        }
                    }
                    // Any click while open closes the menu and is consumed
                    levelMenuOpen = false;
                    lmbPressed = false;
                    lmbDown = false;
                }
            }

            // DEBUG toggle available during play/win
            // F3 (not D — D is camera pan) or the DEBUG button
            if (IsKeyPressed(KEY_F3) ||
                (lmbPressed && CheckCollisionPointRec(uiMouse, RenderGetDebugButtonRect())))
            {
                debugMode = !debugMode;
                lmbPressed = false; // consume click so it doesn't draw / advance
            }
        }
    }

    if ((screen == SCREEN_PLAYING) && pauseMenuOpen)
    {
        // Paused: world frozen, only the pause menu responds
        if (IsKeyPressed(KEY_R))
        {
            pauseMenuOpen = false;
            StartPlaying();
        }
        else if (IsKeyPressed(KEY_Q)) { QuitToTitle(); }
        else if (lmbPressed)
        {
            for (int i = 0; i < 3; i++)
            {
                if (!CheckCollisionPointRec(uiMouse, RenderGetWinMenuButtonRect(i, 3))) continue;
                if (i == 0) pauseMenuOpen = false;              // Resume
                else if (i == 1)
                {
                    pauseMenuOpen = false;
                    StartPlaying();                             // Restart level
                }
                else QuitToTitle();                             // Quit to title
                break;
            }
        }
    }
    else if (screen == SCREEN_PLAYING)
    {
        // Alt+Z reverts the last build action (draw/erase/place); plain Z is a tool key
        bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        if (IsKeyPressed(KEY_Z) && altDown)
        {
            if (!PhysicsIsSimulating(&physics))
            {
                SketchCancel(&sketch);
                PhysicsUndoLastAction(&physics);
            }
        }

        // Tool hotkeys — only tools the level offers can be selected
        if (IsKeyPressed(KEY_Z) && !altDown) TrySelectTool(TOOL_CRAYON);
        if (IsKeyPressed(KEY_X)) TrySelectTool(TOOL_BOOST_LINE);
        if (IsKeyPressed(KEY_C)) TrySelectTool(TOOL_CANNON);
        if (IsKeyPressed(KEY_V)) TrySelectTool(TOOL_FLAG);
        if (IsKeyPressed(KEY_E)) TrySelectTool(TOOL_ERASER);

        // Tool bar clicks (build phase) — consumed so they never draw ink
        if (lmbPressed && !PhysicsIsSimulating(&physics))
        {
            BuildTool tools[TOOL_COUNT];
            int toolCount = GetVisibleTools(tools);
            for (int i = 0; i < toolCount; i++)
            {
                if (CheckCollisionPointRec(uiMouse, RenderGetToolButtonRect(i)))
                {
                    TrySelectTool(tools[i]);
                    lmbPressed = false;
                    lmbDown = false;
                    break;
                }
            }
        }

        // Number row is solutions during play (1 save / 2 load / 3 delete) —
        // laptop-friendly, no F-keys. Level select stays on the menu, [ ], and
        // the title screen's number keys.
        int stepped = GetLevelStepKey();
        if (IsKeyPressed(KEY_R))
        {
            LoadCurrentLevel();
        }
        else if (stepped >= 0) { levelIndex = stepped; LoadCurrentLevel(); }
        else if (IsKeyPressed(KEY_ONE)) { SaveCurrentSolution(); }
        else if (IsKeyPressed(KEY_TWO)) { RestoreCurrentSolution(); }
        else if (IsKeyPressed(KEY_THREE)) { DeleteCurrentSolution(); }
        else
        {
            bool building = !PhysicsIsSimulating(&physics);
            bool uiClick = IsUiClick(uiMouse);

            // Enter / START drops the ball; while running the same input STOPs:
            // ball back to spawn, strokes intact, free to draw/erase/save/load again
            if (WantsToggleSimulation(lmbPressed, uiMouse))
            {
                SketchCancel(&sketch);
                if (building)
                {
                    runTime = 0.0f;
                    PhysicsStartSimulation(&physics);
                }
                else PhysicsStopSimulation(&physics);
            }
            else if (building)
            {
                // Drawing/erasing is build-phase only: the world is immutable after
                // Start, so every run is a pure replay of the built track
                bool sketchLmbPressed = lmbPressed && !uiClick;
                bool sketchLmbDown = lmbDown && !uiClick;

                // No-build zones don't cancel the stroke — ink simply doesn't
                // register inside, splitting the stroke around the zone
                bool inNoBuild = TiledLevelNoBuildContains(GetTiledLevel(levelIndex), worldMouse);

                SketchUpdate(&sketch, &physics, worldMouse, sketchLmbDown, sketchLmbPressed, rmbPressed, inNoBuild);
            }

            PhysicsStep(&physics, dt);
            if (PhysicsIsSimulating(&physics)) runTime += dt;

            if (PhysicsIsSimulating(&physics) && PhysicsCheckWin(&physics))
            {
                screen = SCREEN_WIN;
                winMenuShow = true;
                winSolutionSaved = false;
                winTrailCount = physics.trailCount; // ghost ends here, not mid-admire
            }
            else if (PhysicsIsSimulating(&physics) && PhysicsCheckPit(&physics))
            {
                screen = SCREEN_GAMEOVER;
            }
        }
    }
    else if (screen == SCREEN_GAMEOVER)
    {
        // Fell into a pit. Strokes survive — Try again goes back to the build phase.
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_T))
        {
            PhysicsStopSimulation(&physics);
            screen = SCREEN_PLAYING;
        }
        else if (IsKeyPressed(KEY_R)) { StartPlaying(); }
        else if (IsKeyPressed(KEY_Q)) { QuitToTitle(); }
        else if (lmbPressed)
        {
            for (int i = 0; i < 3; i++)
            {
                if (!CheckCollisionPointRec(uiMouse, RenderGetWinMenuButtonRect(i, 3))) continue;
                if (i == 0)
                {
                    PhysicsStopSimulation(&physics); // Try again (keep strokes)
                    screen = SCREEN_PLAYING;
                }
                else if (i == 1) StartPlaying();     // Restart level (fresh)
                else QuitToTitle();                  // Quit to title
                break;
            }
        }
    }
    else if (screen == SCREEN_WIN)
    {
        // The world keeps simulating — the ball rolls on while you admire the run
        PhysicsStep(&physics, dt);

        bool hasNext = (levelIndex + 1) < GameGetLevelCount();
        int buttonCount = hasNext ? 6 : 5;

        if (!winMenuShow)
        {
            if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_ESCAPE)) winMenuShow = true;
        }
        else if (IsKeyPressed(KEY_R)) { StartPlaying(); }
        else if (IsKeyPressed(KEY_E)) { KeepEditingAfterWin(); }
        else if (IsKeyPressed(KEY_S)) { winSolutionSaved = SaveCurrentSolution(); }
        else if (IsKeyPressed(KEY_N) && hasNext) { AdvanceLevel(); }
        else if (IsKeyPressed(KEY_Q)) { QuitToTitle(); }
        else if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_ESCAPE)) { winMenuShow = false; }
        else if (lmbPressed)
        {
            for (int i = 0; i < buttonCount; i++)
            {
                if (!CheckCollisionPointRec(uiMouse, RenderGetWinMenuButtonRect(i, buttonCount))) continue;
                if (i == 0) winMenuShow = false;                  // Admire creation
                else if (i == 1) KeepEditingAfterWin();           // Back to build, ghost kept
                else if (i == 2) winSolutionSaved = SaveCurrentSolution();
                else if (i == 3) StartPlaying();                  // Restart level (fresh)
                else if ((i == 4) && hasNext) AdvanceLevel();     // Next level
                else QuitToTitle();                               // Quit to title
                break;
            }
        }
    }
    //----------------------------------------------------------------------------------

    // Draw
    //----------------------------------------------------------------------------------
    BeginTextureMode(target);
        RenderPaperBackground();

        if (screen == SCREEN_TITLE)
        {
            RenderHud(NULL, 0, true, false, false, false, false, false, uiMouse);
        }
        else
        {
            const LevelDef *level = GetLevelDef(levelIndex);
            bool showPlayButton = (screen == SCREEN_PLAYING);
            bool building = !PhysicsIsSimulating(&physics);

            BeginMode2D(camera); // world space: pans/zooms, HUD below does not
                RenderTiledLevel(GetTiledLevel(levelIndex)); // tile art + no-build overlay
                if (building) RenderGhostTrail(&physics);    // last run, build phase only
                RenderBoostLines(&physics);
                RenderPhysics(&physics);
                RenderSketchPreview(&sketch);
                RenderCannons(&physics);
                RenderCannonPreview(&sketch);
                RenderCheckpointFlag(&physics);
                RenderFinishLine(&physics.finishLine);
                RenderBall(PhysicsGetBallPos(&physics), physics.ballRadius, PhysicsGetBallAngle(&physics));

                if (debugMode)
                {
                    RenderPhysicsDebug(&physics, level);
                }
            EndMode2D();

            RenderHud(level->name, levelIndex, false, showPlayButton,
                      PhysicsIsSimulating(&physics), debugMode, levelMenuOpen,
                      physics.checkpointSet, uiMouse);
            if ((screen == SCREEN_PLAYING) && building)
            {
                BuildTool tools[TOOL_COUNT];
                int toolCount = GetVisibleTools(tools);
                RenderToolBar(&physics, tools, toolCount, sketch.tool, uiMouse);
            }
            AdminDraw(&physics, uiMouse);

            if (screen == SCREEN_WIN)
            {
                bool hasNext = (levelIndex + 1) < GameGetLevelCount();
                if (winMenuShow) RenderWinMenu(CountActiveStrokes(), runTime, hasNext,
                                               winSolutionSaved, uiMouse);
                else RenderWinAdmireHint();
            }
            else if (screen == SCREEN_GAMEOVER)
            {
                RenderGameOverMenu(uiMouse);
            }
            else if ((screen == SCREEN_PLAYING) && pauseMenuOpen)
            {
                RenderPauseMenu(uiMouse);
            }
        }
    EndTextureMode();

    // The view texture matches the window aspect, so it fills the window exactly
    BeginDrawing();
        ClearBackground((Color){ 40, 35, 30, 255 });

        DrawTexturePro(target.texture,
            (Rectangle){ 0, 0, (float)target.texture.width, -(float)target.texture.height },
            (Rectangle){ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() },
            (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
    //----------------------------------------------------------------------------------
}

void GameUnload(void)
{
    for (int i = 0; i < tiledLevelCount; i++) TiledLevelUnload(&tiledLevels[i]);
    PhysicsShutdown(&physics);
    UnloadRenderTexture(target);
}
