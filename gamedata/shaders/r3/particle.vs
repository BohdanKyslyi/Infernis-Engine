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

struct vv
{
	float4 P	: POSITION;
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;
};

struct v2p
{
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	float4 tctexgen	: TEXCOORD1;
#endif	//	USE_SOFT_PARTICLES
	float  fog	: FOG;	// Fog
	float4 hpos	: SV_Position;
};

uniform float4x4 	mVPTexgen;

v2p main (vv v)
{
	v2p 		o;

	o.hpos 		= mul	(m_WVP, v.P);		// xform, input in world coords
	o.tc		= v.tc;				// copy tc
	o.c			= unpack_D3DCOLOR(v.c);				// copy color

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	o.tctexgen 	= mul( mVPTexgen, v.P);
	o.tctexgen.z	= o.hpos.z;
#endif	//	USE_SOFT_PARTICLES
	o.fog = saturate(calc_fogging(v.P));	// // ForserX (Port SkyLoader fog fix): fog, input in world coords

	return o;
}
#else
#include "common.h"

struct vv
{
	float4 P	: POSITION;
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;
};

struct v2p
{
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	float4 tctexgen	: TEXCOORD1;
#endif	//	USE_SOFT_PARTICLES

	float4 hpos	: SV_Position;
};

uniform float4x4 	mVPTexgen;

v2p main (vv v)
{
	v2p 		o;

	o.hpos 		= mul	(m_WVP, v.P);		// xform, input in world coords
//	o.hpos 		= mul	(m_VP, v.P);		// xform, input in world coords
	o.tc		= v.tc;				// copy tc
	o.c			= unpack_D3DCOLOR(v.c);				// copy color

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	o.tctexgen 	= mul( mVPTexgen, v.P);
	o.tctexgen.z	= o.hpos.z;
#endif	//	USE_SOFT_PARTICLES

	return o;
}
#endif
