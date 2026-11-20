#include "pch_script.h"
#include "infernis_spawner_runtime.h"
#include "infernis_spawner_tools.h"
#include "Actor.h"
#include "ActorCondition.h"
#include "Inventory.h"
#include "inventory_item.h"
#include "Weapon.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"
#include "UserBackpack.h"
#include "Level.h"
#include "ai_space.h"
#include "ai_object_location.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "game_graph.h"
#include "level_graph.h"
#include "xrServer_Objects_ALife.h"
#include "space_restrictor.h"
#include "map_manager.h"
#include "map_location.h"
#include "../xrEngine/Environment.h"
#include "../xrEngine/gamefont.h"
#include "../xrEngine/x_ray.h"

namespace infernis_spawner::runtime {
namespace {
State state;
bool active = false;
CActor* current_actor = nullptr;
CGameFont* font = nullptr;
u32 font_width = 0, font_height = 0, last_tick = 0, last_restrictors = 0, report_until = 0;
xr_string last_info, zones;
CMapLocation* anchor_marker = nullptr;

bool ready() {
    return active && g_pGameLevel && IsGameTypeSingle() && ai().get_alife();
}

// A fixed point needs no spawned bolt, story ID or serialized server entity.
class AnchorMarker final : public CMapLocation {
public:
    AnchorMarker() : CMapLocation("blue_location", u16(-1)) {
        m_owner_se_object = nullptr;
        m_flags.set(eHideInOffline | eTTL | ePosToActor, FALSE);
        SetSerializable(false);
        SetHint("Spawner: distance origin");
        Update();
    }
    bool Update() override {
        m_position_global.set(state.anchor[0], state.anchor[1], state.anchor[2]);
        m_cached.m_Position.set(state.anchor[0], state.anchor[2]);
        m_cached.m_LevelName = state.anchor_level;
        m_cached.m_Direction.set(0.f, 0.f);
        m_cached.m_updatedFrame = Device.dwFrame;
        return m_cached.m_Actuality = state.has(distance) && !state.anchor_level.empty();
    }
};

void remove_marker() {
    if (anchor_marker && g_pGameLevel && IsGameTypeSingle())
        Level().MapManager().RemoveMapLocation(anchor_marker);
    anchor_marker = nullptr;
}

void update_marker(CActor& actor) {
    if (!state.has(distance)) { remove_marker(); return; }
    if (state.anchor_level.empty()) {
        const auto& p = actor.Position();
        state.anchor[0] = p.x; state.anchor[1] = p.y; state.anchor[2] = p.z;
        state.anchor_level = Level().name().c_str();
    }
    if (!anchor_marker) {
        anchor_marker = xr_new<AnchorMarker>();
        SLocationKey key("blue_location", u16(-1));
        key.location = anchor_marker;
        Level().MapManager().Locations().push_back(key);
    }
}

bool select_weather(LPCSTR name, bool fx) {
    auto& env = g_pGamePersistent->Environment();
    if (fx) {
        if (env.WeatherFXs.find(name) == env.WeatherFXs.end()) return false;
        if (env.IsWFXPlaying()) env.StopWFX();
        return env.SetWeatherFX(name);
    }
    if (env.WeatherCycles.find(name) == env.WeatherCycles.end()) return false;
    if (env.IsWFXPlaying()) env.StopWFX();
    env.SetWeather(name, true);
    return true;
}

void update_weather() {
    if (!state.has(weather_lock) || state.weather.empty() ||
        !g_pGamePersistent || g_pGamePersistent->Environment().IsWeatherEditorSessionActive()) return;
    auto& env = g_pGamePersistent->Environment();
    if (state.has(weather_fx)) {
        if (!env.IsWFXPlaying() || env.CurrentWeatherName != state.weather)
            select_weather(state.weather.c_str(), true);
    } else if (env.IsWFXPlaying() || env.CurrentCycleName != state.weather)
        select_weather(state.weather.c_str(), false);
}

int result(lua_State* L, LPCSTR error = "") {
    lua_createtable(L, 0, 2);
    lua_pushboolean(L, !*error); lua_setfield(L, -2, "ok");
    lua_pushstring(L, error); lua_setfield(L, -2, "error");
    return 1;
}

int get_option(lua_State* L) {
    LPCSTR name = luaL_checkstring(L, 1);
    if (!xr_strcmp(name, "god_weather_type")) {
        lua_pushstring(L, state.weather.empty() ? "none" : state.weather.c_str());
        return 1;
    }
    const auto flag = option(name);
    if (flag) lua_pushboolean(L, active && state.has(flag));
    else if (lua_gettop(L) >= 2) lua_pushvalue(L, 2);
    else lua_pushnil(L);
    return 1;
}

int set_option(lua_State* L) {
    LPCSTR name = luaL_checkstring(L, 1);
    if (!ready() || !current_actor) return result(L, "Spawner runtime is not active in a loaded single-player game");
    if (!xr_strcmp(name, "god_weather_type")) {
        LPCSTR value = luaL_checkstring(L, 2);
        if (xr_strlen(value) > 127) return result(L, "Weather name is too long");
        if (xr_strcmp(value, "none")) {
            const auto& env = g_pGamePersistent->Environment();
            if (env.WeatherCycles.find(value) == env.WeatherCycles.end() &&
                env.WeatherFXs.find(value) == env.WeatherFXs.end())
                return result(L, "Unknown weather name");
        }
        state.weather = !xr_strcmp(value, "none") ? "" : value;
        return result(L);
    }
    const auto flag = option(name);
    if (!flag || !lua_isboolean(L, 2)) return result(L, "Unknown option or non-boolean value");
    const bool value = lua_toboolean(L, 2) != 0;
    if (flag == distance && value != state.has(distance)) {
        remove_marker(); state.anchor_level.clear();
    }
    state.set(flag, value);
    if (flag == distance) update_marker(*current_actor);
    return result(L);
}

int set_weather(lua_State* L) {
    LPCSTR name = luaL_checkstring(L, 1);
    if (!ready() || !current_actor || !g_pGamePersistent) return result(L, "No loaded single-player game");
    if (xr_strlen(name) > 127) return result(L, "Weather name is too long");
    auto& env = g_pGamePersistent->Environment();
    if (env.IsWeatherEditorSessionActive()) return result(L, "Close the weather editor before selecting weather");
    // An explicit flag keeps identically named weather cycles and FX selectable.
    bool fx = env.WeatherCycles.find(name) == env.WeatherCycles.end();
    if (lua_gettop(L) >= 2) {
        if (!lua_isboolean(L, 2)) return result(L, "Weather type must be boolean");
        fx = lua_toboolean(L, 2) != 0;
    }
    if (!select_weather(name, fx)) return result(L, "Weather cycle/effect is not loaded");
    state.weather = name; state.set(weather_fx, fx);
    return result(L);
}

int teleport_to_object(lua_State* L) {
    const double raw = luaL_checknumber(L, 1);
    if (!std::isfinite(raw) || raw < 0 || raw >= 65535 || std::floor(raw) != raw)
        return result(L, "Invalid destination object ID");
    if (!ready() || !current_actor || !current_actor->g_Alive()) return result(L, "No living actor in a loaded single-player game");
    const auto* obj = ai().alife().objects().object(static_cast<u16>(raw), true);
    if (!obj || !ai().game_graph().valid_vertex_id(obj->m_tGraphID)) return result(L, "Destination is no longer available");
    const auto* vertex = ai().game_graph().vertex(obj->m_tGraphID);
    Fvector pos = obj->o_Position;
    u32 lv = obj->m_tNodeID;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return result(L, "Invalid destination position");
    if (vertex->level_id() == ai().level_graph().level_id()) {
        const auto& graph = ai().level_graph();
        if (!graph.valid_vertex_position(pos) || !graph.valid_vertex_id(lv)) return result(L, "Destination is outside the current level graph");
        Fmatrix transform = current_actor->XFORM(); transform.c = pos;
        current_actor->ForceTransform(transform);
    } else {
        // The target graph is not loaded yet; use its verified game-graph point.
        // This also avoids trusting a stale server node ID from a different map.
        const auto gv = obj->m_tGraphID;
        lv = vertex->level_vertex_id(); pos = vertex->level_point();
        NET_Packet packet;
        packet.w_begin(M_CHANGE_LEVEL);
        packet.w_u16(gv); packet.w_u32(lv); packet.w_vec3(pos);
        packet.w_vec3(Fvector().set(0.f, 0.f, 0.f));
        Level().Send(packet, net_flags(TRUE));
    }
    return result(L);
}

int position_report(lua_State* L) {
    if (!ready() || !current_actor) return result(L, "No loaded actor");
    const auto& p = current_actor->Position();
    Msg("* [infernis_spawner] %s: position %.3f %.3f %.3f, lv %u, gv %u", Level().name().c_str(),
        p.x, p.y, p.z, current_actor->ai_location().level_vertex_id(), current_actor->ai_location().game_vertex_id());
    report_until = Device.dwTimeGlobal + 15000;
    return result(L);
}
} // namespace

bool enabled(Option flag) { return current_actor && ready() && state.has(flag); }

void release_ui() { xr_delete(font); font_width = font_height = 0; }

void on_game_start() {
    tools::reset(false);
    release_ui();
    // The previous map registry may already be gone: never dereference its marker.
    anchor_marker = nullptr; current_actor = nullptr; state = State{};
    last_info.clear(); zones.clear(); last_tick = last_restrictors = report_until = 0;
    active = false;
    string_path path;
    if (FS.exist(path, "$game_config$", "infernis_engine\\spawner_runtime.ltx")) {
        CInifile settings(path);
        active = settings.section_exist("spawner_runtime") &&
            settings.line_exist("spawner_runtime", "enabled") && settings.r_bool("spawner_runtime", "enabled");
    }
}

void on_actor_spawn(CActor& actor) {
    if (!ready()) return;
    current_actor = &actor;
    // Stage 1/2 could serialize a map spot on the old helper bolt. Keep its
    // server entity (old-save compatibility), but retire its obsolete HUD marker.
    xr_vector<CMapLocation*> legacy;
    for (const auto& key : Level().MapManager().Locations()) {
        const auto* object = ai().alife().objects().object(key.object_id, true);
        if (object && object->s_name == "bolt_dist" && key.spot_type == "blue_location")
            legacy.push_back(key.location);
    }
    for (auto* location : legacy) Level().MapManager().RemoveMapLocation(location);
}

void on_actor_destroy(CActor& actor) {
    if (&actor != current_actor) return;
    tools::reset(true);
    remove_marker(); release_ui(); current_actor = nullptr;
    last_info.clear(); zones.clear(); report_until = 0;
}

void on_actor_update(CActor& actor) {
    if (!ready() || &actor != current_actor || !actor.g_Alive()) return;
    if (state.has(power)) actor.conditions().MaxPower();
    if (state.has(bleeding)) actor.conditions().ClearWounds();
    const u32 now = Device.dwTimeGlobal;
    if (u32(now - last_tick) >= 500) {
        last_tick = now;
        if (state.has(repair)) {
            auto& inventory = actor.inventory();
            for (u16 slot = inventory.FirstSlot(); slot <= inventory.LastSlot(); ++slot) {
                auto* item = inventory.ItemFromSlot(slot);
                if (item && (smart_cast<CWeapon*>(item) || smart_cast<CCustomOutfit*>(item) ||
                    smart_cast<CHelmet*>(item) || smart_cast<CBackpack*>(item)) && item->GetCondition() < .95f)
                    item->SetCondition(1.f);
            }
        }
        update_weather(); update_marker(actor);
    }
    if (state.has(restrictors) && u32(now - last_restrictors) >= 2000) {
        last_restrictors = now; zones.clear();
        Fsphere sphere; sphere.set(actor.Position(), 0.1f);
        for (u32 i = 0; i < Level().Objects.o_count(); ++i) {
            auto* zone = smart_cast<CSpaceRestrictor*>(Level().Objects.o_get_by_iterator(i));
            if (zone && zone->inside(sphere)) {
                if (!zones.empty()) zones += ", ";
                if (zones.size() < 700) zones += zone->cName().c_str();
            }
        }
    }
}

void on_info(const CActor& actor, const char* id) {
    if (!ready() || &actor != current_actor || !state.has(info) || !id) return;
    last_info = id;
    Msg("* [infernis_spawner] info: %s", id);
}

void render() {
    if (!ready() || !current_actor || !current_actor->g_Alive()) return;
    const bool report = report_until && static_cast<s32>(report_until - Device.dwTimeGlobal) > 0;
    if (!(state.flags & (position | info | restrictors | distance)) && !report) return;
    if (font && (font_width != Device.dwWidth || font_height != Device.dwHeight)) release_ui();
    if (!font) {
        if (!pSettings->section_exist("hud_font_medium")) return;
        font = xr_new<CGameFont>("hud_font_medium", CGameFont::fsDeviceIndependent);
        font_width = Device.dwWidth; font_height = Device.dwHeight;
    }
    font->SetColor(color_rgba(220, 230, 220, 255));
    font->SetAligment(CGameFont::alLeft);
    font->SetHeightI(.021f); font->OutSetI(-.96f, -.88f);
    if (state.has(position) || report) {
        const auto& p = current_actor->Position(); const auto& d = current_actor->Direction();
        const auto lv = current_actor->ai_location().level_vertex_id();
        const bool on_graph = ai().level_graph().valid_vertex_id(lv) &&
            ai().level_graph().vertex_position(lv).distance_to(p) <= .7f;
        font->OutNext("%s: %.3f, %.3f, %.3f", Level().name().c_str(), p.x, p.y, p.z);
        font->OutNext("LV: %u  GV: %u  Direction: %.3f, %.3f, %.3f", current_actor->ai_location().level_vertex_id(),
            current_actor->ai_location().game_vertex_id(), d.x, d.y, d.z);
        font->OutNext("Heading: %.5f  AI graph: %s", d.getH(), on_graph ? "yes" : "no");
    }
    if (state.has(distance) && !state.anchor_level.empty()) {
        if (state.anchor_level == Level().name().c_str()) {
            const Fvector origin = Fvector().set(state.anchor[0], state.anchor[1], state.anchor[2]);
            font->OutNext("Distance: %.3f m", current_actor->Position().distance_to(origin));
        } else font->OutNext("Distance origin: %s", state.anchor_level.c_str());
    }
    if (state.has(info)) font->OutNext("Info: %.700s", last_info.c_str());
    if (state.has(restrictors)) font->OutNext("Restrictors: %.700s", zones.c_str());
    font->OnRender();
}

void save(IWriter& stream) {
    if (!active) return;
    const auto data = encode(state);
    if (data.empty()) return;
    stream.open_chunk(save_chunk); stream.w(data.data(), static_cast<u32>(data.size())); stream.close_chunk();
}

void load(IReader& stream) {
    state = State{}; anchor_marker = nullptr;
    last_info.clear(); zones.clear(); report_until = 0;
    IReader* payload = stream.open_chunk(save_chunk);
    if (!payload) return; // Save made before stage 3: all options start off.
    if (active && !decode(static_cast<const std::uint8_t*>(payload->pointer()), payload->length(), state))
        Msg("! [infernis_spawner] Ignoring unsupported or damaged runtime save chunk");
    payload->close();
}

void register_api(lua_State* L) {
    const luaL_Reg functions[] = {{"get_option", get_option}, {"set_option", set_option},
        {"set_weather", set_weather}, {"teleport_to_object", teleport_to_object},
        {"position_report", position_report}, {nullptr, nullptr}};
    luaL_register(L, "infernis_spawner", functions); lua_pop(L, 1);
}
} // namespace infernis_spawner::runtime
