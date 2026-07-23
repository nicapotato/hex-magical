/*******************************************************************************************
*
*   sketch.c - Build tools: stroke capture (RDP simplify + Chaikin rounding),
*   cannon aim-and-place, checkpoint flag. Stroke ink is budgeted per level.
*
********************************************************************************************/

#include "sketch.h"

#include <math.h>

static const float SAMPLE_DIST = 5.0f;
// Light simplify — keep path detail so capsule chain follows the ink
static const float RDP_EPSILON = 2.5f;
// Chaikin corner-cutting passes after RDP: rounds corners so the ball rolls
// over joints instead of catching (Line Rider feel)
static const int CHAIKIN_PASSES = 2;
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
// Chaikin corner cutting — each pass replaces every interior corner with two
// points at 1/4 and 3/4 along its segments, converging toward a smooth curve.
// Endpoints are preserved so the stroke starts/ends exactly where drawn.
//----------------------------------------------------------------------------------
static int ChaikinPass(const Vector2 *in, int count, Vector2 *out, int maxOut)
{
    int outCount = 0;
    out[outCount++] = in[0];
    for (int i = 0; i < count - 1; i++)
    {
        Vector2 a = in[i];
        Vector2 b = in[i + 1];
        if (outCount < maxOut) out[outCount++] = (Vector2){ 0.75f*a.x + 0.25f*b.x, 0.75f*a.y + 0.25f*b.y };
        if (outCount < maxOut) out[outCount++] = (Vector2){ 0.25f*a.x + 0.75f*b.x, 0.25f*a.y + 0.75f*b.y };
    }
    if (outCount < maxOut) out[outCount++] = in[count - 1];
    else out[outCount - 1] = in[count - 1];
    return outCount;
}

//----------------------------------------------------------------------------------
// Finalize the accumulated points (RDP denoise + Chaikin rounding) into either a
// solid crayon body or a boost line, and reset accumulation. Strokes split by
// no-build zones call this once per segment, so each side of the zone becomes
// its own piece.
//----------------------------------------------------------------------------------
static void FinalizeSegment(SketchState *sketch, PhysicsWorld *phys)
{
    if (sketch->pointCount >= 2)
    {
        Vector2 bufA[MAX_STROKE_POINTS];
        Vector2 bufB[MAX_STROKE_POINTS];
        int n = SimplifyRDP(sketch->points, sketch->pointCount, RDP_EPSILON, bufA, MAX_STROKE_POINTS);
        if (n < 2)
        {
            bufA[0] = sketch->points[0];
            bufA[1] = sketch->points[sketch->pointCount - 1];
            n = 2;
        }

        Vector2 *cur = bufA;
        Vector2 *next = bufB;
        for (int pass = 0; pass < CHAIKIN_PASSES; pass++)
        {
            // Each pass ~doubles the point count; keeping the whole stroke
            // beats extra smoothing, so stop rather than truncate the tail
            if (2 * n > MAX_STROKE_POINTS) break;
            n = ChaikinPass(cur, n, next, MAX_STROKE_POINTS);
            Vector2 *tmp = cur;
            cur = next;
            next = tmp;
        }

        if (sketch->tool == TOOL_BOOST_LINE) PhysicsCreateBoostLine(phys, cur, n);
        else PhysicsCreateDrawnBody(phys, cur, n, sketch->crayonColor);
    }

    sketch->pointCount = 0;
    sketch->inkUsedThisStroke = 0.0f;
}

// Remaining ink for the active stroke tool: level capacity minus what already
// sits on the canvas (erasing refunds by removing geometry) minus the stroke
// currently being drawn
static float RemainingInk(const SketchState *sketch, const PhysicsWorld *phys)
{
    float capacity = (sketch->tool == TOOL_BOOST_LINE) ? phys->boostLineCapacity : phys->lineCapacity;
    float used = (sketch->tool == TOOL_BOOST_LINE) ? PhysicsBoostInkUsed(phys) : PhysicsDrawnInkUsed(phys);
    return capacity - used - sketch->inkUsedThisStroke;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
void SketchInit(SketchState *sketch)
{
    sketch->tool = TOOL_CRAYON;
    sketch->drawing = false;
    sketch->pointCount = 0;
    sketch->crayonColor = CRAYON_BLUE;
    sketch->inkUsedThisStroke = 0.0f;
    sketch->aimingCannon = false;
    sketch->cannonAnchor = (Vector2){ 0.0f, 0.0f };
    sketch->cannonAngle = 0.0f;
}

void SketchCancel(SketchState *sketch)
{
    sketch->drawing = false;
    sketch->pointCount = 0;
    sketch->inkUsedThisStroke = 0.0f;
    sketch->aimingCannon = false;
}

//----------------------------------------------------------------------------------
// Per-tool input handling
//----------------------------------------------------------------------------------
static void UpdateStrokeTool(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool inNoBuild)
{
    if (lmbPressed)
    {
        // Starting inside a no-build zone is fine — points only register once
        // the cursor leaves the zone
        sketch->drawing = true;
        sketch->pointCount = 0;
        sketch->inkUsedThisStroke = 0.0f;
        if (!inNoBuild) sketch->points[sketch->pointCount++] = worldMouse;
        return;
    }

    if (sketch->drawing && lmbDown)
    {
        if (inNoBuild)
        {
            // Ink pauses inside the zone: whatever was drawn so far becomes its
            // own track segment, and drawing resumes on the other side
            FinalizeSegment(sketch, phys);
            return;
        }

        if (sketch->pointCount == 0)
        {
            sketch->points[sketch->pointCount++] = worldMouse;
            return;
        }

        Vector2 last = sketch->points[sketch->pointCount - 1];
        float dx = worldMouse.x - last.x;
        float dy = worldMouse.y - last.y;
        float distSq = dx * dx + dy * dy;
        if (distSq >= (SAMPLE_DIST * SAMPLE_DIST))
        {
            // Level ink budget: the pen simply runs dry — no point registers
            // past the remaining capacity (erase strokes to refund)
            float segLen = sqrtf(distSq);
            if (segLen > RemainingInk(sketch, phys)) return;

            if (sketch->pointCount < MAX_STROKE_POINTS)
            {
                sketch->points[sketch->pointCount++] = worldMouse;
                sketch->inkUsedThisStroke += segLen;
            }
        }
        return;
    }

    if (sketch->drawing && !lmbDown)
    {
        // Finalize stroke: RDP denoises the raw mouse path, then Chaikin rounds
        // the surviving corners. The smoothed polyline is the single source of
        // truth: collider, ink rendering, and saved solutions all use it.
        FinalizeSegment(sketch, phys);
        sketch->drawing = false;
    }
}

static void UpdateCannonTool(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool inNoBuild)
{
    if (lmbPressed)
    {
        // Cannons are builds too — no placing inside no-build zones or past the count
        if (inNoBuild) return;
        if (PhysicsActiveCannonCount(phys) >= phys->cannonCapacity) return;

        sketch->aimingCannon = true;
        sketch->cannonAnchor = worldMouse;
        sketch->cannonAngle = -PI / 2.0f; // default: straight up until dragged
        return;
    }

    if (sketch->aimingCannon && lmbDown)
    {
        float dx = worldMouse.x - sketch->cannonAnchor.x;
        float dy = worldMouse.y - sketch->cannonAnchor.y;
        if ((dx * dx + dy * dy) > (8.0f * 8.0f)) sketch->cannonAngle = atan2f(dy, dx);
        return;
    }

    if (sketch->aimingCannon && !lmbDown)
    {
        PhysicsAddCannon(phys, sketch->cannonAnchor, sketch->cannonAngle);
        sketch->aimingCannon = false;
    }
}

// Erase whatever build sits under the cursor: crayon stroke, boost line or cannon
static void EraseAt(PhysicsWorld *phys, Vector2 worldMouse)
{
    if (!PhysicsEraseAtPoint(phys, worldMouse))
    {
        if (!PhysicsEraseBoostLineAt(phys, worldMouse)) PhysicsEraseCannonAt(phys, worldMouse);
    }
}

void SketchUpdate(SketchState *sketch, PhysicsWorld *phys, Vector2 worldMouse, bool lmbDown, bool lmbPressed, bool rmbPressed, bool inNoBuild)
{
    if (rmbPressed)
    {
        // RMB erases regardless of the selected tool (quick spot-erase)
        EraseAt(phys, worldMouse);
        SketchCancel(sketch);
        return;
    }

    switch (sketch->tool)
    {
        case TOOL_CRAYON:
        case TOOL_BOOST_LINE:
        {
            UpdateStrokeTool(sketch, phys, worldMouse, lmbDown, lmbPressed, inNoBuild);
        } break;
        case TOOL_CANNON:
        {
            UpdateCannonTool(sketch, phys, worldMouse, lmbDown, lmbPressed, inNoBuild);
        } break;
        case TOOL_FLAG:
        {
            // Click near the ghost trail plants the flag; clicking away clears it
            if (lmbPressed) PhysicsSetCheckpointNear(phys, worldMouse);
        } break;
        case TOOL_ERASER:
        {
            // Erase mode: hold LMB and sweep — anything under the cursor goes
            if (lmbDown) EraseAt(phys, worldMouse);
        } break;
        default: break;
    }
}
