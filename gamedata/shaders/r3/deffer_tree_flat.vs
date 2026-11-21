// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 23
 * @ Description: Trees - Trunk
 * @ Modified time: 2025-04-05 15:36:47
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#include "common.h"
#include "check_screenspace.h"

#include "screenspace_mvectors.h"

uniform float3x4	m_xform;
uniform float3x4	m_xform_v;
uniform float4 		consts;
uniform float4 		c_scale, c_bias, wind, wave;
uniform float2 		c_sun;

float4 prev_wave;
float4 prev_wind;

#ifdef SSFX_WIND
	#include "screenspace_wind.h"
#endif

v2p_flat main (v_tree I)
{
	I.Nh	= unpack_D3DCOLOR(I.Nh);
	I.T		= unpack_D3DCOLOR(I.T);
	I.B		= unpack_D3DCOLOR(I.B);

	v2p_flat o;

	// Transform to world coords
	float3 pos		= mul(m_xform, I.P);
	float H			= pos.y - m_xform._24;

	float4 P_prev = 0;

#ifndef SSFX_WIND
	float dp			= calc_cyclic(wave.w+dot(pos,(float3)wave));
	float frac			= I.tc.z * consts.x;
	float inten			= H * dp;
	float2 wind_result	= calc_xz_wave(wind.xz * inten, frac);

	// Prev Position
	dp = calc_cyclic  (prev_wave.w+dot(pos,(float3)prev_wave));
	inten = H * dp;
	float2 p_wind_result = calc_xz_wave(prev_wind.xz * inten, frac);
	
	P_prev = float4(pos.x + p_wind_result.x, pos.y, pos.z + p_wind_result.y, 1);
#else
	float2 wind_result = ssfx_wind_tree_trunk(pos, H, ssfx_wind_setup(), false).xy;

	float2 prev_wind_result = ssfx_wind_tree_trunk(pos, H, ssfx_wind_setup(), true).xy;
	P_prev = float4(pos.x + prev_wind_result.x, pos.y, pos.z + prev_wind_result.y, 1);

#endif

#ifdef USE_TREEWAVE
	wind_result	= 0;
#endif

	float4 f_pos = float4(pos.x + wind_result.x, pos.y, pos.z + wind_result.y, 1);

	// Final xform
	float3 Pe 	= mul(m_V,  f_pos);
	float hemi 	= I.Nh.w * c_scale.w + c_bias.w;
	o.hpos 		= mul(m_VP, f_pos);
	o.N 		= normalize(mul((float3x3)m_xform_v, I.P.xyz));
	o.tcdh 		= float4((I.tc * consts).xyyy);
	o.position 	= float4(Pe, hemi);

	// Values to calc motion vectors
	o.vel_curr = o.hpos;
	o.vel_prev = mul(m_vp_prev, f_pos);

	// Test TAA
	o.hpos.xy = ssfx_taa_jitter(o.hpos);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float suno = I.Nh.w * c_sun.x + c_sun.y;
	o.tcdh.w = suno;
#endif

	#ifdef USE_TDETAIL
		o.tcdbump	= o.tcdh * dt_params;
	#endif

	return o;
}
FXVS;
#else
#include "common.h"
#include "noir_tree_wind.h"

uniform float3x4	m_xform;
uniform float3x4	m_xform_v;
uniform float4 		consts; 	// {1/quant,1/quant,???,???}
uniform float4 		c_scale,c_bias,wind,wave;
uniform float2 		c_sun;		// x=*, y=+

v2p_flat main (v_tree I)
{
	I.Nh	=	unpack_D3DCOLOR(I.Nh);
	I.T		=	unpack_D3DCOLOR(I.T);
	I.B		=	unpack_D3DCOLOR(I.B);

	v2p_flat 		o;

	// Transform to world coords
	float3 	pos		= mul		(m_xform, I.P);

	//
    float3 displacement = float3(0.0, 0.0, 0.0);
#ifndef USE_TREEWAVE
    // In this engine USE_TREEWAVE marks the STATIC tree_s/Object LOD variants.
    displacement = noir_calc_tree_wind(pos, m_xform._24, I.tc.z * consts.x, wind, wave.w);
#endif
    float4 f_pos = float4(pos + displacement, 1.0);


	// Final xform(s)
	// Final xform
	float3	Pe		= mul		(m_V,  f_pos				);
	float 	hemi 	= I.Nh.w*c_scale.w + c_bias.w;
    //float 	hemi 	= I.Nh.w;
	o.hpos			= mul		(m_VP, f_pos				);
	o.N 			= mul		((float3x3)m_xform_v, unpack_bx2(I.Nh)	);
	o.tcdh 			= float4	((I.tc * consts).xyyy		);
	o.position		= float4	(Pe, hemi					);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float 	suno 	= I.Nh.w * c_sun.x + c_sun.y	;
	o.tcdh.w		= suno;					// (,,,dir-occlusion)
#endif

	#ifdef USE_TDETAIL
	o.tcdbump	= o.tcdh*dt_params;					// dt tc
	#endif

	return o;
}
FXVS;
#endif
