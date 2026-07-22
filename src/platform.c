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

#include <stdio.h>                          // Required for: printf(), popen()
#include <string.h>                         // Required for: strlen()

//--------------------------------------------------------------------------------------------
// Module Functions Definition
//--------------------------------------------------------------------------------------------
void PlatformInit(void)
{
#if !defined(_DEBUG)
    // Keep errors visible so TMX authoring mistakes (oversized maps, missing
    // properties, empty terrain) show up in the terminal instead of silently
    // dropping levels from the registry. Raylib INFO/DEBUG stay quiet.
    SetTraceLogLevel(LOG_ERROR);
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

bool PlatformPickFolder(char *out, int outSize)
{
#if defined(__APPLE__) && !defined(PLATFORM_WEB)
    // osascript drives the native NSOpenPanel — no extra dependency, works from
    // both a terminal-launched binary and a bundled .app. Blocks until closed.
    FILE *pipe = popen(
        "osascript -e 'POSIX path of (choose folder with prompt \"Select a resources folder (with .tmx maps + tileset)\")' 2>/dev/null",
        "r");
    if (pipe == NULL) return false;

    if (fgets(out, outSize, pipe) == NULL)
    {
        pclose(pipe);
        return false; // user cancelled (osascript exits non-zero, prints nothing)
    }
    pclose(pipe);

    // Trim the trailing newline and the trailing slash osascript appends
    size_t len = strlen(out);
    while ((len > 1) && ((out[len - 1] == '\n') || (out[len - 1] == '/')))
    {
        out[--len] = '\0';
    }
    return len > 0;
#else
    (void)out; (void)outSize;
    return false; // no native picker wired up on this platform
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
