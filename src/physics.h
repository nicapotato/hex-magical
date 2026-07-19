/*******************************************************************************************
*
*   physics.h - Box2D world wrapper for hex-magical
*
********************************************************************************************/

#ifndef PHYSICS_H
#define PHYSICS_H

#include "box2d/box2d.h"
#include "levels.h"
#include "raylib.h"

#include <stdbool.h>

#define MAX_DRAWN_BODIES 64
#define MAX_STROKE_POINTS 256
#define PHYSICS_HZ 60.0f
#define PHYSICS_SUBSTEPS 4
// Half-width of drawn crayon ink — capsules use this so colliders match the stroke
#define STROKE_PHYSICS_RADIUS 5.0f
#define MAX_STROKE_CAPSULES 96

typedef struct DrawnBody
{
    bool active;
    b2BodyId bodyId;
    Vector2 localPoints[MAX_STROKE_POINTS]; // stroke points in body-local space
    int pointCount;
    Color crayonColor;
} DrawnBody;

typedef struct PhysicsWorld
{
    b2WorldId worldId;
    bool valid;

    b2BodyId ballId;
    float ballRadius;

    Vector2 starPos;
    float starRadius;

    DrawnBody drawn[MAX_DRAWN_BODIES];
    int drawnCount;

    const StaticBox *staticBoxes;
    int staticBoxCount;

    float accumulator;
    bool simulating; // false = build phase (ball frozen, no gravity)
} PhysicsWorld;

void PhysicsInit(PhysicsWorld *phys);
void PhysicsShutdown(PhysicsWorld *phys);
void PhysicsLoadLevel(PhysicsWorld *phys, const LevelDef *level);
void PhysicsStartSimulation(PhysicsWorld *phys); // drop the ball / enable gravity
void PhysicsStep(PhysicsWorld *phys, float dt);
bool PhysicsIsSimulating(const PhysicsWorld *phys);

Vector2 PhysicsGetBallPos(const PhysicsWorld *phys);
float PhysicsGetBallAngle(const PhysicsWorld *phys);
bool PhysicsCheckWin(const PhysicsWorld *phys);

// Create a dynamic polygon/plank from world-space stroke points. Returns drawn index or -1.
int PhysicsCreateDrawnBody(PhysicsWorld *phys, const Vector2 *worldPoints, int count, Color color);

// Destroy drawn body under world-space point. Returns true if something was erased.
bool PhysicsEraseAtPoint(PhysicsWorld *phys, Vector2 worldPoint);

DrawnBody *PhysicsGetDrawn(PhysicsWorld *phys, int index);
b2Transform PhysicsGetBodyTransform(b2BodyId bodyId);

#endif // PHYSICS_H
