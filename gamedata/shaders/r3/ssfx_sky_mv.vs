#include "common.h"

struct vi
{
	float4	p		: POSITION;
};

struct v2p
{
	float4	vel_curr 	: TEXCOORD2;
	float4	vel_prev 	: TEXCOORD3;
	float4	hpos		: SV_Position;
};

v2p main (vi v)
{
	v2p o;

	float4 tpos = float4(2000 * v.p.x, 2000 * v.p.y, 2000 * v.p.z, 2000 * v.p.w);
	o.hpos = mul (m_WVP, tpos);
	o.hpos.z = o.hpos.w;
	
	// Values to calc motion vectors
	o.vel_curr = o.hpos;
	o.vel_prev = mul(m_wvp_prev, tpos);
	
	return	o;
}