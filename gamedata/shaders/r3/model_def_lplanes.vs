// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "skin.h"
#include "screenspace_mvectors.h"

struct vf
{
	float2 tc0	: TEXCOORD0;		// base
	float4 c0	: COLOR0;		// color
	float4 hpos	: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc

	// calculate fade
	float3  dir_v 	= normalize		(mul(m_WV,v.P));
	float3 	norm_v 	= normalize 		(mul(m_WV,v.N));
	float 	fade 	= abs			(dot(dir_v,norm_v));
	o.c0		= fade;

	// TAA Jitter
	o.hpos.xy = ssfx_taa_jitter(o.hpos);
	
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
	float2 tc0	: TEXCOORD0;		// base
	float4 c0	: COLOR0;		// color
	float4 hpos	: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc

	// calculate fade
	float3  dir_v 	= normalize		(mul(m_WV,v.P));
	float3 	norm_v 	= normalize 		(mul(m_WV,v.N));
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
