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
    SCREEN_LOGO = 0,
    SCREEN_TITLE,
    SCREEN_GAMEPLAY,
    SCREEN_ENDING
} GameScreen;

// TODO: Define your custom data types here

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
void GameInit(void);              // Load resources / initialize game state
void GameUpdateDrawFrame(void);   // Update and draw one frame
void GameUnload(void);            // Unload resources / free game state

#endif // GAME_H
