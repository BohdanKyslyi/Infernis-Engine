#pragma once

// Screen Space Shaders - console variables, together with the Monolith console variables the
// SSFX shaders use.
//
// These are defined in xrEngine/xr_ioc_cmd.cpp and exported as ENGINE_API because both sides of
// the engine use them: Environment drives its wind animation from ssfx_wind_trees and the wetness
// from ssfx_wetness_multiplier, and the renderers read the whole set. xrRender_console.cpp is
// compiled separately into every renderer DLL, so defining them there would give each DLL its own
// copy and leave the engine side unlinked. Names, defaults and ranges follow Monolith.

extern ENGINE_API Fvector4 ps_ssfx_floravariation;
extern ENGINE_API Fvector4 ps_ssfx_motionblur;
extern ENGINE_API Fvector4 ps_ssfx_taa;
extern ENGINE_API Fvector4 ps_ssfx_fog;
extern ENGINE_API float ps_ssfx_fog_scattering;

extern ENGINE_API int ps_ssfx_pom_refine;
extern ENGINE_API Fvector4 ps_ssfx_pom;

extern ENGINE_API int ps_ssfx_terrain_grass_align;
extern ENGINE_API float ps_ssfx_terrain_grass_slope;
extern ENGINE_API Fvector4 ps_ssfx_terrain_pom;
extern ENGINE_API int ps_ssfx_terrain_pom_refine;

extern ENGINE_API int ps_ssfx_bloom_use_presets;
extern ENGINE_API Fvector4 ps_ssfx_bloom_1;
extern ENGINE_API Fvector4 ps_ssfx_bloom_2;

extern ENGINE_API Fvector4 ps_ssfx_sss_quality;
extern ENGINE_API Fvector4 ps_ssfx_sss;

extern ENGINE_API float ps_ssfx_hud_hemi;

extern ENGINE_API int ps_ssfx_il_quality;
extern ENGINE_API Fvector4 ps_ssfx_il;
extern ENGINE_API Fvector4 ps_ssfx_il_setup1;

extern ENGINE_API int ps_ssfx_ao_quality;
extern ENGINE_API Fvector4 ps_ssfx_ao;
extern ENGINE_API Fvector4 ps_ssfx_ao_setup1;

extern ENGINE_API Fvector4 ps_ssfx_water;
extern ENGINE_API Fvector3 ps_ssfx_water_quality;
extern ENGINE_API Fvector4 ps_ssfx_water_setup1;
extern ENGINE_API Fvector4 ps_ssfx_water_setup2;

extern ENGINE_API int ps_ssfx_ssr_quality;
extern ENGINE_API Fvector4 ps_ssfx_ssr;
extern ENGINE_API Fvector4 ps_ssfx_ssr_2;

extern ENGINE_API Fvector4 ps_ssfx_terrain_quality;
extern ENGINE_API Fvector4 ps_ssfx_terrain_offset;

extern ENGINE_API Fvector3 ps_ssfx_shadows;
extern ENGINE_API Fvector4 ps_ssfx_volumetric;

extern ENGINE_API Fvector3 ps_ssfx_shadow_bias;

extern ENGINE_API Fvector4 ps_ssfx_lut;

extern ENGINE_API Fvector4 ps_ssfx_wind_grass;
extern ENGINE_API Fvector4 ps_ssfx_wind_trees;

extern ENGINE_API Fvector4 ps_ssfx_florafixes_1;
extern ENGINE_API Fvector4 ps_ssfx_florafixes_2;

extern ENGINE_API Fvector4 ps_ssfx_wetsurfaces_1;
extern ENGINE_API Fvector4 ps_ssfx_wetsurfaces_2;

extern ENGINE_API int ps_ssfx_is_underground;
extern ENGINE_API int ps_ssfx_gloss_method;
extern ENGINE_API float ps_ssfx_gloss_factor;
extern ENGINE_API Fvector4 ps_ssfx_gloss_minmax;

extern ENGINE_API Fvector4 ps_ssfx_lightsetup_1;

// The pseudo-PBR module ( pbr_settings.h ): four vectors bound to the shaders, two compiled-in switches.
extern ENGINE_API Fvector4 ps_ssfx_es_albedo;
extern ENGINE_API Fvector4 ps_ssfx_es_rough;
extern ENGINE_API Fvector4 ps_ssfx_es_spec;
extern ENGINE_API Fvector4 ps_ssfx_es_metal;
extern ENGINE_API int ps_ssfx_es_burley;
extern ENGINE_API int ps_ssfx_es_ggx;
// The same module: the eight numbers the donor writes into the formulas, three more vectors.
extern ENGINE_API Fvector4 ps_ssfx_es_light;
extern ENGINE_API Fvector4 ps_ssfx_es_flora;
extern ENGINE_API Fvector4 ps_ssfx_es_water;

extern ENGINE_API Fvector4 ps_ssfx_hud_drops_1;
extern ENGINE_API Fvector4 ps_ssfx_hud_drops_2;

extern ENGINE_API Fvector4 ps_ssfx_blood_decals;
extern ENGINE_API Fvector4 ps_ssfx_rain_1;
extern ENGINE_API Fvector4 ps_ssfx_rain_2;
extern ENGINE_API Fvector4 ps_ssfx_rain_3;
extern ENGINE_API Fvector4 ps_ssfx_rain_drops_setup;

// Registered by the renderer ( xrRender_console.cpp ): changing it calls CRender::init_cacades().
extern ENGINE_API Fvector3 ps_ssfx_shadow_cascades;
extern ENGINE_API Fvector4 ps_ssfx_grass_shadows;

extern ENGINE_API Fvector4 ps_ssfx_grass_interactive;
extern ENGINE_API Fvector4 ps_ssfx_int_grass_params_1;
extern ENGINE_API Fvector4 ps_ssfx_int_grass_params_2;

extern ENGINE_API Fvector4 ps_ssfx_wpn_dof_1;
extern ENGINE_API float ps_ssfx_wpn_dof_2;

extern ENGINE_API Fvector3 ssfx_wetness_multiplier;

// Tonemapping
extern ENGINE_API float ps_r2_tnmp_a;
extern ENGINE_API float ps_r2_tnmp_b;
extern ENGINE_API float ps_r2_tnmp_c;
extern ENGINE_API float ps_r2_tnmp_d;
extern ENGINE_API float ps_r2_tnmp_e;
extern ENGINE_API float ps_r2_tnmp_f;
extern ENGINE_API float ps_r2_tnmp_w;
extern ENGINE_API float ps_r2_tnmp_exposure;
extern ENGINE_API float ps_r2_tnmp_gamma;
extern ENGINE_API float ps_r2_tnmp_onoff;

// Mask control ( set by the bloom and combine phases )
extern ENGINE_API Fvector4 ps_r2_mask_control;

// Image corrections
extern ENGINE_API float ps_r2_img_exposure;
extern ENGINE_API float ps_r2_img_gamma;
extern ENGINE_API float ps_r2_img_saturation;
extern ENGINE_API Fvector ps_r2_img_cg;

// Shader params ( shader_param_1..8 )
extern ENGINE_API Fvector4 ps_dev_param_1;
extern ENGINE_API Fvector4 ps_dev_param_2;
extern ENGINE_API Fvector4 ps_dev_param_3;
extern ENGINE_API Fvector4 ps_dev_param_4;
extern ENGINE_API Fvector4 ps_dev_param_5;
extern ENGINE_API Fvector4 ps_dev_param_6;
extern ENGINE_API Fvector4 ps_dev_param_7;
extern ENGINE_API Fvector4 ps_dev_param_8;

// Not SSFX itself. Two Monolith sun-shaft controls the addon's settings expect to exist, kept in
// this header because it is the one the environment and both renderers already share.
// Consumed by CEnvDescriptorMixer::lerp.
extern ENGINE_API float ps_r2_sun_shafts_min;
extern ENGINE_API float ps_r2_sun_shafts_value;

// With the addon installed, the stock video options SSFX replaces or depends on are forced to the
// combination the modules need ( see CRender::create ). 0 hands the switches back to the player.
extern ENGINE_API int ps_ssfx_auto_settings;

// 1 = the native SSFX settings menu owns the module settings. The addon's applier scripts read it
// and stop pushing the defaults out of their own *_settings.script tables, which would otherwise
// overwrite the player's choices on every game start. 0 gives the scripts back their old role.
extern ENGINE_API int ps_ssfx_menu;

// 0 until the player applies something in the SSFX menu. While it is 0 the addon's appliers still
// run and lay down the reference values from their own *_settings.script tables, which is what a
// fresh installation needs - several engine defaults differ from them on purpose.
extern ENGINE_API int ps_ssfx_menu_configured;

// SSS on or off: the "SSS on/off" check box above "SSS Settings" on the video page. The SSS files
// are in gamedata, and every file there that SSS changes holds both versions - the SSS one and
// the game's own. At 0 the renderer, the .s scripts and the SSS scripts all take the game's own,
// and the game runs as it does without SSS. The player's choice, kept in user.ltx; its factory
// value is enable_ssfx of [shader_extensions] in configs\infernis_engine\engine_external.ltx.
extern ENGINE_API int ps_ssfx_enable;

// What this session runs with: ps_ssfx_enable as it stood when the game started. The modules,
// the .s scripts and the shader cache key are decided from it at different moments, so a change
// made while the game runs takes effect with the next start ( as pbr_session_enable ).
extern ENGINE_API int ssfx_session_enable;

// Called by the engine start-up ( x_ray.cpp ), in this order, each right after its PBR twin.
void ssfx_settings_config_loaded();      // the game settings are read, user.ltx is not yet
void ssfx_settings_user_config_loaded(); // user.ltx has been executed, no shader is built yet

// The two profiles ( user 2026-10-27, xr_ioc_cmd.cpp ): user.ltx is the game without SSS and keeps
// the SSS switch, user_sss.ltx is the game with SSS. Called by the engine start-up and shut-down
// ( x_ray.cpp ), in this order.
void ssfx_profile_select();      // the console is made: which of the two files is the profile
void ssfx_profile_renderer();    // the renderer line is read: with SSS, DX11
void ssfx_profile_cache_check(); // the renderer is loaded: a cache of another renderer, SSS or
                                 // set of SSS addon packs goes
void ssfx_profile_backup();      // the last save is done: a copy of each profile

// Not SSFX. Monolith (Swartz): the actor's own shadow in first person, console r__actor_shadow,
// off by default as in the donor ( xrRender_console.cpp:482 and :1339 ). Kept here for the same
// reason as the variables above: xrGame reads it in CActor::AllowActorShadow, which allows it on
// DX11 only.
extern ENGINE_API Flags32 ps_actor_shadow_flags;
enum {
    RFLAG_ACTOR_SHADOW = (1 << 0),
};
