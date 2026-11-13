#include "common.h"
#include "gs_scope_electronics.h"
#ifdef GS_SCOPE_GAUSS
#include "gs_scope_gauss.h"
#endif

sampler2D s_vp2;
uniform float4 gunslinger_scope_state; // blend, mode, height, width

struct p_scope_lens {
    float2 tc : TEXCOORD0;
};

float4 main(p_scope_lens input) : COLOR0
{
    float blend = saturate(gunslinger_scope_state.x);
    clip(blend - 0.001f);
    float2 uv = (input.tc - 0.5f) * float2(gunslinger_scope_state.z / max(gunslinger_scope_state.w, 1.0f), 1.0f) + 0.5f;
    float3 scene = tex2D(s_vp2, uv).rgb;

#ifdef GS_SCOPE_GAUSS
    float2 offset = length(uv - 0.5f) * scope_lens_gauss.x;
    scene.r = tex2D(s_vp2, uv + offset).r;
    scene.b = tex2D(s_vp2, uv - offset).b;
    scene = scope_gauss_image(scene, uv);
#else
    float mode = gunslinger_scope_state.y;
#ifdef GS_SCOPE_NIGHT
    mode = mode < 0.5f ? 2.0f : mode;
#endif
    if (mode > 1.5f)
        scene = scope_night_vision(scene, uv, gunslinger_scope_state.z);
    else if (mode > 0.5f)
        scene = scope_contrast(scene);
#endif

#ifdef GS_SCOPE_SPLIT_RETICLE
    float2 reticle_uv = input.tc * float2(0.5f, 1.0f);
    float4 unlit = tex2D(s_base, reticle_uv);
    float4 lit = tex2D(s_base, reticle_uv + float2(0.5f, 0.0f));
    float4 reticle = lerp(unlit, lit, saturate(scope_lens_electronics.x));
#else
    float4 reticle = tex2D(s_base, input.tc);
#endif
    return float4(lerp(scene, reticle.rgb, reticle.a), blend);
}
