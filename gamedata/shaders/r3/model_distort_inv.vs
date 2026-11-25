// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's own
// version after #else ( CoP 1.6.00, what the game loaded before ).
// Ported by Ivan Danko, 2026-11-17: FPS plan step 2 - fog on particles as in Anomaly. The SSS
// version is Monolith's own file, copied unchanged ( its authors' notes kept inside ): the vertex
// shader passes the fog factor, the pixel shader fades the particle into the fog with it.
// Перенесено (Ivan Danko, 2026-11-17): крок 2 плану FPS - туман на частинках як в Anomaly. Версія
// для SSS - власний файл Monolith без змін (примітки його авторів збережено всередині): вершинний
// шейдер передає коефіцієнт туману, піксельний з ним розчиняє частинку в тумані. З вимкненим SSS
// і на DX10 - версія гри після #else (CoP 1.6.00, яку гра брала раніше).
// ( _ssfx_work\particle_fog_step2.py )
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "skin.h"

struct vf
{
	float2	tc0	: TEXCOORD0;	// base
	float4	c0	: COLOR0;		// color
	float4	hpos: SV_Position;
	float  fog	: FOG; 	
};

vf 	_main (v_model v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc

	// calculate fade
	float3  dir_v   = normalize    (mul(m_WV,v.P));
	float3  norm_v  = normalize    (mul(m_WV,v.N));
	float 	fade 	= abs			(dot(dir_v,norm_v));
	o.c0		= fade;
	o.fog = saturate( calc_fogging(v.P));
	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ return _main(v); 		}
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif
#else
#include "common.h"
#include "skin.h"

struct vf
{
	float2	tc0	: TEXCOORD0;	// base
	float4	c0	: COLOR0;		// color
	float4	hpos: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc

	// calculate fade
	float3  dir_v   = normalize    (mul(m_WV,v.P));
	float3  norm_v  = normalize    (mul(m_WV,v.N));
	float 	fade 	= abs			(dot(dir_v,norm_v));
	o.c0		= fade;

	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ return _main(v); 		}
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif
#endif
