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
void RenderFinishLine(const PolyZone *zone);
void RenderBall(Vector2 pos, float radius, float angle);

// World-space build elements: boost lines (directional dashes), cannons, the
// in-progress cannon aim preview, the last-run ghost trail and checkpoint flag
void RenderBoostLines(const PhysicsWorld *phys);
void RenderCannons(const PhysicsWorld *phys);
void RenderCannonPreview(const SketchState *sketch);
void RenderGhostTrail(const PhysicsWorld *phys);
void RenderCheckpointFlag(const PhysicsWorld *phys);

// uiMouse is the mouse position in game-canvas coordinates (letterbox-corrected).
// showPlayButton shows the START/STOP toggle; simulating selects its label.
// checkpointSet relabels START to show the run resumes from the flag.
void RenderHud(const char *levelName, int levelIndex, bool showTitle, bool showPlayButton,
               bool simulating, bool debugMode, bool levelMenuOpen, bool checkpointSet, Vector2 uiMouse);
Rectangle RenderGetStartButtonRect(void);
Rectangle RenderGetDebugButtonRect(void);
Rectangle RenderGetLevelMenuHeaderRect(void);
Rectangle RenderGetLevelMenuItemRect(int index);

// Build tool bar (bottom-left, build phase only). `tools` lists the visible
// tools in display order — zero-capacity resources are simply not in the list.
void RenderToolBar(const PhysicsWorld *phys, const BuildTool *tools, int toolCount,
                   BuildTool currentTool, Vector2 uiMouse);
Rectangle RenderGetToolButtonRect(int slot);

// Level-complete menu: stats + Admire / Save / Restart / Next / Quit.
void RenderWinMenu(int strokeCount, float runTime, bool hasNext, bool solutionSaved, Vector2 uiMouse);
void RenderWinAdmireHint(void); // shown while the menu is hidden to admire the run
Rectangle RenderGetWinMenuButtonRect(int index, int buttonCount);

// Game-over menu (ball fell into a pit): Try again / Restart / Quit — button
// rects come from RenderGetWinMenuButtonRect(i, 3).
void RenderGameOverMenu(Vector2 uiMouse);

// ESC pause menu: Resume / Restart level / Quit to title — world frozen while
// open. Button rects come from RenderGetWinMenuButtonRect(i, 3).
void RenderPauseMenu(Vector2 uiMouse);

#endif // RENDER_H
