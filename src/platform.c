/*******************************************************************************************
*
*   platform.c - Platform-specific macros and abstractions
*
********************************************************************************************/

#include "platform.h"
#include "raylib.h"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>      // Emscripten library
#endif

#include <stdio.h>                          // Required for: printf()

//--------------------------------------------------------------------------------------------
// Module Functions Definition
//--------------------------------------------------------------------------------------------
void PlatformInit(void)
{
#if !defined(_DEBUG)
    SetTraceLogLevel(LOG_NONE);         // Disable raylib trace log messages
#endif
}

void PlatformSyncFiles(void)
{
#if defined(PLATFORM_WEB)
    // /solutions is mounted as IDBFS by web_storage.js before main() runs.
    // syncfs is asynchronous; writes are already visible to this session.
    EM_ASM({
        FS.syncfs(false, function(error) {
            if (error) console.error("SOLUTION: browser persistence sync failed", error);
        });
    });
#endif
}

void PlatformRunLoop(void (*updateDrawFrame)(void))
{
#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(updateDrawFrame, 60, 1);
#else
    SetTargetFPS(60);     // Set our game frames-per-second

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button
    {
        updateDrawFrame();
    }
#endif
}
