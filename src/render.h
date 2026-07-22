/*******************************************************************************************
*
*   render.h - Crayon-on-paper drawing
*
********************************************************************************************/

#ifndef RENDER_H
#define RENDER_H

#include "physics.h"
#include "sketch.h"
#include "levels.h"
#include "raylib.h"

#include <stdbool.h>

void RenderPaperBackground(void);
void RenderPhysics(PhysicsWorld *phys);
void RenderPhysicsDebug(PhysicsWorld *phys, const LevelDef *level);
void RenderSketchPreview(const SketchState *sketch);
void RenderFinishLine(Rectangle bounds);
void RenderBall(Vector2 pos, float radius, float angle);
// uiMouse is the mouse position in game-canvas coordinates (letterbox-corrected).
// showPlayButton shows the START/STOP toggle; simulating selects its label.
void RenderHud(const char *levelName, int levelIndex, bool showTitle, bool showPlayButton,
               bool simulating, bool debugMode, bool levelMenuOpen, Vector2 uiMouse);
Rectangle RenderGetStartButtonRect(void);
Rectangle RenderGetDebugButtonRect(void);
Rectangle RenderGetLevelMenuHeaderRect(void);
Rectangle RenderGetLevelMenuItemRect(int index);

// Level-complete menu (chlorostitch style): stats + Admire / Restart / Next / Quit.
// Button order matches RenderGetWinMenuButtonRect indices; when hasNext is false
// there are 3 buttons and index 2 is "Quit to title".
void RenderWinMenu(int strokeCount, float runTime, bool hasNext, Vector2 uiMouse);
void RenderWinAdmireHint(void); // shown while the menu is hidden to admire the run
Rectangle RenderGetWinMenuButtonRect(int index, int buttonCount);

#endif // RENDER_H
