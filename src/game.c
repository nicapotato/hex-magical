/*******************************************************************************************
*
*   game.c - Crayon Physics clone: state machine, update, draw
*
********************************************************************************************/

#include "game.h"
#include "levels.h"
#include "physics.h"
#include "render.h"
#include "sketch.h"

#include "raylib.h"

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

//----------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------
static Vector2 GetWorldMouse(void)
{
    // Game renders 1:1 into a GAME_SCREEN render texture currently
    return GetMousePosition();
}

static void LoadCurrentLevel(void)
{
    PhysicsLoadLevel(&physics, &LEVELS[levelIndex]);
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
    if (levelIndex >= LEVEL_COUNT)
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
    if (building && CheckCollisionPointRec(worldMouse, RenderGetStartButtonRect())) return true;
    return false;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void GameInit(void)
{
    target = LoadRenderTexture(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    PhysicsInit(&physics);
    SketchInit(&sketch);
    screen = SCREEN_TITLE;
    levelIndex = 0;
    animTime = 0.0f;
    debugMode = false;
}

void GameUpdateDrawFrame(void)
{
    float dt = GetFrameTime();
    animTime += dt;

    Vector2 worldMouse = GetWorldMouse();
    bool lmbDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool lmbPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool rmbPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    // Update
    //----------------------------------------------------------------------------------
    if (screen == SCREEN_TITLE)
    {
        if (IsKeyPressed(KEY_SPACE) || lmbPressed)
        {
            StartPlaying();
        }
        else if (IsKeyPressed(KEY_ONE)) { levelIndex = 0; StartPlaying(); }
        else if (IsKeyPressed(KEY_TWO)) { levelIndex = 1; StartPlaying(); }
        else if (IsKeyPressed(KEY_THREE)) { levelIndex = 2; StartPlaying(); }
    }
    else if ((screen == SCREEN_PLAYING) || (screen == SCREEN_WIN))
    {
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
        if (IsKeyPressed(KEY_R))
        {
            LoadCurrentLevel();
        }
        else if (IsKeyPressed(KEY_ONE)) { levelIndex = 0; LoadCurrentLevel(); }
        else if (IsKeyPressed(KEY_TWO)) { levelIndex = 1; LoadCurrentLevel(); }
        else if (IsKeyPressed(KEY_THREE)) { levelIndex = 2; LoadCurrentLevel(); }
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
            RenderHud(NULL, 0, false, true, false, false);
        }
        else
        {
            const LevelDef *level = &LEVELS[levelIndex];
            bool showStart = (screen == SCREEN_PLAYING) && !PhysicsIsSimulating(&physics);
            RenderLevelStatics(level);
            RenderPhysics(&physics);
            RenderSketchPreview(&sketch);
            RenderStar(physics.starPos, physics.starRadius, animTime);
            RenderBall(PhysicsGetBallPos(&physics), physics.ballRadius, PhysicsGetBallAngle(&physics));

            if (debugMode)
            {
                RenderPhysicsDebug(&physics, level);
            }

            RenderHud(level->name, levelIndex, screen == SCREEN_WIN, false, showStart, debugMode);
        }

        DrawRectangleLinesEx((Rectangle){ 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT }, 4, (Color){ 90, 60, 40, 180 });
    EndTextureMode();

    BeginDrawing();
        ClearBackground((Color){ 40, 35, 30, 255 });

        DrawTexturePro(target.texture,
            (Rectangle){ 0, 0, (float)target.texture.width, -(float)target.texture.height },
            (Rectangle){ 0, 0, (float)GAME_SCREEN_WIDTH, (float)GAME_SCREEN_HEIGHT },
            (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
    //----------------------------------------------------------------------------------
}

void GameUnload(void)
{
    PhysicsShutdown(&physics);
    UnloadRenderTexture(target);
}
