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
#include <stdio.h>

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

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void RenderPaperBackground(void)
{
    ClearBackground(PAPER);

    // Subtle paper grain lines across the full (aspect-dependent) view width
    Color grain = { 220, 210, 185, 40 };
    for (int y = 0; y < GAME_SCREEN_HEIGHT; y += 8)
    {
        DrawLine(0, y, GameGetViewWidth(), y, grain);
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
    for (int i = 0; i < level->polygonCount; i++)
    {
        const StaticPolygon *polygon = &level->polygons[i];
        for (int p = 0; p < polygon->pointCount; p++)
        {
            DrawLineEx(polygon->points[p],
                       polygon->points[(p + 1) % polygon->pointCount],
                       2.0f, (Color){ 80, 220, 80, 255 });
        }
    }

    // Drawn body collision shapes (cyan) — actual Box2D polygons/planks
    for (int i = 0; i < MAX_DRAWN_BODIES; i++)
    {
        if (!phys->drawn[i].active) continue;
        DrawDebugBodyShapes(phys->drawn[i].bodyId, (Color){ 40, 200, 255, 255 });
    }

    // Ball collision circle (magenta) from live Box2D transform
    DrawDebugBodyShapes(phys->ballId, (Color){ 255, 60, 200, 255 });

    // Finish-line win area
    for (int i = 0; i < phys->finishLine.pointCount; i++)
    {
        DrawLineEx(phys->finishLine.points[i],
                   phys->finishLine.points[(i + 1) % phys->finishLine.pointCount],
                   2.0f, (Color){ 255, 220, 40, 255 });
    }
}

void RenderSketchPreview(const SketchState *sketch)
{
    if (!sketch->drawing || (sketch->pointCount < 2)) return;
    DrawCrayonPolyline(sketch->points, sketch->pointCount, sketch->crayonColor, STROKE_PHYSICS_RADIUS * 2.0f);
}

void RenderFinishLine(const PolyZone *zone)
{
    Color fill = STAR_YELLOW;
    fill.a = 60;

    // Fan fill + bold outline (zones from Tiled are convex-ish; outline is exact)
    for (int i = 1; i < zone->pointCount - 1; i++)
    {
        DrawTriangle(zone->points[0], zone->points[i + 1], zone->points[i], fill);
        DrawTriangle(zone->points[0], zone->points[i], zone->points[i + 1], fill);
    }
    for (int i = 0; i < zone->pointCount; i++)
    {
        DrawLineEx(zone->points[i], zone->points[(i + 1) % zone->pointCount], 3.0f, STAR_YELLOW);
    }
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
    return (Rectangle){ (float)GameGetViewWidth() - 80.0f - 8.0f, 12.0f, 80.0f, 40.0f };
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
    DrawText(fps, GameGetViewWidth() - fw - 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
}

// Horizontally centered text in the current view
static void DrawTextCentered(const char *text, int y, int fontSize, Color color)
{
    DrawText(text, (GameGetViewWidth() - MeasureText(text, fontSize)) / 2, y, fontSize, color);
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

void RenderHud(const char *levelName, int levelIndex, bool showTitle, bool showPlayButton,
               bool simulating, bool debugMode, bool levelMenuOpen, Vector2 uiMouse)
{
    if (showTitle)
    {
        DrawTextCentered("HEX MAGICAL", 200, 48, INK_BROWN);
        DrawTextCentered("a line rider crayon toy", 260, 22, CRAYON_BROWN);
        DrawTextCentered("Draw track under the ball, then send it to the star", 340, 18, INK_BROWN);
        DrawTextCentered("LMB draw   RMB erase   SPACE start/stop   R restart   WASD pan   +/- zoom", 380, 18, CRAYON_BROWN);
        DrawTextCentered("1 save solution   2 load   3 delete", 410, 18, CRAYON_BROWN);
        DrawTextCentered("Press SPACE or click to play", 480, 22, BALL_RED);
        DrawFpsIndicator();
        return;
    }

    Rectangle debugBtn = RenderGetDebugButtonRect();
    DrawUiButton(debugBtn, "DEBUG", debugMode, CheckCollisionPointRec(uiMouse, debugBtn));

    if (showPlayButton)
    {
        Rectangle btn = RenderGetStartButtonRect();
        DrawUiButton(btn, simulating ? "STOP" : "START", simulating, CheckCollisionPointRec(uiMouse, btn));
        if (simulating)
        {
            DrawText("Running — STOP or SPACE to go back to drawing", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
        }
        else
        {
            DrawText("Draw track, then START or SPACE   |   1 save solution  2 load  3 delete", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
        }
    }
    else
    {
        DrawText("LMB draw  RMB erase  R restart  F3 debug  [ ] level  WASD pan  +/- zoom", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
    }

    if (debugMode)
    {
        DrawText("debug: green=static  cyan=stroke capsules  magenta=ball  yellow=finish line",
                 16, 44, 14, (Color){ 40, 100, 40, 255 });
    }

    DrawFpsIndicator();

    // Drawn last so the open list overlaps the play field
    DrawLevelMenu(levelName, levelIndex, levelMenuOpen, uiMouse);
}

//----------------------------------------------------------------------------------
// Level-complete menu
//----------------------------------------------------------------------------------
#define WIN_MENU_W 380.0f
#define WIN_MENU_PAD 20.0f
#define WIN_MENU_FONT 18
#define WIN_MENU_TITLE_FONT 32
#define WIN_MENU_STAT_H 26.0f
#define WIN_MENU_BTN_H 40.0f
#define WIN_MENU_BTN_GAP 10.0f

static float WinMenuPanelHeight(int buttonCount)
{
    return WIN_MENU_PAD * 2.0f + (float)WIN_MENU_TITLE_FONT + 16.0f
         + WIN_MENU_STAT_H * 2.0f + 20.0f
         + (float)buttonCount * WIN_MENU_BTN_H + (float)(buttonCount - 1) * WIN_MENU_BTN_GAP;
}

static Rectangle WinMenuPanelRect(int buttonCount)
{
    float h = WinMenuPanelHeight(buttonCount);
    return (Rectangle){
        ((float)GameGetViewWidth() - WIN_MENU_W) * 0.5f,
        ((float)GAME_SCREEN_HEIGHT - h) * 0.5f,
        WIN_MENU_W,
        h
    };
}

Rectangle RenderGetWinMenuButtonRect(int index, int buttonCount)
{
    Rectangle panel = WinMenuPanelRect(buttonCount);
    float totalH = (float)buttonCount * WIN_MENU_BTN_H + (float)(buttonCount - 1) * WIN_MENU_BTN_GAP;
    float startY = panel.y + panel.height - WIN_MENU_PAD - totalH;
    return (Rectangle){
        panel.x + WIN_MENU_PAD,
        startY + (float)index * (WIN_MENU_BTN_H + WIN_MENU_BTN_GAP),
        panel.width - WIN_MENU_PAD * 2.0f,
        WIN_MENU_BTN_H
    };
}

static void DrawWinMenuStat(Rectangle panel, int row, const char *label, const char *value)
{
    float y = panel.y + WIN_MENU_PAD + (float)WIN_MENU_TITLE_FONT + 16.0f + (float)row * WIN_MENU_STAT_H;
    DrawText(label, (int)(panel.x + WIN_MENU_PAD), (int)y, WIN_MENU_FONT, CRAYON_BROWN);
    int vw = MeasureText(value, WIN_MENU_FONT);
    DrawText(value, (int)(panel.x + panel.width - WIN_MENU_PAD - (float)vw), (int)y, WIN_MENU_FONT, INK_BROWN);
}

void RenderWinMenu(int strokeCount, float runTime, bool hasNext, bool solutionSaved, Vector2 uiMouse)
{
    // Dim the scene but keep it visible — the run continues behind the panel
    DrawRectangle(0, 0, GameGetViewWidth(), GAME_SCREEN_HEIGHT, (Color){ 60, 45, 30, 140 });

    int buttonCount = hasNext ? 5 : 4;
    Rectangle panel = WinMenuPanelRect(buttonCount);
    DrawRectangleRec(panel, (Color){ 245, 236, 214, 250 });
    DrawRectangleLinesEx(panel, 3.0f, INK_BROWN);
    DrawText("You did it!", (int)(panel.x + WIN_MENU_PAD), (int)(panel.y + WIN_MENU_PAD),
             WIN_MENU_TITLE_FONT, BALL_RED);

    char timeBuf[16];
    int mins = (int)(runTime / 60.0f);
    int secs = (int)runTime % 60;
    int tenths = (int)(runTime * 10.0f) % 10;
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%d", mins, secs, tenths);

    DrawWinMenuStat(panel, 0, "Track drawn", TextFormat("%d stroke%s", strokeCount, (strokeCount == 1) ? "" : "s"));
    DrawWinMenuStat(panel, 1, "Run time", timeBuf);

    const char *labels[5];
    labels[0] = "Admire creation";
    labels[1] = solutionSaved ? "Solution saved!" : "Save as solution";
    labels[2] = "Restart level";
    labels[3] = hasNext ? "Next level" : "Quit to title";
    labels[4] = "Quit to title";

    for (int i = 0; i < buttonCount; i++)
    {
        Rectangle btn = RenderGetWinMenuButtonRect(i, buttonCount);
        DrawUiButton(btn, labels[i], false, CheckCollisionPointRec(uiMouse, btn));
    }

    const char *hint = "H admire   S save   R restart   N next   Q quit";
    int hw = MeasureText(hint, 14);
    DrawText(hint, (GameGetViewWidth() - hw) / 2, (int)(panel.y + panel.height + 12), 14, PAPER);
}

void RenderWinAdmireHint(void)
{
    DrawTextCentered("Admiring your creation — H or ESC to reopen the menu",
                     GAME_SCREEN_HEIGHT - 56, 16, BALL_RED);
}

void RenderGameOverMenu(Vector2 uiMouse)
{
    DrawRectangle(0, 0, GameGetViewWidth(), GAME_SCREEN_HEIGHT, (Color){ 60, 45, 30, 140 });

    const int buttonCount = 3;
    Rectangle panel = WinMenuPanelRect(buttonCount);
    DrawRectangleRec(panel, (Color){ 245, 236, 214, 250 });
    DrawRectangleLinesEx(panel, 3.0f, INK_BROWN);
    DrawText("Into the pit!", (int)(panel.x + WIN_MENU_PAD), (int)(panel.y + WIN_MENU_PAD),
             WIN_MENU_TITLE_FONT, BALL_RED);
    DrawText("The ball fell into a pit — game over.",
             (int)(panel.x + WIN_MENU_PAD),
             (int)(panel.y + WIN_MENU_PAD + (float)WIN_MENU_TITLE_FONT + 16.0f),
             WIN_MENU_FONT, CRAYON_BROWN);

    const char *labels[3] = { "Try again", "Restart level", "Quit to title" };
    for (int i = 0; i < buttonCount; i++)
    {
        Rectangle btn = RenderGetWinMenuButtonRect(i, buttonCount);
        DrawUiButton(btn, labels[i], false, CheckCollisionPointRec(uiMouse, btn));
    }

    const char *hint = "ENTER try again   R restart   Q quit";
    int hw = MeasureText(hint, 14);
    DrawText(hint, (GameGetViewWidth() - hw) / 2, (int)(panel.y + panel.height + 12), 14, PAPER);
}
