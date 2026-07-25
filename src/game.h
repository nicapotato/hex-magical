/*******************************************************************************************
*
*   game.h - Game logic: state, frame update and draw
*
********************************************************************************************/

#ifndef GAME_H
#define GAME_H

#include <stdbool.h>

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define GAME_SCREEN_WIDTH  720
#define GAME_SCREEN_HEIGHT 720

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
typedef enum {
    SCREEN_TITLE = 0,
    SCREEN_PLAYING,
    SCREEN_WIN,
    SCREEN_GAMEOVER // ball fell into a pit
} GameScreen;

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
void GameInit(void);              // Load resources / initialize game state
void GameUpdateDrawFrame(void);   // Update and draw one frame
void GameUnload(void);            // Unload resources / free game state

// Level registry: every act-<n>/map-<m>.tmx map found in resources/
int GameGetLevelCount(void);
const char *GameGetLevelName(int index);

// Act registry: distinct act-<n> folders holding the levels, ascending.
// actIndex is a registry index (0..count-1); the act *number* is what the
// folder is named (act-1 -> 1). GameGetActLevel maps a slot within an act
// back to a global level index.
int GameGetActCount(void);
int GameGetActNumber(int actIndex);
int GameGetLevelActIndex(int levelIndex);
int GameGetActLevelCount(int actIndex);
int GameGetActLevel(int actIndex, int slot);

// Resources dir feeding the level registry. GameSetResourcesDir rescans the
// new folder and swaps the registry; on failure (no loadable .tmx) the
// current levels are kept and false is returned.
const char *GameGetResourcesDir(void);
bool GameSetResourcesDir(const char *dir);

// Current view width in game pixels — follows the window aspect so wide/fullscreen
// windows see more world horizontally. View height is always GAME_SCREEN_HEIGHT.
int GameGetViewWidth(void);

#endif // GAME_H
