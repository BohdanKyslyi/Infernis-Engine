#include "common.h"

struct v_rain {
    float4 P     : POSITION;
    float4 color : COLOR0;
    float2 tc0   : TEXCOORD0;
};

struct v2p {
    float4 hpos : POSITION;
    float4 c    : COLOR0;
    float2 tc   : TEXCOORD0;
    float3 Pw   : TEXCOORD1;
};

v2p main(v_rain v) {
    v2p o;
    o.hpos = mul(m_WVP, v.P);
    o.c    = v.color;
    o.tc   = v.tc0;
    o.Pw   = v.P.xyz;
    return o;
}