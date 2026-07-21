/*******************************************************************************************
*
*   admin.h - Admin panel: live physics tunables (ball weight, bounciness, drop force)
*
********************************************************************************************/

#ifndef ADMIN_H
#define ADMIN_H

#include "physics.h"
#include "raylib.h"

#include <stdbool.h>

typedef enum AdminAction
{
    ADMIN_ACTION_NONE = 0,  // input was not over admin UI
    ADMIN_ACTION_CONSUMED,  // admin UI handled the click/drag
    ADMIN_ACTION_RESPAWN    // user clicked RESPAWN — reload the level
} AdminAction;

bool AdminIsOpen(void);
Rectangle AdminGetButtonRect(void);

// Process mouse input for the ADMIN button + panel. Mouse is in game-canvas coords.
AdminAction AdminHandleInput(PhysicsWorld *phys, Vector2 mouse, bool lmbDown, bool lmbPressed);

// Draw the ADMIN button and, when open, the tunables panel
void AdminDraw(const PhysicsWorld *phys, Vector2 mouse);

#endif // ADMIN_H
