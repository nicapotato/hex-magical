/*******************************************************************************************
*
*   admin.c - Admin panel: live physics tunables (ball weight, bounciness, drop force)
*
********************************************************************************************/

#include "admin.h"
#include "game.h"

#include <stdio.h>

#define ADMIN_BTN_W 80.0f
#define ADMIN_BTN_H 40.0f
#define ADMIN_PANEL_W 248.0f
#define ADMIN_PAD 10.0f
#define ADMIN_FONT 14
#define ADMIN_HEADER_FONT 16
#define ADMIN_ROW_H 38.0f
#define ADMIN_SLIDER_COUNT 3
#define ADMIN_ACTION_BTN_H 26.0f
#define ADMIN_GAP 8.0f

static bool adminOpen = false;

static const Color PANEL_FILL = { 245, 236, 214, 245 };
static const Color INK = { 90, 60, 40, 255 };
static const Color CRAYON = { 120, 80, 50, 255 };
static const Color ACCENT_RED = { 210, 50, 50, 255 };
static const Color PAPER_TEXT = { 245, 236, 214, 255 };

typedef struct SliderDef
{
    const char *label;
    float minValue;
    float maxValue;
    const char *fmt;
} SliderDef;

static const SliderDef SLIDERS[ADMIN_SLIDER_COUNT] = {
    { "Ball weight (density)", TUNE_BALL_DENSITY_MIN, TUNE_BALL_DENSITY_MAX, "%.2f" },
    { "Bounciness", TUNE_BALL_RESTITUTION_MIN, TUNE_BALL_RESTITUTION_MAX, "%.2f" },
    { "Drop force (down)", TUNE_DROP_FORCE_MIN, TUNE_DROP_FORCE_MAX, "%.0f" },
};

static float GetSliderValue(const PhysicsTunables *t, int row)
{
    switch (row)
    {
        case 0: return t->ballDensity;
        case 1: return t->ballRestitution;
        default: return t->dropForce;
    }
}

static void SetSliderValue(PhysicsTunables *t, int row, float value)
{
    switch (row)
    {
        case 0: t->ballDensity = value; break;
        case 1: t->ballRestitution = value; break;
        default: t->dropForce = value; break;
    }
}

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
static Rectangle PanelRect(void)
{
    Rectangle btn = AdminGetButtonRect();
    float h = ADMIN_PAD + (float)ADMIN_HEADER_FONT + ADMIN_GAP
            + (float)ADMIN_SLIDER_COUNT * ADMIN_ROW_H
            + ADMIN_GAP + ADMIN_ACTION_BTN_H + ADMIN_PAD;
    return (Rectangle){
        (float)GameGetViewWidth() - ADMIN_PANEL_W - 8.0f,
        btn.y + btn.height + 8.0f,
        ADMIN_PANEL_W,
        h
    };
}

static float SliderRowY(const Rectangle *panel, int row)
{
    return panel->y + ADMIN_PAD + (float)ADMIN_HEADER_FONT + ADMIN_GAP + (float)row * ADMIN_ROW_H;
}

static Rectangle SliderBarRect(const Rectangle *panel, int row)
{
    return (Rectangle){
        panel->x + ADMIN_PAD,
        SliderRowY(panel, row) + 18.0f,
        panel->width - ADMIN_PAD * 2.0f,
        10.0f
    };
}

static Rectangle ActionButtonRect(const Rectangle *panel, int index)
{
    float totalW = panel->width - ADMIN_PAD * 2.0f;
    float btnW = (totalW - ADMIN_GAP) * 0.5f;
    return (Rectangle){
        panel->x + ADMIN_PAD + (float)index * (btnW + ADMIN_GAP),
        panel->y + panel->height - ADMIN_PAD - ADMIN_ACTION_BTN_H,
        btnW,
        ADMIN_ACTION_BTN_H
    };
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
bool AdminIsOpen(void)
{
    return adminOpen;
}

Rectangle AdminGetButtonRect(void)
{
    // Sits left of DEBUG (80 wide at the top-right edge)
    return (Rectangle){
        (float)GameGetViewWidth() - 80.0f - 8.0f - ADMIN_BTN_W - 8.0f,
        12.0f,
        ADMIN_BTN_W,
        ADMIN_BTN_H
    };
}

AdminAction AdminHandleInput(PhysicsWorld *phys, Vector2 mouse, bool lmbDown, bool lmbPressed)
{
    if (lmbPressed && CheckCollisionPointRec(mouse, AdminGetButtonRect()))
    {
        adminOpen = !adminOpen;
        return ADMIN_ACTION_CONSUMED;
    }

    if (!adminOpen) return ADMIN_ACTION_NONE;
    if (!lmbDown && !lmbPressed) return ADMIN_ACTION_NONE;

    Rectangle panel = PanelRect();
    if (!CheckCollisionPointRec(mouse, panel)) return ADMIN_ACTION_NONE;

    if (lmbPressed)
    {
        if (CheckCollisionPointRec(mouse, ActionButtonRect(&panel, 0)))
        {
            PhysicsTunablesDefaults(&phys->tunables);
            PhysicsApplyBallTunables(phys);
            return ADMIN_ACTION_CONSUMED;
        }
        if (CheckCollisionPointRec(mouse, ActionButtonRect(&panel, 1)))
        {
            return ADMIN_ACTION_RESPAWN;
        }
    }

    for (int i = 0; i < ADMIN_SLIDER_COUNT; i++)
    {
        Rectangle bar = SliderBarRect(&panel, i);
        Rectangle grab = { bar.x - 4.0f, bar.y - 8.0f, bar.width + 8.0f, bar.height + 16.0f };
        if (!CheckCollisionPointRec(mouse, grab)) continue;

        float t = (mouse.x - bar.x) / bar.width;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        SetSliderValue(&phys->tunables, i, SLIDERS[i].minValue + t * (SLIDERS[i].maxValue - SLIDERS[i].minValue));
        PhysicsApplyBallTunables(phys);
        break;
    }

    return ADMIN_ACTION_CONSUMED; // swallow all input over the open panel
}

static void DrawAdminButton(Vector2 mouse)
{
    Rectangle btn = AdminGetButtonRect();
    bool hover = CheckCollisionPointRec(mouse, btn);

    Color fill = adminOpen ? (Color){ 60, 120, 60, 230 } : (hover ? ACCENT_RED : CRAYON);
    Color border = adminOpen ? (Color){ 80, 220, 80, 255 } : INK;
    Color text = adminOpen ? (Color){ 180, 255, 180, 255 } : PAPER_TEXT;

    DrawRectangleRec(btn, fill);
    DrawRectangleLinesEx(btn, 2.0f, border);
    int tw = MeasureText("ADMIN", 18);
    DrawText("ADMIN", (int)btn.x + ((int)btn.width - tw) / 2, (int)btn.y + 11, 18, text);
}

void AdminDraw(const PhysicsWorld *phys, Vector2 mouse)
{
    DrawAdminButton(mouse);

    if (!adminOpen) return;

    Rectangle panel = PanelRect();
    DrawRectangleRec(panel, PANEL_FILL);
    DrawRectangleLinesEx(panel, 2.0f, INK);

    DrawText("BALL PHYSICS", (int)(panel.x + ADMIN_PAD), (int)(panel.y + ADMIN_PAD), ADMIN_HEADER_FONT, INK);

    char valueText[32];
    for (int i = 0; i < ADMIN_SLIDER_COUNT; i++)
    {
        float value = GetSliderValue(&phys->tunables, i);
        float t = (value - SLIDERS[i].minValue) / (SLIDERS[i].maxValue - SLIDERS[i].minValue);
        float rowY = SliderRowY(&panel, i);

        snprintf(valueText, sizeof(valueText), SLIDERS[i].fmt, value);
        DrawText(SLIDERS[i].label, (int)(panel.x + ADMIN_PAD), (int)rowY, ADMIN_FONT, INK);
        int vw = MeasureText(valueText, ADMIN_FONT);
        DrawText(valueText, (int)(panel.x + panel.width - ADMIN_PAD - (float)vw), (int)rowY, ADMIN_FONT, CRAYON);

        Rectangle bar = SliderBarRect(&panel, i);
        DrawRectangleRec(bar, (Color){ 220, 210, 185, 255 });
        DrawRectangleLinesEx(bar, 1.0f, CRAYON);
        if (t > 0.0f)
        {
            DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * t), (int)bar.height, ACCENT_RED);
        }
        DrawCircleV((Vector2){ bar.x + bar.width * t, bar.y + bar.height * 0.5f }, 6.0f, INK);
    }

    Rectangle resetBtn = ActionButtonRect(&panel, 0);
    bool resetHover = CheckCollisionPointRec(mouse, resetBtn);
    DrawRectangleRec(resetBtn, resetHover ? ACCENT_RED : CRAYON);
    DrawRectangleLinesEx(resetBtn, 2.0f, INK);
    int tw = MeasureText("RESET", ADMIN_FONT);
    DrawText("RESET", (int)(resetBtn.x + (resetBtn.width - (float)tw) * 0.5f), (int)(resetBtn.y + 6.0f), ADMIN_FONT, PAPER_TEXT);

    Rectangle respawnBtn = ActionButtonRect(&panel, 1);
    bool respawnHover = CheckCollisionPointRec(mouse, respawnBtn);
    DrawRectangleRec(respawnBtn, respawnHover ? ACCENT_RED : CRAYON);
    DrawRectangleLinesEx(respawnBtn, 2.0f, INK);
    tw = MeasureText("RESPAWN", ADMIN_FONT);
    DrawText("RESPAWN", (int)(respawnBtn.x + (respawnBtn.width - (float)tw) * 0.5f), (int)(respawnBtn.y + 6.0f), ADMIN_FONT, PAPER_TEXT);
}
