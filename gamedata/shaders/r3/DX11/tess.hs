// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"
#include "DX11\tess.h"

HS_CONSTANT_DATA_OUTPUT PatchConstantsHS( 
    InputPatch<p_bumped, 3> ip,
    uint PatchID : SV_PrimitiveID )
{	
    HS_CONSTANT_DATA_OUTPUT Output;

	ComputeTessFactor(Output.Edges, Output.Inside);
	
#ifdef TESS_PN
	float3 N[3] =
	{
		normalize(float3(ip[0].M1.z, ip[0].M2.z, ip[0].M3.z)),
		normalize(float3(ip[1].M1.z, ip[1].M2.z, ip[1].M3.z)),
		normalize(float3(ip[2].M1.z, ip[2].M2.z, ip[2].M3.z))
	};
	
	float3 P[3] = 
	{
		ip[0].position.xyz,
		ip[1].position.xyz,
		ip[2].position.xyz
	};
	
	ComputePNPatch(P, N, Output.patch);
	
//Discard back facing patches
#	ifndef TESS_HM
	bool doDiscard = (N[0].z>0.1) && (N[1].z>0.1) && (N[2].z>0.1) 
			&& (Output.patch.f3N110.z>0.1) && (Output.patch.f3N011.z>0.1) && (Output.patch.f3N101.z>0.1)
			&& (P[0].z>5) && (P[1].z>5) && (P[2].z>5);
			
	if (doDiscard)
		Output.Edges[0]= Output.Edges[1]=Output.Edges[2]=Output.Inside=-1;
#	endif
	
#endif

//	Data for interpolation in screen space
//	float w0 = mul(m_P, float4(ip[2].position.xyz, 1)).w;
//	float w1 = mul(m_P, float4(ip[1].position.xyz, 1)).w;
//	float w2 = mul(m_P, float4(ip[0].position.xyz, 1)).w;
	
//	Output.www = float3(w0, w1, w2);
	
    return Output;
}

[domain("tri")]
[partitioning("pow2")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantsHS")]
p_bumped main( InputPatch<p_bumped, 3> ip, 
                           uint i : SV_OutputControlPointID,
                           uint PatchID : SV_PrimitiveID )
{
    p_bumped ouput;

	ouput.tcdh	= ip[i].tcdh;
	ouput.position	= ip[i].position;
	ouput.M1		= ip[i].M1;
	ouput.M2		= ip[i].M2;
	ouput.M3		= ip[i].M3;

	ouput.vel_curr = ip[i].vel_curr;
	ouput.vel_prev = ip[i].vel_prev;

#ifdef USE_TDETAIL
	ouput.tcdbump	= ip[i].tcdbump;
#endif
#ifdef USE_LM_HEMI
	ouput.lmh		= ip[i].lmh;
#endif
	
    return ouput;
}


#else
#include "common.h"
#include "DX11\tess.h"

HS_CONSTANT_DATA_OUTPUT PatchConstantsHS( 
    InputPatch<p_bumped, 3> ip,
    uint PatchID : SV_PrimitiveID )
{	
    HS_CONSTANT_DATA_OUTPUT Output;

	ComputeTessFactor(Output.Edges, Output.Inside);
	
#ifdef TESS_PN
	float3 N[3] =
	{
		normalize(float3(ip[0].M1.z, ip[0].M2.z, ip[0].M3.z)),
		normalize(float3(ip[1].M1.z, ip[1].M2.z, ip[1].M3.z)),
		normalize(float3(ip[2].M1.z, ip[2].M2.z, ip[2].M3.z))
	};
	
	float3 P[3] = 
	{
		ip[0].position.xyz,
		ip[1].position.xyz,
		ip[2].position.xyz
	};
	
	ComputePNPatch(P, N, Output.patch);
	
//Discard back facing patches
#	ifndef TESS_HM
	bool doDiscard = (N[0].z>0.1) && (N[1].z>0.1) && (N[2].z>0.1) 
			&& (Output.patch.f3N110.z>0.1) && (Output.patch.f3N011.z>0.1) && (Output.patch.f3N101.z>0.1)
			&& (P[0].z>5) && (P[1].z>5) && (P[2].z>5);
			
	if (doDiscard)
		Output.Edges[0]= Output.Edges[1]=Output.Edges[2]=Output.Inside=-1;
#	endif
	
#endif

//	Data for interpolation in screen space
//	float w0 = mul(m_P, float4(ip[2].position.xyz, 1)).w;
//	float w1 = mul(m_P, float4(ip[1].position.xyz, 1)).w;
//	float w2 = mul(m_P, float4(ip[0].position.xyz, 1)).w;
	
//	Output.www = float3(w0, w1, w2);
	
    return Output;
}

[domain("tri")]
[partitioning("pow2")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantsHS")]
p_bumped main( InputPatch<p_bumped, 3> ip, 
                           uint i : SV_OutputControlPointID,
                           uint PatchID : SV_PrimitiveID )
{
    p_bumped ouput;

	ouput.tcdh	= ip[i].tcdh;
	ouput.position	= ip[i].position;
	ouput.M1		= ip[i].M1;
	ouput.M2		= ip[i].M2;
	ouput.M3		= ip[i].M3;
#ifdef USE_TDETAIL
	ouput.tcdbump	= ip[i].tcdbump;
#endif
#ifdef USE_LM_HEMI
	ouput.lmh		= ip[i].lmh;
#endif
	
    return ouput;
}


#endif
