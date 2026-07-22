/*******************************************************************************************
*
*   platform.h - Platform-specific macros and abstractions
*
*   Isolates the #if defined(PLATFORM_WEB) branching (Emscripten main loop, logging, etc.)
*   so main.c and game.c can stay platform-agnostic.
*
********************************************************************************************/

#ifndef PLATFORM_H
#define PLATFORM_H

// Simple log system to avoid printf() calls if required
// NOTE: Avoiding those calls, also avoids const strings memory usage
#define SUPPORT_LOG_INFO
#if defined(SUPPORT_LOG_INFO)
    #define LOG(...) printf(__VA_ARGS__)
#else
    #define LOG(...)
#endif

// Apply platform-specific startup configuration (e.g. trace log verbosity)
void PlatformInit(void);

// Flush browser-backed persistent files after a save/delete. This is a no-op
// on desktop, where stdio writes are already durable.
void PlatformSyncFiles(void);

// Run the main game loop using whichever mechanism the target platform requires:
// Emscripten's asynchronous browser loop on Web, a blocking while loop everywhere else
void PlatformRunLoop(void (*updateDrawFrame)(void));

#endif // PLATFORM_H
