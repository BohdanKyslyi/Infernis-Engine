// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 19
 * @ Description: Trees - Shadows
 * @ Modified time: 2025-02-04 02:37
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#include "common.h"

uniform float3x4		m_xform;
uniform float4 			consts; 	// {1/quant,1/quant,???,???}

#include "screenspace_wind.h"

#ifdef	USE_AREF
v2p_shadow_direct_aref main ( v_shadow_direct_aref I )
#else
v2p_shadow_direct main ( v_shadow_direct I )
#endif
{
#ifdef	USE_AREF
	v2p_shadow_direct_aref 	O;
#else
	v2p_shadow_direct 		O;
#endif
	
	// Transform to world coords
	float3 	pos	= mul(m_xform , I.P);
	float 	H	= pos.y - m_xform._24;	// height of vertex (scaled, rotated, etc.)
	float2	tc = 0;
	float3	wind_result = 0;

#ifdef	USE_TREEWAVE
	wind_result	= 0;
#else

	#ifdef	USE_AREF
		tc = (I.tc * consts).xy;
		wind_result = ssfx_wind_tree_branches(pos, H, tc.y, ssfx_wind_setup(), false);
	#else
		wind_result.xz = ssfx_wind_tree_trunk(pos, H, ssfx_wind_setup(), false).xy;
	#endif

#endif

	float4 f_pos = float4(pos.xyz + wind_result.xyz, 1);

	O.hpos 	= mul(m_VP,	f_pos	);

#ifdef	USE_AREF
	O.tc0 = tc;
#endif

#ifndef USE_HWSMAP
	O.depth = O.hpos.z;
#endif
	return O;
}
FXVS;
#else
#include "common.h"
#include "noir_tree_wind.h"

uniform float3x4		m_xform;
uniform float3x4		m_xform_v;
uniform float4 			consts; 	// {1/quant,1/quant,???,???}
uniform float4 			c_scale,c_bias,wind,wave;

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
#ifdef	USE_AREF
v2p_shadow_direct_aref main (v_tree I)
#else	//	USE_AREF
v2p_shadow_direct main (v_tree I)
#endif	//	USE_AREF
{
#ifdef	USE_AREF
	v2p_shadow_direct_aref 	O;
#else	//	USE_AREF
	v2p_shadow_direct 		O;
#endif	//	USE_AREF
	

	// Transform to world coords
	float3 	pos	= mul		(m_xform , I.P);

	// 
    float3 displacement = float3(0.0, 0.0, 0.0);
#ifndef USE_TREEWAVE
    displacement = noir_calc_tree_wind(pos, m_xform._24, I.tc.z * consts.x, wind, wave.w);
#endif
    float4 f_pos = float4(pos + displacement, 1.0);


	O.hpos 	= mul		(m_VP,	f_pos	);
#ifdef	USE_AREF
	O.tc0 	= (I.tc * consts).xy;		//	+ result;
#endif	//	USE_AREF
#ifndef USE_HWSMAP
	O.depth = O.hpos.z;
#endif
 	return	O;
}
FXVS;
#endif
