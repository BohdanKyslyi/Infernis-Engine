#ifndef GS_SCOPE_ELECTRONICS_H
#define GS_SCOPE_ELECTRONICS_H

uniform float4 scope_lens_electronics; // atlas blend, NV gain, noise scale, time in seconds

// Night vision based on the GUNSLINGER pnv.h effect by LVutner and Romann.
float3 scope_night_vision(float3 scene, float2 uv, float height)
{
    float time = scope_lens_electronics.w;
    float light = dot(scene, float3(0.30f, 0.38f, 0.22f)) * 5.0f;
    float3 image = float3(1.0f, 2.0f, 1.0f) * (scope_lens_electronics.y * light);
    float noise = frac(sin(dot(uv, float2(12.0f, 78.0f) + time * 1.17f)) * 43758.0f);
    float scanlines = 0.015f * sin(uv.y * height * 2.0f);
    float flicker = 0.003f * sin(time * 60.0f);
    return saturate(image + scanlines + 0.13f * noise * saturate(scope_lens_electronics.z) + flicker);
}

float3 scope_contrast(float3 scene)
{
    float light = dot(scene, float3(0.299f, 0.587f, 0.114f));
    return saturate(light * 1.55f - 0.12f) * float3(1.0f, 0.96f, 0.20f);
}

#endif
