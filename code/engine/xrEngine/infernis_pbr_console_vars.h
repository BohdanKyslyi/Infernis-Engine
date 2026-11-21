#pragma once

// Infernis PBR - what the native settings page drives.
//
// Defined in xrEngine/xr_ioc_cmd.cpp and exported for the same reason as the Screen Space Shaders
// set ( ssfx_console_vars.h ): the renderers, the game module and the menu all read them.

// The player's choice for the two settings that are COMPILED INTO the shaders. Shown and written
// by the settings page, kept in user.ltx. Their factory values are the two lines of
// [shader_extensions] in configs\infernis_engine\engine_external.ltx.
extern ENGINE_API int ps_pbr_enable;
extern ENGINE_API u32 ps_pbr_sslr_mode; // 0 off, 1 performance, 2 balanced, 3 quality, 4 reference

// What this session runs with: the two above as they stood when the game started. A shader and
// the choice of shader per material are built from them at different moments, so a change made
// while the game runs must not reach whatever happens to be built next - part of the scene would
// be drawn one way and part the other. It takes effect with the next start.
extern ENGINE_API int pbr_session_enable;
extern ENGINE_API u32 pbr_session_sslr_mode;

// Sent to the shaders every frame ( Blender_Recorder_StandartBinding.cpp ): these act at once.
// Each component stands for one number of shaders\r3\infernis_pbr_lighting.h, and starts at the
// value that file ships with.
extern ENGINE_API Fvector4 ps_pbr_cal_diffuse;  // sun, local lights, ambient, metal energy
extern ENGINE_API Fvector4 ps_pbr_cal_specular; // sun, local lights, ambient, metal roughness gain
extern ENGINE_API Fvector4 ps_pbr_highlight;    // sun peak, local peak, AA strength, AA variance
extern ENGINE_API Fvector4 ps_pbr_surface;      // minimum roughness, -, -, -

// Called by the engine start-up ( x_ray.cpp ), in this order.
void pbr_settings_config_loaded();      // the game settings are read, user.ltx is not yet
void pbr_settings_user_config_loaded(); // user.ltx has been executed, no shader is built yet
