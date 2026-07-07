/*******************************************************************************************
*
*   game.c - Game logic: state, frame update and draw
*
********************************************************************************************/

#include "game.h"
#include "raylib.h"

//----------------------------------------------------------------------------------
// Global Variables Definition (local to this module)
//----------------------------------------------------------------------------------
static RenderTexture2D target = { 0 };  // Render texture to render our game
static int frameCounter = 0;

// TODO: Define global variables here, recommended to make them static

//--------------------------------------------------------------------------------------------
// Module Functions Definition
//--------------------------------------------------------------------------------------------
void GameInit(void)
{
    // TODO: Load resources / Initialize variables at this point

    // Render texture to draw, enables screen scaling
    // NOTE: If screen is scaled, mouse input should be scaled proportionally
    target = LoadRenderTexture(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
}

void GameUpdateDrawFrame(void)
{
    // Update
    //----------------------------------------------------------------------------------
    // TODO: Update variables / Implement example logic at this point

    frameCounter++;
    //----------------------------------------------------------------------------------

    // Draw
    //----------------------------------------------------------------------------------
    // Render game screen to a texture,
    // it could be useful for scaling or further shader postprocessing
    BeginTextureMode(target);
        ClearBackground(RAYWHITE);

        // TODO: Draw your game screen here

        DrawRectangle(70, 90, 200, 200, BLACK);
        DrawRectangle(70 + 16, 90 + 16, 200 - 32, 200 - 32, RAYWHITE);
        DrawText("raylib", 70 + 200 - MeasureText("raylib", 40) - 32, 90 + 200 - 40 - 24, 40, BLACK);

        DrawText("6.x", 290, 90 - 26, 280, BLACK);
        DrawText("GAMEJAM", 70, 90 + 210, 120, MAROON);

        if ((frameCounter/20)%2) DrawText("are you ready?", 160, 500, 50, BLACK);

        DrawRectangleLinesEx((Rectangle){ 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT }, 16, BLACK);

    EndTextureMode();

    // Render to screen (main framebuffer)
    BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw render texture to screen, scaled if required
        DrawTexturePro(target.texture, (Rectangle){ 0, 0, (float)target.texture.width, -(float)target.texture.height },
            (Rectangle){ 0, 0, (float)target.texture.width, (float)target.texture.height }, (Vector2){ 0, 0 }, 0.0f, WHITE);

        // TODO: Draw everything that requires to be drawn at this point, maybe UI?

    EndDrawing();
    //----------------------------------------------------------------------------------
}

void GameUnload(void)
{
    UnloadRenderTexture(target);

    // TODO: Unload all loaded resources at this point
}
