#pragma once

// Screen Space Shaders - engine side of the addon's script bridge.
//
// The SSFX gameplay scripts (rain footsteps, HUD raindrops, weapon DOF, LUT, ...) are written for
// Anomaly, whose Lua framework fires events such as on_game_load, actor_on_update or on_key_press.
// Call of Pripyat scripts have no such events, so the engine forwards them to ssfx_callbacks.script,
// which dispatches them with Anomaly's RegisterScriptCallback semantics.
//
// Every hook does nothing unless the addon ships ssfx_callbacks.script, so without the addon the
// game behaves exactly as stock Infernis.
namespace ssfx_script_bridge {
// After start_game_callback of every new or loaded game (the Lua state is rebuilt for each one).
void on_game_start();
// At the end of the player actor's net_Spawn: Anomaly's on_game_load.
void on_actor_spawn();
// Every shedule_Update of the player actor: actor_on_first_update, actor_on_update, time events.
void on_actor_update(u32 dt);
// The player's weapon zoomed in ( true ) or out ( false ): Anomaly's
// actor_on_weapon_zoom_in / actor_on_weapon_zoom_out, and the UIZoom show/hide pair it fires.
void on_weapon_zoom(bool zoom_in);
// A key press that reached the gameplay layer: not taken by a UI dialog, game not paused. The key
// is only reported, never consumed.
void on_key_press(int key);
// One footstep of the player, with the name of the material underfoot, the step's own volume and
// whether it was the first-person legs: Anomaly's actor_on_footstep, which ssfx_rain_footsteps.script
// uses to lay a wet splash over the step. Returning false suppresses the engine's own step sound,
// which is Anomaly's contract for this callback; with no addon installed it always returns true.
bool on_actor_footstep(LPCSTR material, float power, bool hud_view);
// The player jumped: Anomaly's actor_on_jump.
void on_actor_jump();
// The player landed, with the speed of the impact: Anomaly's actor_on_land.
void on_actor_land(float landing_speed);
// The player actor has been saved, its script binder too ( CActor::net_Save ): where Anomaly's actor
// binder sends on_level_changing when it is the save made on the way to another level
// ( bind_stalker_ext.script:198-206 ). Wind Leaves and the Anomaly weather scripts listen to it.
void on_actor_save();
} // namespace ssfx_script_bridge
