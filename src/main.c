
#include "raylib.h"
#include "platform.h"
#include "game.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    PlatformInit();

    // Initialization
    //--------------------------------------------------------------------------------------
    InitWindow(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, "raylib gamejam template");

    GameInit();
    //--------------------------------------------------------------------------------------

    PlatformRunLoop(GameUpdateDrawFrame);

    // De-Initialization
    //--------------------------------------------------------------------------------------
    GameUnload();

    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}
