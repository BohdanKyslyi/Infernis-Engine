// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "skin.h"

#include "check_screenspace.h"

#ifdef SSFX_FOG
	#include "screenspace_fog.h"
#endif

struct	v_model_
{
	float4	P	: POSITION;		// (float,float,float,1)
	float3	N	: NORMAL;		// (nx,ny,nz)
	float3	T	: TANGENT;		// (nx,ny,nz)
	float3	B	: BINORMAL;		// (nx,ny,nz)
	float2	tc	: TEXCOORD0;	// (u,v)
};

struct 	vf
{
	float2 tc0	: TEXCOORD0;	// base
	float3 tc1	: TEXCOORD1;	// environment
	float3 c0	: COLOR0;		// color
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

#ifdef 	SKIN_NONE
vf _main (v_model_ v)
#else
vf _main (v_model v)
#endif
{
	vf 		o;

	float4 	pos 	= v.P;
	float3  pos_w 	= mul(m_W, pos);
	float3 	norm_w 	= normalize(mul(m_W,v.N));

	o.hpos 		= mul(m_WVP, pos); // xform, input in world coords
	o.tc0		= v.tc.xy; // copy tc
	o.tc1		= calc_reflection(pos_w, norm_w);
	
#ifndef SSFX_FLORAFIX
	o.c0 		= calc_model_lq_lighting(norm_w);
#else
	o.c0 		= calc_model_lq_lighting(float3(0,1,0)); // SSS 14.5 - Improve the illumination using a fake normal
#endif
	
	o.fog 		= saturate(calc_fogging(float4(pos_w,1)) );	// Vanilla fog, input in world coords

#ifdef SSFX_FOG
	o.fog		= SSFX_FOGGING(1.0 - o.fog, pos.y); // SSS Height Fog
#endif	

	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model_ v) { return _main(v); }
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) { return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) { return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) { return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) { return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif
#else
#include "common.h"
#include "skin.h"

struct 	vf
{
	float2 tc0	: TEXCOORD0;		// base
	float3 tc1	: TEXCOORD1;		// environment
	float3 c0	: COLOR0;		// color
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	float4 	pos 	= v.P;
	float3  pos_w 	= mul			(m_W, pos);
	float3 	norm_w 	= normalize 		(mul(m_W,v.N));

	o.hpos 		= mul			(m_WVP, pos);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc
	o.tc1		= calc_reflection	(pos_w, norm_w);
	o.c0 		= calc_model_lq_lighting(norm_w);
	o.fog 		= saturate(calc_fogging 		(float4(pos_w,1)) );	// fog, input in world coords

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
