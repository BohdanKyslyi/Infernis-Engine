#include "pch_script.h"
#include "ssfx_script_bridge.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrEngine/ssfx_console_vars.h" // ssfx_session_enable

extern ENGINE_API int g_current_renderer; // xrEngine/EngineAPI.cpp: 4 = the DX11 renderer runs

namespace ssfx_script_bridge {
namespace {
// Decided once per game in on_game_start(): neither the SSS switch nor the renderer changes
// mid-session. Checking the file first matters - asking the script engine for a script that
// doesn't exist logs an error.
bool s_active = false;

// Calls ssfx_callbacks.<function>(args...). The script traps errors raised by the SSFX scripts (a
// broken effect must not take the game down) and returns them as text, "" when there are none:
// MASTER_GOLD compiles Lua's own log() out, so the engine log is the only place they can show up.
template <typename... Args>
void call(LPCSTR function, Args... args) {
    luabind::functor<LPCSTR> lua_function;
    if (!ai().script_engine().functor(function, lua_function))
        return;

    LPCSTR report = lua_function(args...);
    if (report && report[0])
        Msg("%s", report);
}
} // namespace

void on_game_start() {
    // The SSS scripts drive the DX11 renderer's SSS modules only: with SSS switched off, or with
    // another renderer running, none of them starts and the game runs as it does without SSS.
    s_active = ssfx_session_enable != 0 && g_current_renderer == 4 &&
               !!FS.exist("$game_scripts$", "ssfx_callbacks.script");
    if (s_active)
        call("ssfx_callbacks.engine_game_start");
}

void on_actor_spawn() {
    if (s_active)
        call("ssfx_callbacks.engine_actor_spawn");
}

void on_actor_update(u32 dt) {
    if (s_active)
        call("ssfx_callbacks.engine_actor_update", dt);
}

void on_key_press(int key) {
    if (s_active)
        call("ssfx_callbacks.engine_key_press", key);
}

void on_weapon_zoom(bool zoom_in) {
    if (s_active)
        call("ssfx_callbacks.engine_weapon_zoom", zoom_in);
}

bool on_actor_footstep(LPCSTR material, float power, bool hud_view) {
    if (!s_active)
        return true;

    // This one answers with the veto instead of a log line, because Anomaly lets a listener stop the
    // step sound (_g.script:1259-1263). Anything the script has to say waits in its own list and
    // goes out with the next actor update, which is the very next frame.
    luabind::functor<bool> lua_function;
    if (!ai().script_engine().functor("ssfx_callbacks.engine_actor_footstep", lua_function))
        return true;

    return lua_function(material ? material : "", power, hud_view);
}

void on_actor_jump() {
    if (s_active)
        call("ssfx_callbacks.engine_actor_jump");
}

void on_actor_land(float landing_speed) {
    if (s_active)
        call("ssfx_callbacks.engine_actor_land", landing_speed);
}

void on_actor_save() {
    if (s_active)
        call("ssfx_callbacks.engine_actor_save");
}
} // namespace ssfx_script_bridge
