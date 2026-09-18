#include "common.h"
#include "skin.h"

struct p_scope_lens {
    float2 tc : TEXCOORD0;
    float3 view_pos : TEXCOORD1;
    float3 view_tangent : TEXCOORD2;
    float3 view_binormal : TEXCOORD3;
    float3 view_normal : TEXCOORD4;
    float4 hpos : SV_Position;
};

p_scope_lens scope_lens_vertex(v_model v) {
    p_scope_lens o;
    o.tc = v.tc.xy;
    o.hpos = mul(m_WVP, v.P);
    // Work with the skinned vertex handed to this function, so the pupil
    // shadow follows the lens mesh through HUD animations.
    o.view_pos = mul(m_WV, v.P).xyz;
    o.view_tangent = mul((float3x3)m_WV, v.T.xyz);
    o.view_binormal = mul((float3x3)m_WV, v.B.xyz);
    o.view_normal = mul((float3x3)m_WV, v.N.xyz);
    return o;
}

#ifdef SKIN_NONE
p_scope_lens main(v_model v) { return scope_lens_vertex(v); }
#endif
#ifdef SKIN_0
p_scope_lens main(v_model_skinned_0 v) { return scope_lens_vertex(skinning_0(v)); }
#endif
#ifdef SKIN_1
p_scope_lens main(v_model_skinned_1 v) { return scope_lens_vertex(skinning_1(v)); }
#endif
#ifdef SKIN_2
p_scope_lens main(v_model_skinned_2 v) { return scope_lens_vertex(skinning_2(v)); }
#endif
#ifdef SKIN_3
p_scope_lens main(v_model_skinned_3 v) { return scope_lens_vertex(skinning_3(v)); }
#endif
#ifdef SKIN_4
p_scope_lens main(v_model_skinned_4 v) { return scope_lens_vertex(skinning_4(v)); }
#endif
