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
static float viewZoom = 1.0f;
static Vector2 viewPan = { 0.0f, 0.0f }; // camera target offset from level center

#define VIEW_ZOOM_MIN 0.5f
#define VIEW_ZOOM_MAX 2.5f
#define VIEW_ZOOM_RATE 1.5f // exponential zoom speed per second while +/- held
#define VIEW_PAN_SPEED 480.0f // world pixels per second at 1x zoom

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

static bool WantsDropBall(bool lmbPressed, Vector2 uiMouse)
{
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) return true;
    if (lmbPressed && CheckCollisionPointRec(uiMouse, RenderGetStartButtonRect())) return true;
    return false;
}

static bool IsUiClick(Vector2 uiMouse, bool building)
{
    if (CheckCollisionPointRec(uiMouse, RenderGetDebugButtonRect())) return true;
    if (CheckCollisionPointRec(uiMouse, AdminGetButtonRect())) return true;
    if (CheckCollisionPointRec(uiMouse, RenderGetLevelMenuHeaderRect())) return true;
    if (building && CheckCollisionPointRec(uiMouse, RenderGetStartButtonRect())) return true;
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
        // Admin UI gets first claim on the mouse
        AdminAction adminAction = AdminHandleInput(&physics, uiMouse, lmbDown, lmbPressed);
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
            bool uiClick = IsUiClick(uiMouse, building);

            // Build phase: draw freely; Enter / START drops the ball
            if (building && WantsDropBall(lmbPressed, uiMouse))
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
                 (lmbPressed && !CheckCollisionPointRec(uiMouse, RenderGetDebugButtonRect())))
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
            RenderHud(NULL, 0, false, true, false, false, false, uiMouse);
        }
        else
        {
            const LevelDef *level = GetLevelDef(levelIndex);
            bool showStart = (screen == SCREEN_PLAYING) && !PhysicsIsSimulating(&physics);

            BeginMode2D(camera); // world space: pans/zooms, HUD below does not
                RenderTiledLevel(GetTiledLevel(levelIndex)); // tile art + no-build overlay
                RenderPhysics(&physics);
                RenderSketchPreview(&sketch);
                RenderStar(physics.starPos, physics.starRadius, animTime);
                RenderBall(PhysicsGetBallPos(&physics), physics.ballRadius, PhysicsGetBallAngle(&physics));

                if (debugMode)
                {
                    RenderPhysicsDebug(&physics, level);
                }

                // Playable canvas boundary (the Tiled walls live on these edges)
                DrawRectangleLinesEx((Rectangle){ 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT }, 4, (Color){ 90, 60, 40, 180 });
            EndMode2D();

            RenderHud(level->name, levelIndex, screen == SCREEN_WIN, false, showStart, debugMode,
                      levelMenuOpen, uiMouse);
            AdminDraw(&physics, uiMouse);
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
