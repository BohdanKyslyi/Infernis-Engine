#include "common.h"

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

#ifdef GS_SCOPE_NIGHT
    const float mode = 2.0f;
#else
    float mode = gunslinger_scope_state.y;
#endif
    if (mode > 1.5f) {
        float light = dot(scene, float3(0.299f, 0.587f, 0.114f));
        scene = saturate(float3(0.08f, 1.0f, 0.13f) * pow(saturate(light * 2.2f), 0.75f));
    } else if (mode > 0.5f) {
        float light = dot(scene, float3(0.299f, 0.587f, 0.114f));
        scene = saturate(light * 1.55f - 0.12f) * float3(1.0f, 0.96f, 0.20f);
    }

    float2 reticle_uv = input.tc;
#ifdef GS_SCOPE_SPLIT_RETICLE
    reticle_uv.x *= 0.5f;
#endif
    float4 reticle = tex2D(s_base, reticle_uv);
    return float4(lerp(scene, reticle.rgb, reticle.a), blend);
}
