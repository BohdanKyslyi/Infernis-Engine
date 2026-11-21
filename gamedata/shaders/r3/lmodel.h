// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#ifndef	LMODEL_H
#define LMODEL_H

#include "common.h"
#include "infernis_pbr_lighting.h"

// Screen Space Shaders: the pseudo-PBR module - Enhanced Shaders 1.10 as the SSFX folder patches
// it. Its switch is SSFX_ENHANCED_SHADERS, defined by check_screenspace_ES.h, as in the donor.
// This header is included before check_screenspace.h everywhere ( combine_1.ps, the accum_* passes,
// screenspace_common.h ), so it reads the switch itself; check_screenspace.h including the same
// file again later repeats an identical definition, which the compiler accepts.
#include "check_screenspace_ES.h"

#ifdef SSFX_ENHANCED_SHADERS
#include "common_brdf.h"
#include "pbr_brdf.h"

// The SSFX folder's lmodel.h below, word for word but for one number that is a setting of the SSS
// window now ( ES_FLORA_SSS, pbr_settings.h - x1 in the donor ): the Enhanced Shaders light model.
// It also takes the pixel's albedo and gloss ( c_tex ) and returns the complete lit colour in linear
// light, so every pass that calls it converts the light colour to linear light as well. Legacy
// pixels only - PBR pixels go through plight_infinity_pbr / plight_local_pbr further down.
//////////////////////////////////////////////////////////////////////////////////////////
// Lighting formulas

float4 compute_lighting(float3 N, float3 V, float3 L, float4 alb_gloss, float mat_id)
{
	// [ SSS Test ]. Overwrite terrain material
	bool m_terrain = abs(mat_id - 0.95) <= 0.04f;
	if (m_terrain)
		mat_id = 0;
	
	float3 albedo = calc_albedo(alb_gloss, mat_id);
	float3 specular = calc_specular(alb_gloss, mat_id);
	float rough = calc_rough(alb_gloss, mat_id);
	//calc_rain(albedo, specular, rough, alb_gloss, mat_id, 1);
	calc_foliage(albedo, specular, rough, alb_gloss, mat_id);
	
	float3 light = Lit_BRDF(rough, albedo, specular, V, N, L );

	//if(mat_id == MAT_FLORA) //Be aware of precision loss/errors
	if(abs(mat_id-MAT_FLORA) <= MAT_FLORA_ELIPSON) //Be aware of precision loss/errors
	{
		//Simple subsurface scattering
		float3 subsurface = SSS(N,V,L);
		light.rgb += subsurface*albedo*ES_FLORA_SSS; // this port: x1 in the donor, a setting ( pbr_settings.h )
	}

	return float4(light, 0);
}

float4 plight_infinity(float m, float3 pnt, float3 normal, float4 c_tex, float3 light_direction )
{
	//gsc vanilla stuff
	float3 N = normalize(normal);							// normal 
	float3 V = normalize(-pnt);					// vector2eye
	float3 L = normalize(-light_direction);						// vector2light

	float4 light = compute_lighting(N,V,L,c_tex,m);
	
	return light; // output (albedo.gloss)
}

float4 plight_local(float m, float3 pnt, float3 normal, float4 c_tex, float3 light_position, float light_range_rsq, out float rsqr )
{
	float atteps = 0.1;
	
	float3 L2P = pnt - light_position;                       		// light2point 
	rsqr = dot(L2P,L2P); // distance 2 light (squared)
	rsqr = max(rsqr, atteps);
	//rsqr = rsqr + 1.0;
	
	//vanilla atten - linear
	float att = saturate(1.0 - rsqr*light_range_rsq); // q-linear attenuate
	att = SRGBToLinear(att);
	/*
	//unity atten - quadtratic
	//catlikecoding.com/unity/tutorials/custom-srp/point-and-spot-lights/
	att = rsqr * light_range_rsq;
	att *= att;
	att = saturate(1.0 - att);
	att *= att;
	att = att / rsqr;
	*/
	float3 N = normalize(normal);							// normal 
	float3 V = normalize(-pnt);					// vector2eye
	float3 L = normalize(-L2P);					// vector2light

	float4 light = compute_lighting(N,V,L,c_tex,m);
	
	return att*light;		// output (albedo.gloss)
}
#else
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
#endif

// ============================================================
// Infernis Engine PBR directional light
// ============================================================

// Infernis PBR Stage 2.27:
// X-Ray's sun is authored around the legacy material LUT. Bridge the normalized
// Lambert lobe back to those diffuse units, but keep the independently proven
// GGX limit that prevents low-roughness metals from becoming white.
// A macro, like the three below it: the CAL numbers are uniforms of the PBR settings page now
// ( infernis_pbr_lighting.h ), and a global constant cannot be built from a uniform. Same arithmetic.
#define IE_PBR_SUN_DIFFUSE_SCALE (3.14159265f * IE_PBR_CAL_SUN_DIFFUSE)
#define IE_PBR_SUN_SPECULAR_SCALE (0.25f * IE_PBR_CAL_SUN_SPECULAR)

// Infernis PBR Stage 2.29:
// X-Ray local-light colors are authored around the legacy material LUT.
// Keep the Lambert PI bridge, but calibrate diffuse and GGX independently.
#define IE_PBR_LOCAL_DIFFUSE_SCALE (3.14159265f * IE_PBR_CAL_LOCAL_DIFFUSE)
#define IE_PBR_LOCAL_SPECULAR_SCALE (3.14159265f * IE_PBR_CAL_LOCAL_SPECULAR)

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

// ============================================================
// Screen Space Shaders: sun highlight on water
// ============================================================

#ifdef SSFX_ENHANCED_SHADERS
// With the pseudo-PBR module on, pbr_brdf.h is included above and this is the SSFX folder's own
// function ( lmodel.h:85-94 ), through Lit_Specular. Its reflectance 0.02 and roughness 0.1 are two
// settings of the SSS window now ( ES_WATER_F0 / ES_WATER_ROUGH, pbr_settings.h ), those by default.
float3 specular_phong(float3 pnt, float3 normal, float3 light_direction)
{
	float3 H = normalize(pnt + light_direction );
	float nDotL = saturate(dot(normal, light_direction));
	float nDotH = saturate(dot(normal, H));
	float nDotV = saturate(dot(normal, pnt));
	float lDotH = saturate(dot(light_direction, H));
	//float vDotH = saturate(dot(pnt, H));
	return L_sun_color.rgb * Lit_Specular(nDotL, nDotH, nDotV, lDotH, ES_WATER_F0, ES_WATER_ROUGH); // this port: 0.02 and 0.1 in the donor, settings ( pbr_settings.h )
}
#else
// water.ps is the only caller. SSFX keeps this function in lmodel.h too, in this
// same spot, but builds it out of its own PBR header ( pbr_brdf.h -> Lit_Specular
// -> Lit_GGX ). With the pseudo-PBR module off that header is not included: PBR in this
// engine is Infernis's.
// The highlight is therefore rebuilt from Infernis's own GGX, Smith and Fresnel
// terms, keeping SSFX's two constants - reflectance 0.02 and roughness 0.1 - which
// are what give water its tight sun glint.
//
// How closely the rebuilt term matches the one SSFX evaluates:
//   - distribution: ie_pbr_distribution_ggx is the same D_GGX formula, with the
//     same alpha = roughness * roughness. Identical.
//   - fresnel: SSFX fades towards saturate(50 * F0) = saturate(1.0) = 1.0, which
//     is exactly the 1.0 that ie_pbr_fresnel_schlick fades towards. Identical.
//   - visibility: Infernis uses the Schlick-k Smith form, SSFX the joint
//     approximation. Both return 0.25 head-on and stay close at this roughness.
//     Infernis's is also the safer one here: it clamps its denominators with
//     IE_PBR_EPSILON, where SSFX's takes rcp() of a sum that reaches zero on
//     water facing away from both the eye and the sun.
// IE_PBR_MIN_ROUGHNESS is a slider of the PBR settings page now: 0.08 as shipped, 0.10 at
// the top of its range - so the 0.1 below passes the clamp untouched at any setting.
//
// The three arguments are handed over exactly as SSFX hands them over, and are
// left that way on purpose: water.vs writes v2point_w as P - eye_position and
// L_sun_dir_w is the sun's direction of travel, so both are the negatives of the
// usual view and light vectors. Correcting the signs would change how the water
// looks away from what SSFX ships, so it is not done.
static const float3 SSFX_WATER_SPEC_F0    = float3(0.02f, 0.02f, 0.02f);
static const float  SSFX_WATER_SPEC_ROUGH = 0.1f;

float3 specular_phong( float3 pnt, float3 normal, float3 light_direction )
{
	float3 H		= normalize	( pnt + light_direction );
	float  nDotL	= saturate	( dot(normal, light_direction) );
	float  nDotH	= saturate	( dot(normal, H) );
	float  nDotV	= saturate	( dot(normal, pnt) );
	float  lDotH	= saturate	( dot(light_direction, H) );

	float  D	= ie_pbr_distribution_ggx	( nDotH, SSFX_WATER_SPEC_ROUGH );
	float  V	= ie_pbr_geometry_smith		( nDotL, nDotV, SSFX_WATER_SPEC_ROUGH );
	float3 F	= ie_pbr_fresnel_schlick	( SSFX_WATER_SPEC_F0, lDotH );

	return L_sun_color.rgb * (D * V) * F;
}
#endif

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
#else
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
#endif
