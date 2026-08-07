/*******************************************************************************************
*
*   lighting.c - Sun light + cloud god-ray composite
*
********************************************************************************************/

#include "lighting.h"

#include <math.h>
#include <stdio.h>

static RenderTexture2D cloudMask = { 0 };
static Shader godRayShader = { 0 };
static int locSunPos = -1;
static int locIntensity = -1;
static int locDensity = -1;
static int locWeight = -1;
static bool ready = false;

// GLSL ES 100 — radial shafts sampling the cloud mask toward the sun.
static const char *GODRAY_FS =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 sunPos;\n"
    "uniform float intensity;\n"
    "uniform float density;\n"
    "uniform float weight;\n"
    "void main()\n"
    "{\n"
    "    vec2 texCoord = fragTexCoord;\n"
    "    vec2 delta = (texCoord - sunPos) * density;\n"
    "    float illumination = 0.0;\n"
    "    float fall = 1.0;\n"
    "    for (int i = 0; i < 12; i++)\n"
    "    {\n"
    "        texCoord -= delta;\n"
    "        float a = texture2D(texture0, texCoord).a;\n"
    "        // Open sky contributes; cloud coverage blocks the shaft.\n"
    "        illumination += (1.0 - a) * fall * weight;\n"
    "        fall *= 0.85;\n"
    "    }\n"
    "    float dist = length(fragTexCoord - sunPos);\n"
    "    float glow = exp(-dist * 2.8) * 0.55;\n"
    "    float shaft = illumination * intensity;\n"
    "    vec3 warm = vec3(1.0, 0.93, 0.72);\n"
    "    float alpha = clamp(shaft + glow * intensity, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(warm * alpha, alpha * 0.85);\n"
    "}\n";

void LightingInit(void)
{
    godRayShader = LoadShaderFromMemory(NULL, GODRAY_FS);
    if (godRayShader.id == 0)
    {
        TraceLog(LOG_ERROR, "LIGHTING: failed to compile god-ray shader");
        ready = false;
        return;
    }
    locSunPos = GetShaderLocation(godRayShader, "sunPos");
    locIntensity = GetShaderLocation(godRayShader, "intensity");
    locDensity = GetShaderLocation(godRayShader, "density");
    locWeight = GetShaderLocation(godRayShader, "weight");
    ready = true;
}

void LightingUnload(void)
{
    if (cloudMask.id != 0)
    {
        UnloadRenderTexture(cloudMask);
        cloudMask = (RenderTexture2D){ 0 };
    }
    if (godRayShader.id != 0)
    {
        UnloadShader(godRayShader);
        godRayShader = (Shader){ 0 };
    }
    ready = false;
}

void LightingEnsureSize(int viewW, int viewH)
{
    if (!ready) return;
    if ((viewW <= 0) || (viewH <= 0)) return;
    if ((cloudMask.id != 0) && (cloudMask.texture.width == viewW)
        && (cloudMask.texture.height == viewH)) return;

    if (cloudMask.id != 0) UnloadRenderTexture(cloudMask);
    cloudMask = LoadRenderTexture(viewW, viewH);
    SetTextureFilter(cloudMask.texture, TEXTURE_FILTER_BILINEAR);
}

void LightingBeginCloudMask(void)
{
    if (!ready || (cloudMask.id == 0)) return;
    BeginTextureMode(cloudMask);
    ClearBackground((Color){ 0, 0, 0, 0 });
}

void LightingEndCloudMask(void)
{
    if (!ready || (cloudMask.id == 0)) return;
    EndTextureMode();
}

bool LightingReady(void)
{
    return ready && (cloudMask.id != 0);
}

void LightingApply(Vector2 sunScreen, float sunIntensity, float night)
{
    if (!LightingReady()) return;
    if (sunIntensity < 0.02f) return;

    float viewW = (float)cloudMask.texture.width;
    float viewH = (float)cloudMask.texture.height;
    if ((viewW < 1.0f) || (viewH < 1.0f)) return;

    // Soft cloud shadows: project mask away from the sun onto the scene.
    {
        Vector2 sun = sunScreen;
        Vector2 center = { viewW * 0.5f, viewH * 0.5f };
        Vector2 away = { center.x - sun.x, center.y - sun.y };
        float len = sqrtf(away.x * away.x + away.y * away.y);
        if (len > 1.0f)
        {
            away.x /= len;
            away.y /= len;
        }
        float shadowDist = 28.0f + night * 10.0f;
        float ox = away.x * shadowDist;
        float oy = away.y * shadowDist;
        // Peak brightness scaled to 70% of the original caps.
        unsigned char shadowA = (unsigned char)(sunIntensity * (0.196f - night * 0.056f) * 255.0f);
        Color shadowTint = { 20, 28, 48, shadowA };
        BeginBlendMode(BLEND_ALPHA);
        DrawTexturePro(
            cloudMask.texture,
            (Rectangle){ 0, 0, viewW, -viewH },
            (Rectangle){ ox, oy, viewW, viewH },
            (Vector2){ 0, 0 }, 0.0f, shadowTint);
        EndBlendMode();
    }

    // Warm radial fill from the sun (additive). Avoid DrawCircleGradient — its
    // signature changed between raylib 5.5 (x,y ints) and 6.0 (Vector2).
    {
        float radius = fmaxf(viewW, viewH) * 0.85f;
        float peakA = sunIntensity * 49.0f;
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 10; i++)
        {
            float t = (float)i / 10.0f; // 0 = outer, ~1 = inner
            float falloff = (1.0f - t) * (1.0f - t);
            unsigned char ca = (unsigned char)(peakA * falloff);
            if (ca == 0) continue;
            DrawCircleV(sunScreen, radius * (1.0f - t * 0.92f),
                        (Color){ 255, 230, 160, ca });
        }
        EndBlendMode();
    }

    // God rays through cloud openings.
    {
        float sunUv[2] = { sunScreen.x / viewW, 1.0f - sunScreen.y / viewH }; // RT Y-flip
        float intensity = sunIntensity * (0.805f - night * 0.21f);
        float density = 0.055f;
        float weight = 0.196f;
        SetShaderValue(godRayShader, locSunPos, sunUv, SHADER_UNIFORM_VEC2);
        SetShaderValue(godRayShader, locIntensity, &intensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(godRayShader, locDensity, &density, SHADER_UNIFORM_FLOAT);
        SetShaderValue(godRayShader, locWeight, &weight, SHADER_UNIFORM_FLOAT);

        BeginBlendMode(BLEND_ADDITIVE);
        BeginShaderMode(godRayShader);
        DrawTexturePro(
            cloudMask.texture,
            (Rectangle){ 0, 0, viewW, -viewH },
            (Rectangle){ 0, 0, viewW, viewH },
            (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndShaderMode();
        EndBlendMode();
    }
}
