#ifndef	LMODEL_H
#define LMODEL_H

#include "common.h"
#include "infernis_pbr_lighting.h"

//////////////////////////////////////////////////////////////////////////////////////////
// Lighting formulas			// 
float4 plight_infinity( float m, float3 pnt, float3 normal, float3 light_direction )
{
  	float3 N			= normal;							// normal 
  	float3 V 		= -normalize	(pnt);					// vector2eye
  	float3 L 		= -light_direction;						// vector2light
  	float3 H			= normalize	(L+V);						// float-angle-vector 
//	return tex3D 		(s_material,	float3( dot(L,N), dot(H,N), m ) );		// sample material
	return s_material.Sample( smp_material, float3( dot(L,N), dot(H,N), m ) ).xxxy;		// sample material
}
/*
float plight_infinity2( float m, float3 pnt, float3 normal, float3 light_direction )
{
  	float3 N		= normal;							// normal 
  	float3 V 	= -normalize		(pnt);		// vector2eye
  	float3 L 	= -light_direction;					// vector2light
 	float3 H		= normalize			(L+V);			// float-angle-vector 
	float3 R     = reflect         	(-V,N);
	float 	s	= saturate(dot(L,R));
			s	= saturate(dot(H,N));
	float 	f 	= saturate(dot(-V,R));
			s  *= f;
	float4	r	= tex3D 			(s_material,	float3( dot(L,N), s, m ) );	// sample material
			r.w	= pow(saturate(s),4);
  	return	r	;
}
*/

float4 plight_local( float m, float3 pnt, float3 normal, float3 light_position, float light_range_rsq, out float rsqr )
{
  	float3 N		= normal;							// normal 
  	float3 L2P 	= pnt-light_position;                         		// light2point 
  	float3 V 	= -normalize	(pnt);					// vector2eye
  	float3 L 	= -normalize	((float3)L2P);					// vector2light
  	float3 H		= normalize	(L+V);						// float-angle-vector
		rsqr	= dot		(L2P,L2P);					// distance 2 light (squared)
  	float  att 	= saturate	(1 - rsqr*light_range_rsq);			// q-linear attenuate
//	float4 light	= tex3D		(s_material, float3( dot(L,N), dot(H,N), m ) ); 	// sample material
	float4 light	= s_material.Sample( smp_material, float3( dot(L,N), dot(H,N), m ) ).xxxy;		// sample material
  	return att*light;
}

// ============================================================
// Infernis Engine PBR directional light
// ============================================================

// Infernis PBR Stage 2.27:
// X-Ray's sun is authored around the legacy material LUT. Bridge the normalized
// Lambert lobe back to those diffuse units, but keep the independently proven
// GGX limit that prevents low-roughness metals from becoming white.
static const float IE_PBR_SUN_DIFFUSE_SCALE =
    3.14159265f * IE_PBR_CAL_SUN_DIFFUSE;
static const float IE_PBR_SUN_SPECULAR_SCALE =
    0.25f * IE_PBR_CAL_SUN_SPECULAR;

// Infernis PBR Stage 2.29:
// X-Ray local-light colors are authored around the legacy material LUT.
// Keep the Lambert PI bridge, but calibrate diffuse and GGX independently.
static const float IE_PBR_LOCAL_DIFFUSE_SCALE =
    3.14159265f * IE_PBR_CAL_LOCAL_DIFFUSE;
static const float IE_PBR_LOCAL_SPECULAR_SCALE =
    3.14159265f * IE_PBR_CAL_LOCAL_SPECULAR;

// Stage 2.17 diagnostic selector. Production/full = 0, diffuse-only = 1,
// specular-only = 2. The companion script changes only this define.
#define IE_PBR_SUN_LOBE_MODE 0

// Stage 2.18 accumulator-route diagnostic. Production = 0,
// zero PBR sun = 1, PBR/legacy branch marker = 2.
#define IE_PBR_SUN_ROUTE_MODE 0

// Stage 2.19 directional-pass diagnostic. Production = 0,
// near = red, far = blue, fullscreen/luminance = yellow.
#define IE_PBR_SUN_PASS_MODE 0

// Stage 2.23 local-light route diagnostic. Production = 0,
// PBR/legacy branch marker = 1.
#define IE_PBR_LOCAL_ROUTE_MODE 0

// Stage 2.24 local-light lobe selector. Production/full = 0,
// diffuse-only = 1, specular-only = 2, cosine/albedo probe = 3.
#define IE_PBR_LOCAL_LOBE_MODE 0

float4 plight_infinity_pbr(
    gbuffer_data gbd,
    float3 light_direction
)
{
    float3 albedo =
        ie_pbr_prepare_albedo(
            gbd.C
        );

    // Position is view-space.
    // Camera is at 0,0,0.
    float3 V =
        -normalize(gbd.P);

    // Classic X-Ray gives us light ray direction,
    // therefore surface -> light is the inverse.
    float3 L =
        -normalize(light_direction);

    float3 diffuseLobe;
    float3 specularLobe;

    ie_pbr_direct_brdf_lobes(
        albedo,
        gbd.metallic,
        gbd.roughness,
        gbd.N,
        V,
        L,
        IE_PBR_SUN_SPECULAR_LIMIT,
        0.0f,
        diffuseLobe,
        specularLobe
    );

#if IE_PBR_SUN_LOBE_MODE == 1
    float3 result =
        diffuseLobe *
        IE_PBR_SUN_DIFFUSE_SCALE;
#elif IE_PBR_SUN_LOBE_MODE == 2
    float3 result =
        specularLobe *
        IE_PBR_SUN_SPECULAR_SCALE;
#else
    float3 result =
        diffuseLobe *
        IE_PBR_SUN_DIFFUSE_SCALE +
        specularLobe *
        IE_PBR_SUN_SPECULAR_SCALE;
#endif

    // PBR direct lighting is entirely RGB.
    // Old scalar specular accumulator is unused.
    return float4(
        result,
        0.0f
    );
}


// ============================================================
// Infernis Engine PBR local light
// ============================================================

float4 plight_local_pbr(
    gbuffer_data gbd,
    float3 light_position,
    float light_range_rsq,
    out float rsqr
)
{
    float3 L2P =
        gbd.P - light_position;

    rsqr =
        dot(
            L2P,
            L2P
        );

    // Infernis PBR Stage 2.6:
    // keep X-Ray's attenuation curve, but transport the visibility factor
    // in the same linear-light domain as the PBR radiance.
    float att =
        ie_pbr_prepare_light_factor(
            saturate(
                1.0f -
                rsqr * light_range_rsq
            )
        );

    float3 V =
        -normalize(gbd.P);

    float3 L =
        -normalize(L2P);

    float3 albedo =
        ie_pbr_prepare_albedo(
            gbd.C
        );

    float3 diffuseLobe;
    float3 specularLobe;

    ie_pbr_direct_brdf_lobes(
        albedo,
        gbd.metallic,
        gbd.roughness,
        gbd.N,
        V,
        L,
        IE_PBR_LOCAL_SPECULAR_LIMIT,
        1.0f,
        diffuseLobe,
        specularLobe
    );

#if IE_PBR_LOCAL_LOBE_MODE == 1
    float3 result =
        diffuseLobe *
        IE_PBR_LOCAL_DIFFUSE_SCALE;
#elif IE_PBR_LOCAL_LOBE_MODE == 2
    float3 result =
        specularLobe *
        IE_PBR_LOCAL_SPECULAR_SCALE;
#elif IE_PBR_LOCAL_LOBE_MODE == 3
    // Independent probe for position, normal, and light direction.
    float NdotL =
        saturate(
            dot(
                normalize(gbd.N),
                L
            )
        );
    float3 result = albedo * NdotL;
#else
    float3 result =
        diffuseLobe *
        IE_PBR_LOCAL_DIFFUSE_SCALE +
        specularLobe *
        IE_PBR_LOCAL_SPECULAR_SCALE;
#endif

    return float4(
        result * att,
        0.0f
    );
}

//	TODO: DX10: Remove path without blending
float4 blendp( float4 value, float4 tcp)
{
//	#ifndef FP16_BLEND  
//		value 	+= (float4)tex2Dproj 	(s_accumulator, tcp); 	// emulate blend
//	#endif
	return 	value;
}

float4 blend( float4 value, float2 tc)
{
//	#ifndef FP16_BLEND  
//		value 	+= (float4)tex2D 	(s_accumulator, tc); 	// emulate blend
//	#endif
	return 	value;
}

#endif
