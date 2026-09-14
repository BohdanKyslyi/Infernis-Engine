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
