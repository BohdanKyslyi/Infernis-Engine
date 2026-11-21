/**
 * @ Description: Enhanced Shaders and Color Grading 1.10
 * @ Author: https://www.moddb.com/members/kennshade
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/enhanced-shaders-and-color-grading-for-151
 */
 
//=================================================================================================
//Settings for PBR conversion
//=================================================================================================
// Screen Space Shaders ( this port ): every setting below comes from the engine, so the SSFX
// menu can change it; the donor's own values are the defaults. The two switches are compiled in:
// r4.cpp passes SSFX_ES_BURLEY / SSFX_ES_GGX from the console settings ssfx_es_burley /
// ssfx_es_ggx, so a change takes a restart. A compile without them ( a shader tool ) keeps both
// on, as the donor's file has them.
#if !defined(SSFX_ES_BURLEY) || SSFX_ES_BURLEY
#define USE_BURLEY_DIFFUSE //use expensive Disney/Burley diffuse
#endif
#if !defined(SSFX_ES_GGX) || SSFX_ES_GGX
#define USE_GGX_SPECULAR //use more expensive GGX specular
#endif
//=================================================================================================
// The numbers are four console settings ( ssfx_es_albedo, ssfx_es_rough, ssfx_es_spec,
// ssfx_es_metal ) the engine binds like every other SSFX setting
// ( Blender_Recorder_StandartBinding.cpp ), so a change shows at once. The donor's values, which
// are their defaults, stay in the comments.
uniform float4 ssfx_es_albedo; // ALBEDO_BOOST, ALBEDO_AMOUNT
uniform float4 ssfx_es_rough;  // ROUGHNESS_LOW, ROUGHNESS_HIGH, ROUGHNESS_POW
uniform float4 ssfx_es_spec;   // SPECULAR_BASE, SPECULAR_RANGE, SPECULAR_POW
uniform float4 ssfx_es_metal;  // METAL_BOOST, METALNESS_THRESHOLD

#define ALBEDO_BOOST ssfx_es_albedo.x // 0.0
#define ALBEDO_AMOUNT ssfx_es_albedo.y // 1.00

#define ROUGHNESS_LOW ssfx_es_rough.x // 0.5
#define ROUGHNESS_HIGH ssfx_es_rough.y // 1.0
#define ROUGHNESS_POW ssfx_es_rough.z // 1.0

#define SPECULAR_BASE ssfx_es_spec.x // 0.01
#define SPECULAR_RANGE ssfx_es_spec.y // 1.0
#define SPECULAR_POW ssfx_es_spec.z // 1.0

#define METAL_BOOST ssfx_es_metal.x // 0.25
#define METALNESS_THRESHOLD ssfx_es_metal.y // 0.125
#define METALNESS_SOFTNESS 0.125 // read by nothing - pbr_brdf.h names it only in comments

// The menu step ( 2026-10-14 ): eight numbers the donor writes straight into its formulas are
// settings of the SSS window as well - three more console settings ( ssfx_es_light, ssfx_es_flora,
// ssfx_es_water ), bound the same way. Their defaults are the donor's numbers, in the comments.
uniform float4 ssfx_es_light;  // n.l power, horizon falloff, glow strength
uniform float4 ssfx_es_flora;  // flora specular, flora sky cut, light through leaves
uniform float4 ssfx_es_water;  // sun highlight on water: reflectance, roughness

#define ES_NDOTL_POW ssfx_es_light.x // 2 - pbr_brdf.h Lit_BRDF, the donor squares nDotL
#define ES_HORIZON ssfx_es_light.y // 2.0f - pbr_brdf.h Lit_BRDF, horizon falloff of the highlight
#define ES_EMISSIVE ssfx_es_light.z // 16 - accum_emissive.ps, glow strength

#define ES_FLORA_SPECULAR ssfx_es_flora.x // 0.05f - pbr_brdf.h calc_foliage
#define ES_FLORA_SKY_CUT ssfx_es_flora.y // 0.75f - hmodel.h, the sky reflection flora loses
#define ES_FLORA_SSS ssfx_es_flora.z // x1 - lmodel.h compute_lighting, light through leaves

#define ES_WATER_F0 ssfx_es_water.x // 0.02 - lmodel.h specular_phong, reflectance
#define ES_WATER_ROUGH ssfx_es_water.y // 0.1 - lmodel.h specular_phong, roughness