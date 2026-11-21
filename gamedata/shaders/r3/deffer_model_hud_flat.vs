#include "common.h"
#include "skin.h"

#include "screenspace_mvectors.h"

v2p_flat _main( v_model I, float3 psp )
{
	// world-space  N
	float3 	N_w	= mul( m_W, I.N );

	// Eye-space pos/normal
	v2p_flat 		O;
	float3	Pe	= mul( m_WV, I.P );
	O.hpos 		= mul( m_WVP, I.P );
	O.N 		= mul( (float3x3)m_WV, (float3)I.N );
	O.tcdh 		= float4( I.tc.xyyy	);
	
	// Values to calc motion vectors
	O.vel_curr = O.hpos;
	O.vel_prev = mul(m_wvp_prev, I.P_prev );

	// Test TAA
	O.hpos.xy = ssfx_taa_jitter(O.hpos);

	//  Hemi cube lighting
	float3	Nw	= mul		((float3x3)m_W, (float3)I.N);
	float3  hc_pos	= (float3)hemi_cube_pos_faces;
	float3	hc_neg	= (float3)hemi_cube_neg_faces;
	float3  hc_mixed= (Nw < 0) ? hc_neg : hc_pos;
	float	hemi_val= dot( hc_mixed, abs(Nw) );
	hemi_val	= saturate(hemi_val);

	O.position	= float4(Pe, 	hemi_val);		//Use L_material.x for old behaviour;

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	O.tcdh.w	= L_material.y;							// (,,,dir-occlusion)
#endif

#ifdef USE_TDETAIL
	O.tcdbump	= O.tcdh*dt_params;					// dt tc
#endif

	// HUD Rain drops - SSS Update 17
	// https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders/
	if (!all(psp))
		O.RDrops.xyz = I.P.xyz;
	else
		O.RDrops.xyz = psp;

	O.RDrops.w = Nw.y; // Normal-Y [ World Space ]

	return	O;
}

/////////////////////////////////////////////////////////////////////////
// Screen Space Shaders - DEVIATION FROM THE DONOR, forced by the engine. The second argument is the
// vertex position in model space, in metres: the drops texture is projected on the model with this position. The donor passes v.P
// as it is, because the Monolith engine stores skinned positions as floats. In this engine v.P is the
// raw 16-bit integer of the stock stream, so it goes through u_position() ( skin.h ) to get metres
// back - otherwise the pattern would be about 2700 times too fine.

#ifdef 	SKIN_NONE
v2p_flat	main(v_model v) 		{ return _main(v, 0); 		}
#endif

#ifdef 	SKIN_0
v2p_flat	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v), u_position(v.P).xyz); }
#endif

#ifdef	SKIN_1
v2p_flat	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v), u_position(v.P).xyz); }
#endif

#ifdef	SKIN_2
v2p_flat	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v), u_position(v.P).xyz); }
#endif

#ifdef	SKIN_3
v2p_flat	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v), u_position(v.P).xyz); }
#endif

#ifdef	SKIN_4
v2p_flat	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v), u_position(v.P).xyz); }
#endif

FXVS;
