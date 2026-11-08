// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
// NOTE: Used by a specific vehicle. ( Car with yellow roof )

#include "common.h"

#include "screenspace_mvectors.h"

uniform float3x4 m_xform;
uniform float3x4 m_xform_v;
uniform float4 consts;
uniform float4 c_scale,c_bias;
uniform float2 c_sun;

v2p_flat main (v_tree I)
{
	I.Nh = unpack_D3DCOLOR(I.Nh);
	I.T = unpack_D3DCOLOR(I.T);
	I.B = unpack_D3DCOLOR(I.B);

	v2p_flat O;

	// Transform to world coords
	float3 pos = mul(m_xform, I.P);
	float4 wpos = float4(pos, 1);

	// Final xform
	float3 Pe = mul(m_V,  wpos );
	float hemi = I.Nh.w * c_scale.w + c_bias.w;

	O.hpos = mul(m_VP, wpos );
	O.N = mul((float3x3)m_xform_v, unpack_bx2(I.Nh));
	O.tcdh = float4((I.tc * consts).xyyy );
	O.position = float4(Pe, hemi );

	// Values to calc motion vectors
	O.vel_curr = O.hpos;
	O.vel_prev = mul(m_vp_prev, wpos);

	// TAA Jitter
	O.hpos.xy = ssfx_taa_jitter(O.hpos);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float suno = I.Nh.w * c_sun.x + c_sun.y;
	O.tcdh.w = suno;
#endif

#ifdef USE_TDETAIL
	O.tcdbump = O.tcdh*dt_params;
#endif

	return O;
}
FXVS;
#else
#define USE_TREEWAVE
#include "deffer_tree_flat.vs"
#endif
