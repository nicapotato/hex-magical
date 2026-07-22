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
void RenderStar(Vector2 pos, float radius, float time);
void RenderBall(Vector2 pos, float radius, float angle);
// uiMouse is the mouse position in game-canvas coordinates (letterbox-corrected)
void RenderHud(const char *levelName, int levelIndex, bool showWin, bool showTitle, bool showStart,
               bool debugMode, bool levelMenuOpen, Vector2 uiMouse);
Rectangle RenderGetStartButtonRect(void);
Rectangle RenderGetDebugButtonRect(void);
Rectangle RenderGetLevelMenuHeaderRect(void);
Rectangle RenderGetLevelMenuItemRect(int index);

#endif // RENDER_H
