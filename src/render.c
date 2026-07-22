/*******************************************************************************************
*
*   render.c - Crayon-on-paper aesthetic
*
********************************************************************************************/

#include "render.h"
#include "admin.h"
#include "game.h"

#include <math.h>
#include <stddef.h>

static const Color PAPER = { 245, 236, 214, 255 };
static const Color INK_BROWN = { 90, 60, 40, 255 };
static const Color CRAYON_BROWN = { 120, 80, 50, 255 };
static const Color BALL_RED = { 210, 50, 50, 255 };
static const Color STAR_YELLOW = { 240, 200, 40, 255 };

//----------------------------------------------------------------------------------
// Crayon stroke helpers
//----------------------------------------------------------------------------------
static void DrawCrayonPolyline(const Vector2 *points, int count, Color color, float thickness)
{
    if (count < 2) return;

    // Waxy overdraw: slightly offset passes for a crayon look
    for (int pass = 0; pass < 3; pass++)
    {
        float offset = (float)(pass - 1) * 1.2f;
        Color c = color;
        c.a = (unsigned char)(90 + pass * 50);

        for (int i = 0; i < count - 1; i++)
        {
            Vector2 a = points[i];
            Vector2 b = points[i + 1];
            // Deterministic jitter from index (no RNG needed)
            float jx = sinf((float)i * 12.9898f + (float)pass * 3.1f) * 1.5f;
            float jy = cosf((float)i * 78.233f + (float)pass * 2.7f) * 1.5f;
            a.x += offset + jx;
            a.y += jy;
            b.x += offset + jx * 0.5f;
            b.y += jy * 0.5f;
            DrawLineEx(a, b, thickness, c);
        }
    }
}

static void DrawCrayonBox(float cx, float cy, float halfW, float halfH, float angleDeg, Color color)
{
    float rad = angleDeg * DEG2RAD;
    float c = cosf(rad);
    float s = sinf(rad);

    Vector2 corners[5];
    float hx[4] = { -halfW, halfW, halfW, -halfW };
    float hy[4] = { -halfH, -halfH, halfH, halfH };
    for (int i = 0; i < 4; i++)
    {
        corners[i].x = cx + hx[i] * c - hy[i] * s;
        corners[i].y = cy + hx[i] * s + hy[i] * c;
    }
    corners[4] = corners[0];
    DrawCrayonPolyline(corners, 5, color, 3.5f);

    // Soft fill
    Color fill = color;
    fill.a = 40;
    DrawTriangle(corners[0], corners[1], corners[2], fill);
    DrawTriangle(corners[0], corners[2], corners[3], fill);
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void RenderPaperBackground(void)
{
    ClearBackground(PAPER);

    // Subtle paper grain lines
    Color grain = { 220, 210, 185, 40 };
    for (int y = 0; y < GAME_SCREEN_HEIGHT; y += 8)
    {
        DrawLine(0, y, GAME_SCREEN_WIDTH, y, grain);
    }
}

void RenderLevelStatics(const LevelDef *level)
{
    for (int i = 0; i < level->boxCount; i++)
    {
        const StaticBox *box = &level->boxes[i];
        DrawCrayonBox(box->x, box->y, box->halfWidth, box->halfHeight, box->angleDeg, CRAYON_BROWN);
    }
}

void RenderPhysics(PhysicsWorld *phys)
{
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        DrawnBody *drawn = &phys->drawn[i];
        if (!drawn->active) continue;
        if (!b2Body_IsValid(drawn->bodyId)) continue;
        if (drawn->pointCount < 2) continue;

        b2Transform xf = PhysicsGetBodyTransform(drawn->bodyId);
        Vector2 worldPts[MAX_STROKE_POINTS];
        for (int p = 0; p < drawn->pointCount; p++)
        {
            b2Vec2 local = { drawn->localPoints[p].x, drawn->localPoints[p].y };
            b2Vec2 world = b2TransformPoint(xf, local);
            worldPts[p] = (Vector2){ world.x, world.y };
        }
        DrawCrayonPolyline(worldPts, drawn->pointCount, drawn->crayonColor, STROKE_PHYSICS_RADIUS * 2.0f);
    }
}

static void DrawDebugCross(Vector2 pos, float size, Color color)
{
    DrawLineEx((Vector2){ pos.x - size, pos.y }, (Vector2){ pos.x + size, pos.y }, 2.0f, color);
    DrawLineEx((Vector2){ pos.x, pos.y - size }, (Vector2){ pos.x, pos.y + size }, 2.0f, color);
}

static void DrawDebugRotatedBox(float cx, float cy, float halfW, float halfH, float angleDeg, Color color)
{
    float rad = angleDeg * DEG2RAD;
    float c = cosf(rad);
    float s = sinf(rad);
    Vector2 corners[5];
    float hx[4] = { -halfW, halfW, halfW, -halfW };
    float hy[4] = { -halfH, -halfH, halfH, halfH };
    for (int i = 0; i < 4; i++)
    {
        corners[i].x = cx + hx[i] * c - hy[i] * s;
        corners[i].y = cy + hx[i] * s + hy[i] * c;
    }
    corners[4] = corners[0];
    for (int i = 0; i < 4; i++)
    {
        DrawLineEx(corners[i], corners[i + 1], 2.0f, color);
    }
}

static void DrawDebugCapsule(b2Transform xf, b2Capsule capsule, Color color)
{
    b2Vec2 c1 = b2TransformPoint(xf, capsule.center1);
    b2Vec2 c2 = b2TransformPoint(xf, capsule.center2);
    Vector2 a = { c1.x, c1.y };
    Vector2 b = { c2.x, c2.y };
    float r = capsule.radius;

    DrawCircleLinesV(a, r, color);
    DrawCircleLinesV(b, r, color);

    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len * r;
    float ny = dx / len * r;
    DrawLineEx((Vector2){ a.x + nx, a.y + ny }, (Vector2){ b.x + nx, b.y + ny }, 2.0f, color);
    DrawLineEx((Vector2){ a.x - nx, a.y - ny }, (Vector2){ b.x - nx, b.y - ny }, 2.0f, color);
}

static void DrawDebugBodyShapes(b2BodyId bodyId, Color color)
{
    if (!b2Body_IsValid(bodyId)) return;

    b2Transform xf = b2Body_GetTransform(bodyId);
    // Stroke bodies can have many capsules
    b2ShapeId shapes[128];
    int capacity = (int)(sizeof(shapes) / sizeof(shapes[0]));
    int count = b2Body_GetShapes(bodyId, shapes, capacity);

    for (int s = 0; s < count; s++)
    {
        b2ShapeType type = b2Shape_GetType(shapes[s]);
        if (type == b2_circleShape)
        {
            b2Circle circle = b2Shape_GetCircle(shapes[s]);
            b2Vec2 center = b2TransformPoint(xf, circle.center);
            DrawCircleLinesV((Vector2){ center.x, center.y }, circle.radius, color);
            DrawDebugCross((Vector2){ center.x, center.y }, 6.0f, color);
        }
        else if (type == b2_capsuleShape)
        {
            DrawDebugCapsule(xf, b2Shape_GetCapsule(shapes[s]), color);
        }
        else if (type == b2_polygonShape)
        {
            b2Polygon poly = b2Shape_GetPolygon(shapes[s]);
            for (int i = 0; i < poly.count; i++)
            {
                b2Vec2 a = b2TransformPoint(xf, poly.vertices[i]);
                b2Vec2 b = b2TransformPoint(xf, poly.vertices[(i + 1) % poly.count]);
                DrawLineEx((Vector2){ a.x, a.y }, (Vector2){ b.x, b.y }, 2.0f, color);
            }
        }
    }

    DrawDebugCross((Vector2){ xf.p.x, xf.p.y }, 5.0f, color);
}

void RenderPhysicsDebug(PhysicsWorld *phys, const LevelDef *level)
{
    if (!phys->valid || (level == NULL)) return;

    // Static level geometry (green) — same boxes used to create Box2D statics
    for (int i = 0; i < level->boxCount; i++)
    {
        const StaticBox *box = &level->boxes[i];
        DrawDebugRotatedBox(box->x, box->y, box->halfWidth, box->halfHeight, box->angleDeg,
                            (Color){ 80, 220, 80, 255 });
    }

    // Drawn body collision shapes (cyan) — actual Box2D polygons/planks
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        if (!phys->drawn[i].active) continue;
        DrawDebugBodyShapes(phys->drawn[i].bodyId, (Color){ 40, 200, 255, 255 });
    }

    // Ball collision circle (magenta) from live Box2D transform
    DrawDebugBodyShapes(phys->ballId, (Color){ 255, 60, 200, 255 });

    // Star win radius (yellow dashed-ish ring)
    DrawCircleLinesV(phys->starPos, phys->starRadius, (Color){ 255, 220, 40, 255 });
    DrawDebugCross(phys->starPos, 8.0f, (Color){ 255, 220, 40, 255 });
}

void RenderSketchPreview(const SketchState *sketch)
{
    if (!sketch->drawing || (sketch->pointCount < 2)) return;
    DrawCrayonPolyline(sketch->points, sketch->pointCount, sketch->crayonColor, STROKE_PHYSICS_RADIUS * 2.0f);
}

void RenderStar(Vector2 pos, float radius, float time)
{
    float pulse = 1.0f + 0.08f * sinf(time * 4.0f);
    float r = radius * pulse;
    Vector2 pts[11];
    for (int i = 0; i < 10; i++)
    {
        float a = -PI * 0.5f + (float)i * (PI / 5.0f);
        float rr = (i % 2 == 0)? r : r * 0.45f;
        pts[i].x = pos.x + cosf(a) * rr;
        pts[i].y = pos.y + sinf(a) * rr;
    }
    pts[10] = pts[0];
    DrawCrayonPolyline(pts, 11, STAR_YELLOW, 3.0f);

    Color fill = STAR_YELLOW;
    fill.a = 90;
    DrawCircleV(pos, r * 0.35f, fill);
}

void RenderBall(Vector2 pos, float radius, float angle)
{
    // Filled crayon ball with a highlight arc
    Color fill = BALL_RED;
    fill.a = 180;
    DrawCircleV(pos, radius, fill);

    // Outline as short crayon arcs
    Vector2 ring[17];
    for (int i = 0; i <= 16; i++)
    {
        float a = (float)i * (2.0f * PI / 16.0f);
        ring[i].x = pos.x + cosf(a) * radius;
        ring[i].y = pos.y + sinf(a) * radius;
    }
    DrawCrayonPolyline(ring, 17, BALL_RED, 3.0f);

    // Spin mark
    Vector2 markA = {
        pos.x + cosf(angle) * radius * 0.55f,
        pos.y + sinf(angle) * radius * 0.55f
    };
    DrawCircleV(markA, 3.0f, RAYWHITE);
}

Rectangle RenderGetDebugButtonRect(void)
{
    return (Rectangle){ (float)GAME_SCREEN_WIDTH - 80.0f - 8.0f, 12.0f, 80.0f, 40.0f };
}

Rectangle RenderGetStartButtonRect(void)
{
    // Sit left of ADMIN so START | ADMIN | DEBUG line up in the top-right
    Rectangle admin = AdminGetButtonRect();
    return (Rectangle){ admin.x - 164.0f - 8.0f, 12.0f, 164.0f, 40.0f };
}

#define LEVEL_MENU_X 16.0f
#define LEVEL_MENU_Y 8.0f
#define LEVEL_MENU_W 230.0f
#define LEVEL_MENU_HEADER_H 30.0f
#define LEVEL_MENU_ROW_H 26.0f

Rectangle RenderGetLevelMenuHeaderRect(void)
{
    return (Rectangle){ LEVEL_MENU_X, LEVEL_MENU_Y, LEVEL_MENU_W, LEVEL_MENU_HEADER_H };
}

Rectangle RenderGetLevelMenuItemRect(int index)
{
    return (Rectangle){
        LEVEL_MENU_X,
        LEVEL_MENU_Y + LEVEL_MENU_HEADER_H + (float)index * LEVEL_MENU_ROW_H,
        LEVEL_MENU_W,
        LEVEL_MENU_ROW_H
    };
}

static void DrawLevelMenu(const char *levelName, int levelIndex, bool open, Vector2 uiMouse)
{
    Rectangle header = RenderGetLevelMenuHeaderRect();
    bool headerHover = CheckCollisionPointRec(uiMouse, header);

    DrawRectangleRec(header, headerHover ? (Color){ 235, 222, 190, 255 } : (Color){ 245, 236, 214, 235 });
    DrawRectangleLinesEx(header, 2.0f, INK_BROWN);
    const char *label = TextFormat("Level %d: %s  %s", levelIndex + 1, levelName, open ? "^" : "v");
    DrawText(label, (int)header.x + 8, (int)header.y + 6, 18, INK_BROWN);

    if (!open) return;

    for (int i = 0; i < GameGetLevelCount(); i++)
    {
        Rectangle item = RenderGetLevelMenuItemRect(i);
        bool hover = CheckCollisionPointRec(uiMouse, item);
        bool current = (i == levelIndex);

        Color fill = current ? (Color){ 210, 50, 50, 220 }
                   : hover ? (Color){ 235, 222, 190, 245 }
                   : (Color){ 245, 236, 214, 235 };
        DrawRectangleRec(item, fill);
        DrawRectangleLinesEx(item, 1.0f, CRAYON_BROWN);
        DrawText(TextFormat("%d. %s", i + 1, GameGetLevelName(i)),
                 (int)item.x + 8, (int)item.y + 5, 16,
                 current ? PAPER : INK_BROWN);
    }
}

static void DrawFpsIndicator(void)
{
    const char *fps = TextFormat("%d FPS", GetFPS());
    int fw = MeasureText(fps, 16);
    DrawText(fps, GAME_SCREEN_WIDTH - fw - 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
}

static void DrawUiButton(Rectangle btn, const char *label, bool active, bool hover)
{
    Color fill;
    Color border;
    Color text;
    if (active)
    {
        fill = (Color){ 60, 120, 60, 230 };
        border = (Color){ 80, 220, 80, 255 };
        text = (Color){ 180, 255, 180, 255 };
    }
    else if (hover)
    {
        fill = BALL_RED;
        border = INK_BROWN;
        text = PAPER;
    }
    else
    {
        fill = CRAYON_BROWN;
        border = INK_BROWN;
        text = PAPER;
    }

    DrawRectangleRec(btn, fill);
    DrawRectangleLinesEx(btn, 2.0f, border);
    int tw = MeasureText(label, 18);
    DrawText(label, (int)btn.x + ((int)btn.width - tw) / 2, (int)btn.y + 11, 18, text);
}

void RenderHud(const char *levelName, int levelIndex, bool showWin, bool showTitle, bool showStart,
               bool debugMode, bool levelMenuOpen, Vector2 uiMouse)
{
    if (showTitle)
    {
        DrawText("HEX MAGICAL", 160, 200, 48, INK_BROWN);
        DrawText("a crayon physics clone", 200, 260, 22, CRAYON_BROWN);
        DrawText("Draw under the ball, then drop it to the star", 120, 340, 18, INK_BROWN);
        DrawText("LMB draw   RMB erase   ENTER drop   R restart", 140, 380, 18, CRAYON_BROWN);
        DrawText("Press SPACE or click to play", 200, 480, 22, BALL_RED);
        DrawFpsIndicator();
        return;
    }

    Rectangle debugBtn = RenderGetDebugButtonRect();
    DrawUiButton(debugBtn, "DEBUG", debugMode, CheckCollisionPointRec(uiMouse, debugBtn));

    if (showStart)
    {
        Rectangle btn = RenderGetStartButtonRect();
        DrawUiButton(btn, "START", false, CheckCollisionPointRec(uiMouse, btn));
        DrawText("Draw a path, then START or ENTER to drop the ball", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
    }
    else
    {
        DrawText("LMB draw  RMB erase  R restart  D debug  [ ] level", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
    }

    if (debugMode)
    {
        DrawText("debug: green=static  cyan=stroke capsules  magenta=ball  yellow=star",
                 16, 44, 14, (Color){ 40, 100, 40, 255 });
    }

    if (showWin)
    {
        DrawRectangle(0, 280, GAME_SCREEN_WIDTH, 120, (Color){ 245, 236, 214, 200 });
        DrawText("You did it!", 240, 300, 40, BALL_RED);
        DrawText("Click or SPACE for next level", 180, 360, 20, INK_BROWN);
    }

    DrawFpsIndicator();

    // Drawn last so the open list overlaps the play field
    DrawLevelMenu(levelName, levelIndex, levelMenuOpen, uiMouse);
}
