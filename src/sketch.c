/*******************************************************************************************
*
*   sketch.c - Mouse stroke capture, RDP simplify, spawn/erase bodies
*
********************************************************************************************/

#include "sketch.h"

#include <math.h>

static const float SAMPLE_DIST = 5.0f;
// Light simplify — keep path detail so capsule chain follows the ink
static const float RDP_EPSILON = 2.5f;
static const Color CRAYON_BLUE = { 40, 90, 200, 255 };

//----------------------------------------------------------------------------------
// Ramer–Douglas–Peucker
//----------------------------------------------------------------------------------
static float PointLineDist(Vector2 p, Vector2 a, Vector2 b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 0.0001f)
    {
        float ex = p.x - a.x;
        float ey = p.y - a.y;
        return sqrtf(ex * ex + ey * ey);
    }

    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float projX = a.x + t * dx;
    float projY = a.y + t * dy;
    float ex = p.x - projX;
    float ey = p.y - projY;
    return sqrtf(ex * ex + ey * ey);
}

static void RdpRecurse(const Vector2 *in, int start, int end, float epsilon, bool *keep)
{
    if (end <= start + 1) return;

    float maxDist = 0.0f;
    int maxIndex = start;
    for (int i = start + 1; i < end; i++)
    {
        float d = PointLineDist(in[i], in[start], in[end]);
        if (d > maxDist)
        {
            maxDist = d;
            maxIndex = i;
        }
    }

    if (maxDist > epsilon)
    {
        keep[maxIndex] = true;
        RdpRecurse(in, start, maxIndex, epsilon, keep);
        RdpRecurse(in, maxIndex, end, epsilon, keep);
    }
}

static int SimplifyRDP(const Vector2 *in, int count, float epsilon, Vector2 *out, int maxOut)
{
    if (count <= 2)
    {
        int n = (count < maxOut)? count : maxOut;
        for (int i = 0; i < n; i++) out[i] = in[i];
        return n;
    }

    bool keep[MAX_STROKE_POINTS] = { 0 };
    keep[0] = true;
    keep[count - 1] = true;
    RdpRecurse(in, 0, count - 1, epsilon, keep);

    int outCount = 0;
    for (int i = 0; i < count; i++)
    {
        if (!keep[i]) continue;
        if (outCount >= maxOut) break;
        out[outCount++] = in[i];
    }

    // Cap hard at B2_MAX_POLYGON_VERTICES for hull path, but keep more for stroke render.
    // PhysicsCreateDrawnBody samples down to 8 for the hull; stroke points stay for draw.
    return outCount;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void SketchInit(SketchState *sketch)
{
    sketch->drawing = false;
    sketch->pointCount = 0;
    sketch->crayonColor = CRAYON_BLUE;
}

void SketchCancel(SketchState *sketch)
{
    sketch->drawing = false;
    sketch->pointCount = 0;
}

void SketchUpdate(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool rmbPressed)
{
    if (rmbPressed)
    {
        PhysicsEraseAtPoint(phys, worldMouse);
        SketchCancel(sketch);
        return;
    }

    if (lmbPressed)
    {
        sketch->drawing = true;
        sketch->pointCount = 0;
        sketch->points[sketch->pointCount++] = worldMouse;
        return;
    }

    if (sketch->drawing && lmbDown)
    {
        if (sketch->pointCount > 0)
        {
            Vector2 last = sketch->points[sketch->pointCount - 1];
            float dx = worldMouse.x - last.x;
            float dy = worldMouse.y - last.y;
            if ((dx * dx + dy * dy) >= (SAMPLE_DIST * SAMPLE_DIST))
            {
                if (sketch->pointCount < MAX_STROKE_POINTS)
                {
                    sketch->points[sketch->pointCount++] = worldMouse;
                }
            }
        }
        return;
    }

    if (sketch->drawing && !lmbDown)
    {
        // Finalize stroke: RDP-simplify for a cleaner hull, keep enough points for crayon look
        if (sketch->pointCount >= 2)
        {
            Vector2 simplified[MAX_STROKE_POINTS];
            int n = SimplifyRDP(sketch->points, sketch->pointCount, RDP_EPSILON, simplified, MAX_STROKE_POINTS);
            if (n < 2)
            {
                simplified[0] = sketch->points[0];
                simplified[1] = sketch->points[sketch->pointCount - 1];
                n = 2;
            }

            PhysicsCreateDrawnBody(phys, simplified, n, sketch->crayonColor);
        }

        SketchCancel(sketch);
    }
}
