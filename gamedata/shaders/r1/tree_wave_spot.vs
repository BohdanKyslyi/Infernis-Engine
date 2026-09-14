#include "common.h"
#include "noir_tree_wind.h"

struct av 
{
	float4 	pos	: POSITION;	// (float,float,float,1)
	float4 	nc	: NORMAL;	// (float,float,float,clr)
	float4 	misc	: TEXCOORD0;	// (u(Q),v(Q),frac,???)
};

uniform float3x4	m_xform;
uniform float4 		consts;		// {1/quant,1/quant,???,???}
uniform float4 		wave; 		// cx,cy,cz,tm
uniform float4 		wind; 		// direction2D
uniform float4		c_bias;		// + color
uniform float4		c_scale;	// * color
uniform float2 		c_sun;		// x=*, y=+

vf_spot main (av v)
{
	vf_spot		o;

	// Transform to world coords
	float3 	pos	= mul	(m_xform, v.pos);

	// 
    float base = m_xform._24;
    float flexibility = v.misc.z * consts.x;
    float3 displacement = noir_calc_tree_wind(pos, base, flexibility, wind, wave.w);
    float4 f_pos = float4(pos + displacement, 1.0);

	float3 	f_N 	= normalize 	(mul (m_xform,  unpack_normal(v.nc)));

	// Final xform
	o.hpos		= mul		(m_VP, f_pos);
	o.tc0		= (v.misc * consts).xy;
	o.color		= calc_spot 	(o.tc1,o.tc2,f_pos,f_N);

	return o;
}
