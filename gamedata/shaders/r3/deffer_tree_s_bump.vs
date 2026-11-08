// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
// NOTE: This shader is used by all sort of vehicles on the map.

#include "common.h"

#include "screenspace_mvectors.h"

uniform float3x4	m_xform;
uniform float3x4	m_xform_v;
uniform float4 		consts;
uniform float4 		c_scale, c_bias;
uniform float2 		c_sun;

v2p_bumped 	main 	(v_tree I)
{
	I.Nh	=	unpack_D3DCOLOR(I.Nh);
	I.T		=	unpack_D3DCOLOR(I.T);
	I.B		=	unpack_D3DCOLOR(I.B);
	
	// Transform to world coords
	float3 pos	= mul(m_xform, I.P);
	float2 tc	= (I.tc * consts).xy;
	float hemi	= I.Nh.w * c_scale.w + c_bias.w;
	float4 wpos = float4(pos, 1);

	// Eye-space pos/normal
	v2p_bumped 	O;
	float3	Pe	= mul(m_V, wpos);
	O.tcdh 		= float4(tc.xyyy);
	O.hpos 		= mul(m_VP, wpos);
	O.position	= float4(Pe, hemi);

	// Values to calc motion vectors
	O.vel_curr = O.hpos;
	O.vel_prev = mul(m_vp_prev, wpos);

	// TAA Jitter
	O.hpos.xy = ssfx_taa_jitter(O.hpos);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float suno	= I.Nh.w * c_sun.x + c_sun.y;
	O.tcdh.w	= suno;
#endif

	// Calculate the 3x3 transform from tangent space to eye-space
	// TangentToEyeSpace = object2eye * tangent2object
	// = object2eye * transpose(object2tangent) (since the inverse of a rotation is its transpose)
	float3 	N = unpack_bx4(I.Nh);
	float3 	T = unpack_bx4(I.T);
	float3 	B = unpack_bx4(I.B);
	float3x3 xform	= mul	((float3x3)m_xform_v, float3x3(
						T.x,B.x,N.x,
						T.y,B.y,N.y,
						T.z,B.z,N.z
					));

	// The pixel shader operates on the bump-map in [0..1] range
	// Remap this range in the matrix, anyway we are pixel-shader limited :)
	// ...... [ 2  0  0  0]
	// ...... [ 0  2  0  0]
	// ...... [ 0  0  2  0]
	// ...... [-1 -1 -1  1]
	// issue: strange, but it's slower :(
	// issue: interpolators? dp4? VS limited? black magic?

	// Feed this transform to pixel shader
	O.M1 			= xform[0];
	O.M2 			= xform[1];
	O.M3 			= xform[2];

#ifdef 	USE_TDETAIL
	O.tcdbump		= O.tcdh * dt_params;
#endif

	return O;
}
FXVS;
#else
#define USE_TREEWAVE
#include "deffer_tree_bump.vs"
#endif
