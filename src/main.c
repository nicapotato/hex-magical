
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
#if !defined(PLATFORM_WEB)
    // Windowed (not fullscreen): resizable, starts maximized to the monitor work area.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#else
    // RESIZABLE lets raylib sync GetScreenWidth/Height to the browser/itch iframe
    // viewport so the aspect-following view fills the whole embed (see shell.html).
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
#endif

    InitWindow(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, "Hex Magical");

#if !defined(PLATFORM_WEB)
    SetWindowMinSize(GAME_SCREEN_WIDTH / 2, GAME_SCREEN_HEIGHT / 2);
    MaximizeWindow(); // fill available monitor space; stays windowed with chrome
#endif

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
