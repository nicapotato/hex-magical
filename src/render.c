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
static const Color BOOST_ORANGE = { 240, 140, 40, 255 };
static const Color GHOST_BLUE = { 90, 90, 140, 255 };

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
    Color color = (sketch->tool == TOOL_BOOST_LINE) ? BOOST_ORANGE : sketch->crayonColor;
    DrawCrayonPolyline(sketch->points, sketch->pointCount, color, STROKE_PHYSICS_RADIUS * 2.0f);
}

//----------------------------------------------------------------------------------
// Build elements: boost lines, cannons, ghost trail, checkpoint flag
//----------------------------------------------------------------------------------
// Small chevron pointing along `dir` — used for boost line direction arrows
static void DrawDirectionArrow(Vector2 tip, Vector2 dir, float size, Color color)
{
    Vector2 back = { tip.x - dir.x * size, tip.y - dir.y * size };
    Vector2 normal = { -dir.y * size * 0.5f, dir.x * size * 0.5f };
    DrawLineEx((Vector2){ back.x + normal.x, back.y + normal.y }, tip, 3.0f, color);
    DrawLineEx((Vector2){ back.x - normal.x, back.y - normal.y }, tip, 3.0f, color);
}

void RenderBoostLines(const PhysicsWorld *phys)
{
    Color dash = BOOST_ORANGE;
    dash.a = 200;

    for (int l = 0; l < MAX_BOOST_LINES; l++)
    {
        const BoostLine *line = &phys->boostLines[l];
        if (!line->active || (line->pointCount < 2)) continue;

        // Dashed stroke: walk the polyline alternating ink and gap
        const float dashLen = 10.0f;
        const float gapLen = 7.0f;
        float phase = 0.0f;
        float sinceArrow = 0.0f;
        for (int i = 0; i < line->pointCount - 1; i++)
        {
            Vector2 a = line->points[i];
            Vector2 b = line->points[i + 1];
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float segLen = sqrtf(dx * dx + dy * dy);
            if (segLen < 0.001f) continue;
            Vector2 dir = { dx / segLen, dy / segLen };

            float t = 0.0f;
            while (t < segLen)
            {
                float cycle = dashLen + gapLen;
                float inCycle = fmodf(phase + t, cycle);
                float run = (inCycle < dashLen) ? (dashLen - inCycle) : (cycle - inCycle);
                float end = t + run;
                if (end > segLen) end = segLen;
                if (inCycle < dashLen)
                {
                    DrawLineEx((Vector2){ a.x + dir.x * t, a.y + dir.y * t },
                               (Vector2){ a.x + dir.x * end, a.y + dir.y * end }, 5.0f, dash);
                }
                t = end;
            }
            phase = fmodf(phase + segLen, dashLen + gapLen);

            // Direction arrows roughly every 60 px of stroke
            sinceArrow += segLen;
            if (sinceArrow >= 60.0f)
            {
                sinceArrow = 0.0f;
                DrawDirectionArrow(b, dir, 12.0f, BOOST_ORANGE);
            }
        }

        // Always mark the stroke end so short lines still read as directional
        Vector2 endA = line->points[line->pointCount - 2];
        Vector2 endB = line->points[line->pointCount - 1];
        float ex = endB.x - endA.x;
        float ey = endB.y - endA.y;
        float elen = sqrtf(ex * ex + ey * ey);
        if (elen > 0.001f) DrawDirectionArrow(endB, (Vector2){ ex / elen, ey / elen }, 14.0f, BOOST_ORANGE);
    }
}

// Placeholder cannon art: circle base + thick barrel, faint entry circle
static void DrawCannonShape(Vector2 pos, float angleRad, float alpha)
{
    Color body = INK_BROWN;
    body.a = (unsigned char)(255.0f * alpha);
    Color accent = BALL_RED;
    accent.a = (unsigned char)(220.0f * alpha);
    Color entry = BOOST_ORANGE;
    entry.a = (unsigned char)(70.0f * alpha);

    DrawCircleLinesV(pos, CANNON_ENTRY_RADIUS, entry);

    Vector2 muzzle = {
        pos.x + cosf(angleRad) * CANNON_BARREL_LENGTH,
        pos.y + sinf(angleRad) * CANNON_BARREL_LENGTH
    };
    DrawLineEx(pos, muzzle, 12.0f, body);
    DrawCircleV(pos, 14.0f, body);
    DrawCircleV(pos, 6.0f, accent);
    DrawDirectionArrow(muzzle, (Vector2){ cosf(angleRad), sinf(angleRad) }, 10.0f, accent);
}

void RenderCannons(const PhysicsWorld *phys)
{
    for (int i = 0; i < MAX_CANNONS; i++)
    {
        const Cannon *cannon = &phys->cannons[i];
        if (!cannon->active) continue;
        DrawCannonShape(cannon->pos, cannon->angleRad, 1.0f);
    }
}

void RenderCannonPreview(const SketchState *sketch)
{
    if (!sketch->aimingCannon) return;
    DrawCannonShape(sketch->cannonAnchor, sketch->cannonAngle, 0.55f);

    // Long aim guide so the launch direction is readable while dragging
    Vector2 far = {
        sketch->cannonAnchor.x + cosf(sketch->cannonAngle) * 240.0f,
        sketch->cannonAnchor.y + sinf(sketch->cannonAngle) * 240.0f
    };
    Color guide = BALL_RED;
    guide.a = 90;
    DrawLineEx(sketch->cannonAnchor, far, 2.0f, guide);
}

void RenderGhostTrail(const PhysicsWorld *phys)
{
    if (phys->ghostCount < 2) return;

    // Faint dotted breadcrumb of the last run — denser where the ball was slow
    Color dot = GHOST_BLUE;
    dot.a = 110;
    for (int i = 0; i < phys->ghostCount; i += 2)
    {
        DrawCircleV(phys->ghost[i].pos, 2.5f, dot);
    }
}

void RenderCheckpointFlag(const PhysicsWorld *phys)
{
    if (!phys->checkpointSet) return;

    Vector2 base = phys->checkpoint.pos;
    Vector2 top = { base.x, base.y - 34.0f };
    DrawLineEx(base, top, 3.0f, INK_BROWN);
    DrawTriangle(top,
                 (Vector2){ top.x, top.y + 12.0f },
                 (Vector2){ top.x + 18.0f, top.y + 6.0f }, BALL_RED);
    DrawCircleV(base, 4.0f, INK_BROWN);
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
               bool simulating, bool debugMode, bool levelMenuOpen, bool checkpointSet, Vector2 uiMouse)
{
    if (showTitle)
    {
        DrawTextCentered("HEX MAGICAL", 200, 48, INK_BROWN);
        DrawTextCentered("a line rider crayon toy", 260, 22, CRAYON_BROWN);
        DrawTextCentered("Draw track under the ball, then send it to the star", 340, 18, INK_BROWN);
        DrawTextCentered("LMB draw   RMB erase   SPACE start/stop   R restart   WASD pan   +/- zoom", 380, 18, CRAYON_BROWN);
        DrawTextCentered("Z/X/C/V tools   1 save solution   2 load   3 delete", 410, 18, CRAYON_BROWN);
        DrawTextCentered("Press SPACE or click to play", 480, 22, BALL_RED);
        DrawFpsIndicator();
        return;
    }

    Rectangle debugBtn = RenderGetDebugButtonRect();
    DrawUiButton(debugBtn, "DEBUG", debugMode, CheckCollisionPointRec(uiMouse, debugBtn));

    if (showPlayButton)
    {
        Rectangle btn = RenderGetStartButtonRect();
        const char *startLabel = checkpointSet ? "START @ FLAG" : "START";
        DrawUiButton(btn, simulating ? "STOP" : startLabel, simulating, CheckCollisionPointRec(uiMouse, btn));
        if (simulating)
        {
            DrawText("Running — STOP or SPACE to go back to drawing", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
        }
        else
        {
            DrawText("Build, then START or SPACE   |   RMB erase   1 save  2 load  3 delete", 16, GAME_SCREEN_HEIGHT - 28, 16, CRAYON_BROWN);
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
// Build tool bar
//----------------------------------------------------------------------------------
#define TOOL_CHIP_W 150.0f
#define TOOL_CHIP_H 48.0f
#define TOOL_CHIP_GAP 8.0f
#define TOOL_BAR_X 16.0f
// Sits just above the bottom hint line
#define TOOL_BAR_Y ((float)GAME_SCREEN_HEIGHT - 28.0f - 14.0f - TOOL_CHIP_H)

Rectangle RenderGetToolButtonRect(int slot)
{
    return (Rectangle){
        TOOL_BAR_X + (float)slot * (TOOL_CHIP_W + TOOL_CHIP_GAP),
        TOOL_BAR_Y,
        TOOL_CHIP_W,
        TOOL_CHIP_H
    };
}

void RenderToolBar(const PhysicsWorld *phys, const BuildTool *tools, int toolCount,
                   BuildTool currentTool, Vector2 uiMouse)
{
    static const char *toolLabels[TOOL_COUNT] = { "Z CRAYON", "X BOOST", "C CANNON", "V FLAG" };

    for (int slot = 0; slot < toolCount; slot++)
    {
        BuildTool tool = tools[slot];
        Rectangle chip = RenderGetToolButtonRect(slot);
        bool selected = (tool == currentTool);
        bool hover = CheckCollisionPointRec(uiMouse, chip);

        Color fill = selected ? (Color){ 60, 120, 60, 230 }
                   : hover ? (Color){ 235, 222, 190, 245 }
                   : (Color){ 245, 236, 214, 235 };
        Color border = selected ? (Color){ 80, 220, 80, 255 } : INK_BROWN;
        Color text = selected ? (Color){ 180, 255, 180, 255 } : INK_BROWN;

        DrawRectangleRec(chip, fill);
        DrawRectangleLinesEx(chip, 2.0f, border);
        DrawText(toolLabels[tool], (int)chip.x + 8, (int)chip.y + 6, 16, text);

        if ((tool == TOOL_CRAYON) || (tool == TOOL_BOOST_LINE))
        {
            // Remaining ink bar (committed strokes only; refunds on erase)
            float capacity = (tool == TOOL_BOOST_LINE) ? phys->boostLineCapacity : phys->lineCapacity;
            float used = (tool == TOOL_BOOST_LINE) ? PhysicsBoostInkUsed(phys) : PhysicsDrawnInkUsed(phys);
            float remaining = capacity - used;
            if (remaining < 0.0f) remaining = 0.0f;
            float fraction = (capacity > 0.0f) ? remaining / capacity : 0.0f;

            Rectangle bar = { chip.x + 8.0f, chip.y + 30.0f, TOOL_CHIP_W - 60.0f, 10.0f };
            Color inkColor = (tool == TOOL_BOOST_LINE) ? BOOST_ORANGE : (Color){ 40, 90, 200, 255 };
            DrawRectangleRec(bar, (Color){ 200, 190, 165, 255 });
            DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width * fraction, bar.height }, inkColor);
            DrawRectangleLinesEx(bar, 1.0f, CRAYON_BROWN);
            DrawText(TextFormat("%.0f", remaining), (int)(bar.x + bar.width + 6.0f), (int)bar.y - 2, 14, text);
        }
        else if (tool == TOOL_CANNON)
        {
            int placed = PhysicsActiveCannonCount(phys);
            DrawText(TextFormat("%d / %d placed", placed, phys->cannonCapacity),
                     (int)chip.x + 8, (int)chip.y + 28, 14, text);
        }
        else if (tool == TOOL_FLAG)
        {
            const char *status = phys->checkpointSet ? "flag set - START resumes"
                               : (phys->ghostCount >= 2) ? "click the ghost trail"
                               : "run once to get a trail";
            DrawText(status, (int)chip.x + 8, (int)chip.y + 28, 13, text);
        }
    }
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

    int buttonCount = hasNext ? 6 : 5;
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

    const char *labels[6];
    labels[0] = "Admire creation";
    labels[1] = "Keep editing";
    labels[2] = solutionSaved ? "Solution saved!" : "Save as solution";
    labels[3] = "Restart level";
    labels[4] = hasNext ? "Next level" : "Quit to title";
    labels[5] = "Quit to title";

    for (int i = 0; i < buttonCount; i++)
    {
        Rectangle btn = RenderGetWinMenuButtonRect(i, buttonCount);
        DrawUiButton(btn, labels[i], false, CheckCollisionPointRec(uiMouse, btn));
    }

    const char *hint = "H admire   E edit   S save   R restart   N next   Q quit";
    int hw = MeasureText(hint, 14);
    DrawText(hint, (GameGetViewWidth() - hw) / 2, (int)(panel.y + panel.height + 12), 14, PAPER);
}

void RenderPauseMenu(Vector2 uiMouse)
{
    DrawRectangle(0, 0, GameGetViewWidth(), GAME_SCREEN_HEIGHT, (Color){ 60, 45, 30, 140 });

    const int buttonCount = 3;
    Rectangle panel = WinMenuPanelRect(buttonCount);
    DrawRectangleRec(panel, (Color){ 245, 236, 214, 250 });
    DrawRectangleLinesEx(panel, 3.0f, INK_BROWN);
    DrawText("Paused", (int)(panel.x + WIN_MENU_PAD), (int)(panel.y + WIN_MENU_PAD),
             WIN_MENU_TITLE_FONT, INK_BROWN);
    DrawText("The world is frozen — take a breath.",
             (int)(panel.x + WIN_MENU_PAD),
             (int)(panel.y + WIN_MENU_PAD + (float)WIN_MENU_TITLE_FONT + 16.0f),
             WIN_MENU_FONT, CRAYON_BROWN);

    const char *labels[3] = { "Resume", "Restart level", "Quit to title" };
    for (int i = 0; i < buttonCount; i++)
    {
        Rectangle btn = RenderGetWinMenuButtonRect(i, buttonCount);
        DrawUiButton(btn, labels[i], false, CheckCollisionPointRec(uiMouse, btn));
    }

    const char *hint = "ESC resume   R restart   Q quit";
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
