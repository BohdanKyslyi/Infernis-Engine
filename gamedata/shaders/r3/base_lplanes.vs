// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "screenspace_mvectors.h"

struct vf
{
	float2 tc0	: TEXCOORD0;		// base
	float4 c0	: COLOR0;		// color
	float4 hpos	: SV_Position;
};

vf main (v_static v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= unpack_tc_base(v.tc,v.T.w,v.B.w);	// copy tc

	// calculate fade
	float3  dir_v 	= normalize	(mul(m_WV,v.P));
	float3 	norm_v 	= normalize (mul(m_WV,unpack_normal(v.Nh).zyx));
	float 	fade 	= abs		(dot(dir_v,norm_v));
	o.c0		= fade;

	// TAA Jitter
	o.hpos.xy = ssfx_taa_jitter(o.hpos);

	return o;
}
#else
#include "common.h"

struct vf
{
	float2 tc0	: TEXCOORD0;		// base
	float4 c0	: COLOR0;		// color
	float4 hpos	: SV_Position;
};

vf main (v_static v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= unpack_tc_base(v.tc,v.T.w,v.B.w);	// copy tc

	// calculate fade
	float3  dir_v 	= normalize	(mul(m_WV,v.P));
	float3 	norm_v 	= normalize (mul(m_WV,unpack_normal(v.Nh).zyx));
	float 	fade 	= abs		(dot(dir_v,norm_v));
	o.c0		= fade;

	return o;
}
#endif
