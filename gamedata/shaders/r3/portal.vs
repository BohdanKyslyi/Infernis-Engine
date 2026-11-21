// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "screenspace_fog.h"

struct	v_vert
{
	float4 	pos	: POSITION;	// (float,float,float,1)
	float4	color	: COLOR0;	// (r,g,b,dir-occlusion)
};
struct 	v2p
{
	float4 c	: COLOR0;
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

v2p main (v_vert v)
{
	v2p 		o;

	o.hpos		= mul(m_VP, v.pos); // xform, input in world coords

	float fog	= saturate(calc_fogging(v.pos)); // fog, input in world coords
	o.fog 		= SSFX_FOGGING(1.0 - fog, v.pos.y); // Add SSFX Fog

	o.c.rgb 	= lerp(fog_color, v.color, o.fog * o.fog);	// fog blending
	o.c.a		= o.fog; // Alpha

	return o;
	
}
FXVS;
#else
#include "common.h"

struct	v_vert
{
	float4 	pos	: POSITION;	// (float,float,float,1)
	float4	color	: COLOR0;	// (r,g,b,dir-occlusion)
};
struct 	v2p
{
	float4 c	: COLOR0;
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

v2p main (v_vert v)
{
	v2p 		o;

	o.hpos		= mul(m_VP, v.pos);				// xform, input in world coords
	o.c			= v.color;
	o.fog 		= calc_fogging(v.pos);			// fog, input in world coords
	o.fog 		= saturate(o.fog);
	o.c.rgb 	= lerp(fog_color, o.c, o.fog);

//	float scale = tex2Dlod	(s_tonemap,float4(.5,.5,.5,.5)).x ;
	float scale = s_tonemap.Load(int3(0,0,0)).x;
//	float scale = s_tonemap.Load(int3(1,1,0)).x;
	o.c.rgb		= o.c.rgb*scale;      		// copy color, pre-scale by tonemap //float4 ( v.c.rgb*scale*2, v.c.a );
	o.c.a		= o.fog;

	return o;
	
}
#endif
