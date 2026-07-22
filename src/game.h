/*******************************************************************************************
*
*   game.h - Game logic: state, frame update and draw
*
********************************************************************************************/

#ifndef GAME_H
#define GAME_H

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

// Level registry: every Tiled .tmx map found in resources/
int GameGetLevelCount(void);
const char *GameGetLevelName(int index);

// Current view width in game pixels — follows the window aspect so wide/fullscreen
// windows see more world horizontally. View height is always GAME_SCREEN_HEIGHT.
int GameGetViewWidth(void);

#endif // GAME_H
