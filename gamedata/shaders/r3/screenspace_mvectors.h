/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 23
 * @ Description: Motion Vectors - Common
 * @ Modified time: 2025-04-20 07:36:37
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#ifndef SSFX_MV_LOADED

	#define SSFX_MV_LOADED

	uniform float4x4 m_wvp_prev;
	uniform float4x4 m_vp_prev;
	uniform float4 ssfx_jitter;

	float4 ssfx_mv_calc(float4 current, float4 previous, float IsHUD, float TAAMask)
	{
		// Motion vectors
		float2 motion_vectors = (current.xy / current.w) - (previous.xy / previous.w);
		
		return float4(float2(motion_vectors.x, -motion_vectors.y) * 0.5f, IsHUD, TAAMask);
	}

	float2 ssfx_taa_jitter(float4 hpos)
	{
		return float2( hpos.xy + ssfx_jitter.xy * hpos.w );
	}

#endif