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
static TiledLevel tiledLevel = { 0 };
static float tiledWatchTimer = 0.0f;

//----------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------
// Registry: built-in LEVELS, plus the Tiled level appended when its .tmx loaded
static const LevelDef *GetLevelDef(int index)
{
    if (index < LEVEL_COUNT) return &LEVELS[index];
    return &tiledLevel.def;
}

int GameGetLevelCount(void)
{
    return LEVEL_COUNT + (tiledLevel.loaded ? 1 : 0);
}

const char *GameGetLevelName(int index)
{
    return GetLevelDef(index)->name;
}

static bool IsTiledLevel(int index)
{
    return tiledLevel.loaded && (index >= LEVEL_COUNT);
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

    // Optional Tiled level — appended to the registry when the .tmx exists.
    // Candidates cover: running from repo root (make run-mac) and from the binary dir.
    {
        const char *candidates[] = { "resources/tile-map.tmx", "../../resources/tile-map.tmx" };
        for (int i = 0; i < 2; i++)
        {
            if (!FileExists(candidates[i])) continue;
            if (TiledLevelLoad(&tiledLevel, candidates[i])) break;
        }
        if (!tiledLevel.loaded)
        {
            TraceLog(LOG_WARNING, "TILED: resources/tile-map.tmx not loaded — running with built-in levels only");
        }
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

    // Hot reload: poll the .tmx every half second; rebuild if saved from Tiled
    tiledWatchTimer += dt;
    if (tiledLevel.loaded && (tiledWatchTimer >= 0.5f))
    {
        tiledWatchTimer = 0.0f;
        if (TiledLevelFileChanged(&tiledLevel) && TiledLevelLoad(&tiledLevel, tiledLevel.tmxPath))
        {
            if (IsTiledLevel(levelIndex) && (screen != SCREEN_TITLE))
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
            if (IsTiledLevel(levelIndex))
            {
                RenderTiledLevel(&tiledLevel); // tile art carries the visuals
            }
            else
            {
                RenderLevelStatics(level);
            }
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
    TiledLevelUnload(&tiledLevel);
    PhysicsShutdown(&physics);
    UnloadRenderTexture(target);
}
