#ifndef GS_SCOPE_GAUSS_H
#define GS_SCOPE_GAUSS_H

uniform float4 scope_lens_gauss; // aberration, electronic interference

float3 scope_gauss_image(float3 scene, float2 uv)
{
    float2 pixel_noise = uv * (scope_lens_electronics.w * 0.1f + 1.0f);
    float noise = frac(sin(dot(pixel_noise, float2(12.9898f, 78.233f))) * 43758.5453f);
    float gain = scope_lens_electronics.y;
    float gray = (scene.r + scene.g + scene.b) * gain * 7.0f;
    scene = scene * (1.0f - gain) + gray;
    scene.b += (0.4f + (0.5f + 0.5f * noise) * scope_lens_electronics.z) * gain;

    float interference = scope_lens_gauss.y;
    return saturate(scene + noise * interference * interference * 15.0f);
}

#endif
