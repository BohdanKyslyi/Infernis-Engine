#include "common.h"

Texture2D s_vp2;
uniform float4 scope_lens_state; // capture ready, mode, detector, blend
uniform float4 scope_lens_glass;
uniform float4 scope_lens_size;

struct p_scope_lens {
    float2 tc : TEXCOORD0;
    float3 view_pos : TEXCOORD1;
    float3 view_tangent : TEXCOORD2;
    float3 view_binormal : TEXCOORD3;
    float3 view_normal : TEXCOORD4;
};

struct scope_output {
    float4 color : SV_Target;
    float depth : SV_Depth;
};

float3 unit_direction(float3 direction)
{
    return direction * rsqrt(max(dot(direction, direction), 0.000001f));
}

scope_output main(p_scope_lens input, float4 screen_pos : SV_Position)
{
    // The authored aperture shows a coated lens until a PiP frame is ready.
    float blend = saturate(scope_lens_state.x * scope_lens_state.w);
    float2 radial = input.tc * 2.0f - 1.0f;
    float3 eye = unit_direction(-input.view_pos);
    float3 normal = unit_direction(input.view_normal);
    float facing = abs(dot(eye, normal));
    float fresnel = (1.0f - facing) * (1.0f - facing);
    float2 highlight = (input.tc - float2(0.31f, 0.24f)) * float2(1.0f, 1.6f);
    float glint = exp2(-17.0f * dot(highlight, highlight));
    float rim = smoothstep(0.25f, 0.90f, dot(radial, radial));
#ifdef GS_SCOPE_NIGHT
    const float3 coating = float3(0.028f, 0.055f, 0.047f);
#else
    const float3 coating = float3(0.032f, 0.044f, 0.055f);
#endif
    float3 glass = coating * (1.0f - 0.65f * rim) +
        (0.23f * fresnel + 0.16f * glint) * float3(0.30f, 0.43f, 0.52f);

    scope_output output;
    output.color = float4(glass, 0.94f);
    output.depth = screen_pos.z;
    if (blend <= 0.001f)
        return output;

    // GUNSLINGER meshes place both the reticle and the scene in lens UVs.
    float2 pixel = 1.0f / max(scope_lens_size.xy, float2(1.0f, 1.0f));
    float2 uv = (input.tc - 0.5f) * float2(scope_lens_size.y * pixel.x, 1.0f) + 0.5f;
    float3 scene = s_vp2.Sample(smp_rtlinear, saturate(uv)).rgb;

#ifdef GS_SCOPE_NIGHT
    const float mode = 2.0f;
#else
    float mode = scope_lens_state.y;
#endif
    if (mode > 1.5f) {
        float light = dot(scene, float3(0.299f, 0.587f, 0.114f));
        scene = saturate(float3(0.08f, 1.0f, 0.13f) * pow(saturate(light * 2.2f), 0.75f));
    } else if (mode > 0.5f) {
        float light = dot(scene, float3(0.299f, 0.587f, 0.114f));
        scene = saturate(light * 1.55f - 0.12f) * float3(1.0f, 0.96f, 0.20f);
    } else {
        float2 aberration = radial * (scope_lens_size.z * rim) * pixel;
        scene.r = s_vp2.Sample(smp_rtlinear, saturate(uv + aberration)).r;
        scene.b = s_vp2.Sample(smp_rtlinear, saturate(uv - aberration)).b;
        scene *= lerp(float3(0.99f, 0.99f, 0.98f), float3(0.80f, 0.80f, 0.76f), rim);

        float2 eye_offset = float2(dot(eye, unit_direction(input.view_tangent)),
                                   dot(eye, unit_direction(input.view_binormal))) / max(facing, 0.2f);
        float pupil = length(radial + eye_offset * scope_lens_glass.x);
        scene *= 1.0f - smoothstep(0.90f, 1.15f, pupil) * scope_lens_glass.z;
        float2 glint_uv = (input.tc - float2(0.29f, 0.23f) - eye_offset * 0.04f) * float2(1.0f, 1.4f);
        scene = saturate(scene + scope_lens_glass.y * exp2(-22.0f * dot(glint_uv, glint_uv)) * (1.0f - rim));
    }

    float2 reticle_uv = input.tc;
#ifdef GS_SCOPE_SPLIT_RETICLE
    reticle_uv.x *= 0.5f; // default, unilluminated half of the GUNSLINGER atlas
#endif
    float4 reticle = s_base.Sample(smp_base, reticle_uv);
    float3 image = lerp(scene, reticle.rgb, reticle.a);
    output.color = float4(lerp(glass, image, blend), lerp(0.94f, 1.0f, blend));
    // Keep the PiP image in front of the geometry behind the aperture.
    output.depth = 0.0f;
    return output;
}
