/*******************************************************************************************
*
*   lighting.h - Sun light + cloud god-ray composite for sun-track levels
*
*   Screen-space passes (GLSL ES 100, desktop + WASM):
*     1) Cloud alpha mask (world-space sprites rendered into an RT)
*     2) Soft radial sun light + crepuscular rays through cloud openings
*     3) Soft cloud shadows projected away from the sun
*
********************************************************************************************/

#ifndef LIGHTING_H
#define LIGHTING_H

#include "raylib.h"

#include <stdbool.h>

void LightingInit(void);
void LightingUnload(void);

// Recreate RTs when the view texture width changes.
void LightingEnsureSize(int viewW, int viewH);

// Cloud mask: call while Mode2D is active (same camera as the scene).
// Clears to transparent, then caller draws cloud sprites (alpha matters).
void LightingBeginCloudMask(void);
void LightingEndCloudMask(void);

// Screen-space composite onto the current render target (call outside Mode2D).
// sunScreen is in view-texture pixels; sunIntensity 0..1; night 0..1.
void LightingApply(Vector2 sunScreen, float sunIntensity, float night);

bool LightingReady(void);

#endif // LIGHTING_H
