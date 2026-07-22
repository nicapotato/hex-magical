/*******************************************************************************************
*
*   game.c - Crayon Physics clone: state machine, update, draw
*
********************************************************************************************/

#include "game.h"
#include "admin.h"
#include "levels.h"
#include "physics.h"
#include "render.h"
#include "sketch.h"
#include "tiled.h"

#include "raylib.h"

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
static float animTime = 0.0f;
static bool debugMode = false;
static bool levelMenuOpen = false;

#define MAX_TILED_LEVELS 8
static TiledLevel tiledLevels[MAX_TILED_LEVELS] = { 0 };
static int tiledLevelCount = 0;
static float tiledWatchTimer = 0.0f;

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

// Scan a resources dir for .tmx maps, sorted by filename for a stable level order
static void LoadTiledLevels(const char *dir)
{
    if (!DirectoryExists(dir)) return;

    FilePathList files = LoadDirectoryFilesEx(dir, ".tmx", false);

    // Insertion sort paths by name (LoadDirectoryFilesEx order is filesystem-dependent)
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

    for (unsigned int i = 0; (i < files.count) && (tiledLevelCount < MAX_TILED_LEVELS); i++)
    {
        if (TiledLevelLoad(&tiledLevels[tiledLevelCount], files.paths[i]))
        {
            tiledLevelCount++;
        }
    }

    UnloadDirectoryFiles(files);
}

// Letterbox the fixed game canvas into the current window.
static Rectangle GetGameDestRect(void)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float scale = fminf(sw / (float)GAME_SCREEN_WIDTH, sh / (float)GAME_SCREEN_HEIGHT);
    float dw = (float)GAME_SCREEN_WIDTH * scale;
    float dh = (float)GAME_SCREEN_HEIGHT * scale;
    return (Rectangle){ (sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh };
}

static Vector2 GetWorldMouse(void)
{
    Vector2 mouse = GetMousePosition();
    Rectangle dest = GetGameDestRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f) return mouse;

    return (Vector2){
        (mouse.x - dest.x) * ((float)GAME_SCREEN_WIDTH / dest.width),
        (mouse.y - dest.y) * ((float)GAME_SCREEN_HEIGHT / dest.height)
    };
}

static void LoadCurrentLevel(void)
{
    PhysicsLoadLevel(&physics, GetLevelDef(levelIndex));
    SketchInit(&sketch);
}

static void StartPlaying(void)
{
    screen = SCREEN_PLAYING;
    LoadCurrentLevel();
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

static bool WantsDropBall(bool lmbPressed, Vector2 worldMouse)
{
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) return true;
    if (lmbPressed && CheckCollisionPointRec(worldMouse, RenderGetStartButtonRect())) return true;
    return false;
}

static bool IsUiClick(Vector2 worldMouse, bool building)
{
    if (CheckCollisionPointRec(worldMouse, RenderGetDebugButtonRect())) return true;
    if (CheckCollisionPointRec(worldMouse, AdminGetButtonRect())) return true;
    if (CheckCollisionPointRec(worldMouse, RenderGetLevelMenuHeaderRect())) return true;
    if (building && CheckCollisionPointRec(worldMouse, RenderGetStartButtonRect())) return true;
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
    target = LoadRenderTexture(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    // Tiled levels are the only level source — every .tmx in resources/ joins the registry.
    // Candidate dirs cover: running from repo root (make run-mac) and from the binary dir.
    LoadTiledLevels("resources");
    if (tiledLevelCount == 0) LoadTiledLevels("../../resources");
    if (tiledLevelCount == 0)
    {
        // Without maps there is no game — abort loudly rather than limping along.
        // (fprintf, not TraceLog: platform.c sets LOG_NONE which would swallow it)
        fprintf(stderr, "FATAL: no loadable .tmx maps found in resources/ — nothing to play\n");
        abort();
    }

    PhysicsInit(&physics);
    SketchInit(&sketch);
    screen = SCREEN_TITLE;
    levelIndex = 0;
    animTime = 0.0f;
    debugMode = false;
    levelMenuOpen = false;
}

void GameUpdateDrawFrame(void)
{
    float dt = GetFrameTime();
    animTime += dt;

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

    Vector2 worldMouse = GetWorldMouse();
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
        // Admin UI gets first claim on the mouse
        AdminAction adminAction = AdminHandleInput(&physics, worldMouse, lmbDown, lmbPressed);
        if (adminAction == ADMIN_ACTION_RESPAWN)
        {
            screen = SCREEN_PLAYING;
            LoadCurrentLevel();
        }
        if (adminAction != ADMIN_ACTION_NONE)
        {
            lmbPressed = false;
            lmbDown = false;
        }

        // Levels dropdown
        if (lmbPressed)
        {
            if (CheckCollisionPointRec(worldMouse, RenderGetLevelMenuHeaderRect()))
            {
                levelMenuOpen = !levelMenuOpen;
                lmbPressed = false;
                lmbDown = false;
            }
            else if (levelMenuOpen)
            {
                for (int i = 0; i < GameGetLevelCount(); i++)
                {
                    if (CheckCollisionPointRec(worldMouse, RenderGetLevelMenuItemRect(i)))
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
        if (IsKeyPressed(KEY_D) ||
            (lmbPressed && CheckCollisionPointRec(worldMouse, RenderGetDebugButtonRect())))
        {
            debugMode = !debugMode;
            lmbPressed = false; // consume click so it doesn't draw / advance
        }
    }

    if (screen == SCREEN_PLAYING)
    {
        int selected = GetLevelSelectKey();
        int stepped = GetLevelStepKey();
        if (IsKeyPressed(KEY_R))
        {
            LoadCurrentLevel();
        }
        else if (selected >= 0) { levelIndex = selected; LoadCurrentLevel(); }
        else if (stepped >= 0) { levelIndex = stepped; LoadCurrentLevel(); }
        else
        {
            bool building = !PhysicsIsSimulating(&physics);
            bool uiClick = IsUiClick(worldMouse, building);

            // Build phase: draw freely; Enter / START drops the ball
            if (building && WantsDropBall(lmbPressed, worldMouse))
            {
                SketchCancel(&sketch);
                PhysicsStartSimulation(&physics);
            }
            else
            {
                bool sketchLmbPressed = lmbPressed && !uiClick;
                bool sketchLmbDown = lmbDown && !uiClick;

                // No-build zones: can't start a stroke inside, and a stroke that
                // wanders in is cancelled rather than spanning the zone
                if (TiledLevelNoBuildContains(GetTiledLevel(levelIndex), worldMouse))
                {
                    if (sketch.drawing) SketchCancel(&sketch);
                    sketchLmbPressed = false;
                    sketchLmbDown = false;
                }

                SketchUpdate(&sketch, &physics, worldMouse, sketchLmbDown, sketchLmbPressed, rmbPressed);
            }

            PhysicsStep(&physics, dt);

            if (PhysicsIsSimulating(&physics) && PhysicsCheckWin(&physics))
            {
                screen = SCREEN_WIN;
            }
        }
    }
    else if (screen == SCREEN_WIN)
    {
        if (IsKeyPressed(KEY_R))
        {
            StartPlaying();
        }
        else if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) ||
                 (lmbPressed && !CheckCollisionPointRec(worldMouse, RenderGetDebugButtonRect())))
        {
            AdvanceLevel();
        }
    }
    //----------------------------------------------------------------------------------

    // Draw
    //----------------------------------------------------------------------------------
    BeginTextureMode(target);
        RenderPaperBackground();

        if (screen == SCREEN_TITLE)
        {
            RenderHud(NULL, 0, false, true, false, false, false, worldMouse);
        }
        else
        {
            const LevelDef *level = GetLevelDef(levelIndex);
            bool showStart = (screen == SCREEN_PLAYING) && !PhysicsIsSimulating(&physics);
            RenderTiledLevel(GetTiledLevel(levelIndex)); // tile art + no-build overlay
            RenderPhysics(&physics);
            RenderSketchPreview(&sketch);
            RenderStar(physics.starPos, physics.starRadius, animTime);
            RenderBall(PhysicsGetBallPos(&physics), physics.ballRadius, PhysicsGetBallAngle(&physics));

            if (debugMode)
            {
                RenderPhysicsDebug(&physics, level);
            }

            RenderHud(level->name, levelIndex, screen == SCREEN_WIN, false, showStart, debugMode,
                      levelMenuOpen, worldMouse);
            AdminDraw(&physics, worldMouse);
        }

        DrawRectangleLinesEx((Rectangle){ 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT }, 4, (Color){ 90, 60, 40, 180 });
    EndTextureMode();

    BeginDrawing();
        ClearBackground((Color){ 40, 35, 30, 255 });

        DrawTexturePro(target.texture,
            (Rectangle){ 0, 0, (float)target.texture.width, -(float)target.texture.height },
            GetGameDestRect(),
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
