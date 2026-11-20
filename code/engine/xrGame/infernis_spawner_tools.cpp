#include "pch_script.h"
#include "infernis_spawner_tools.h"
#include "infernis_spawner_tools_rules.h"
#include "Actor.h"
#include "Level.h"
#include "ai_space.h"
#include "ai_object_location.h"
#include "game_graph.h"
#include "level_graph.h"
#include "character_community.h"
#include "game_sv_single.h"
#include "xrServer.h"
#include "map_manager.h"
#include "map_location.h"
#include "string_table.h"
#include "ui/UIGameTutorial.h"
#include "ui/xrUIXmlParser.h"
#include "UIGameCustom.h"
#include "script_ini_file.h"

extern CUISequencer* g_tutorial;

namespace infernis_spawner::tools {
namespace {
ref_sound music;
struct Point { Fvector p; u32 lv; GameGraph::_GRAPH_ID gv; xr_string animation, signal; bool flags; };
xr_vector<Point> points;
xr_string point_level;
xr_vector<CMapLocation*> markers;

bool ready() {
    return g_pGameLevel && IsGameTypeSingle() && Actor() && Actor()->g_Alive() &&
           Level().Server && ai().get_alife() && ai().get_game_graph() && ai().get_level_graph();
}
int result(lua_State* L, LPCSTR error = "") {
    lua_createtable(L, 0, 2);
    lua_pushboolean(L, !error[0]); lua_setfield(L, -2, "ok");
    lua_pushstring(L, error); lua_setfield(L, -2, "error");
    return 1;
}
void text_field(lua_State* L, LPCSTR key, LPCSTR value) {
    lua_pushstring(L, value); lua_setfield(L, -2, key);
}
void number_field(lua_State* L, LPCSTR key, double value) {
    lua_pushnumber(L, value); lua_setfield(L, -2, key);
}
bool read_file(LPCSTR filename, std::string& data) {
    IReader* reader = FS.r_open("$app_data_root$", filename);
    if (!reader) return false;
    if (reader->length() > 256 * 1024) { FS.r_close(reader); return false; }
    data.assign(static_cast<const char*>(reader->pointer()), reader->length());
    FS.r_close(reader);
    return true;
}
bool write_file(LPCSTR filename, const std::string& data) {
    string_path path, temp;
    FS.update_path(path, "$app_data_root$", filename);
    strconcat(sizeof(temp), temp, path, ".tmp");
    IWriter* writer = FS.w_open_ex(temp);
    if (!writer) return false;
    if (!writer->valid()) { FS.w_close(writer); return false; }
    writer->w(data.data(), data.size());
    FS.w_close(writer);
    // Verify complete temporary bytes before replacing the previous file.
    IReader* check = FS.r_open(temp);
    const bool complete = check && check->length() == data.size() &&
        !memcmp(check->pointer(), data.data(), data.size());
    if (check) FS.r_close(check);
    if (!complete) return false;
    // Keep the prior user file recoverable even if the OS rejects the rename.
    string_path previous_path;
    if (FS.exist(previous_path, "$app_data_root$", filename)) {
        std::string previous;
        if (!read_file(filename, previous)) return false;
        string_path backup; strconcat(sizeof(backup), backup, path, ".bak");
        FS.file_copy(path, backup);
        IReader* saved = FS.r_open(backup);
        const bool backed_up = saved && saved->length() == previous.size() &&
            !memcmp(saved->pointer(), previous.data(), previous.size());
        if (saved) FS.r_close(saved);
        if (!backed_up) return false;
    }
    FS.file_rename(temp, path, true);
    std::string saved;
    return read_file(filename, saved) && saved == data;
}
bool load_bookmarks(std::vector<Bookmark>& entries) {
    string_path path;
    if (!FS.exist(path, "$app_data_root$", "infernis_spawner_bookmarks.txt")) return true;
    std::string data;
    return read_file("infernis_spawner_bookmarks.txt", data) && decode_bookmarks(data, entries);
}
int actor_state(lua_State* L) {
    lua_createtable(L, 0, 10);
    lua_pushboolean(L, ready()); lua_setfield(L, -2, "ready");
    if (ready()) {
        const auto& p = Actor()->Position();
        text_field(L, "level", Level().name().c_str());
        number_field(L, "x", p.x); number_field(L, "y", p.y); number_field(L, "z", p.z);
        number_field(L, "money", Actor()->get_money());
        number_field(L, "time_factor", Level().GetGameTimeFactor());
        number_field(L, "lv", Actor()->ai_location().level_vertex_id());
        number_field(L, "gv", Actor()->ai_location().game_vertex_id());
    }
    return 1;
}
int get_bookmarks(lua_State* L) {
    std::vector<Bookmark> entries;
    if (!load_bookmarks(entries)) return luaL_error(L, "Bookmark file is damaged; it has not been overwritten");
    lua_createtable(L, entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& b = entries[i];
        lua_createtable(L, 0, 6);
        text_field(L, "name", b.name.c_str()); text_field(L, "level", b.level.c_str());
        text_field(L, "section", std::to_string(i + 1).c_str());
        number_field(L, "x", b.x); number_field(L, "y", b.y); number_field(L, "z", b.z);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}
int add_bookmark(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    const double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4);
    if (!ready()) return result(L, "No living actor in a loaded single-player game");
    if (!clean_text(name) || !finite_point(x, y, z)) return result(L, "Invalid bookmark name or coordinates");
    std::vector<Bookmark> entries;
    if (!load_bookmarks(entries)) return result(L, "Bookmark file is damaged; no changes were written");
    Bookmark b{name, Level().name().c_str(), float(x), float(y), float(z)};
    bool replaced = false;
    for (auto& existing : entries) if (existing.name == b.name && existing.level == b.level) {
        existing = b; replaced = true; break;
    }
    if (!replaced) {
        if (entries.size() >= 256) return result(L, "Maximum 256 bookmarks");
        entries.push_back(b);
    }
    return result(L, write_file("infernis_spawner_bookmarks.txt", encode_bookmarks(entries)) ? "" : "Could not save bookmarks");
}
int teleport_position(lua_State* L) {
    const double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2), z = luaL_checknumber(L, 3);
    if (!ready()) return result(L, "No living actor in a loaded single-player game");
    if (!finite_point(x, y, z)) return result(L, "Invalid teleport coordinates");
    Fvector p; p.set(float(x), float(y), float(z));
    const auto& graph = ai().level_graph();
    if (!graph.valid_vertex_position(p)) return result(L, "Teleport position is outside the current AI graph");
    const u32 lv = graph.vertex_id(p);
    if (!graph.valid_vertex_id(lv) || !graph.inside(*graph.vertex(lv), p))
        return result(L, "Teleport position has no AI cell");
    Fmatrix transform = Actor()->XFORM(); transform.c = p; Actor()->ForceTransform(transform);
    return result(L);
}
int teleport_bookmark(lua_State* L) {
    const double raw = luaL_checknumber(L, 1);
    std::vector<Bookmark> entries;
    if (!std::isfinite(raw) || raw < 1 || raw > 256 || floor(raw) != raw ||
        !load_bookmarks(entries) || raw > entries.size()) return result(L, "Bookmark is no longer available");
    if (!ready()) return result(L, "No living actor in a loaded single-player game");
    const auto& b = entries[static_cast<size_t>(raw) - 1];
    // Loading a different map requires an explicit level transition first. Never
    // reuse a foreign map's coordinates or stale vertex IDs on the current map.
    if (b.level != Level().name().c_str()) return result(L, "Bookmark belongs to another level; select that level first");
    lua_settop(L, 0); lua_pushnumber(L, b.x); lua_pushnumber(L, b.y); lua_pushnumber(L, b.z);
    return teleport_position(L);
}
int move_ahead(lua_State* L) {
    const double distance = luaL_optnumber(L, 1, 5);
    if (!ready() || !std::isfinite(distance) || distance < 0 || distance > 1000)
        return result(L, "No loaded actor or invalid distance");
    Fvector p; p.mad(Actor()->Position(), Actor()->Direction(), float(distance));
    lua_settop(L, 0); lua_pushnumber(L, p.x); lua_pushnumber(L, p.y); lua_pushnumber(L, p.z);
    return teleport_position(L);
}
int set_hud(lua_State* L) {
    if (!ready() || !lua_isboolean(L, 1) || !CurrentGameUI()) return result(L, "No loaded UI or invalid HUD flag");
    const bool visible = lua_toboolean(L, 1) != 0;
    CurrentGameUI()->ShowGameIndicators(visible);
    CurrentGameUI()->ShowCrosshair(visible);
    return result(L);
}
int set_money(lua_State* L) {
    const double amount = luaL_checknumber(L, 1);
    if (!ready()) return result(L, "No living actor in a loaded single-player game");
    if (!std::isfinite(amount) || amount < 0 || amount > 4294967295.0 || floor(amount) != amount)
        return result(L, "Money must be an integer in 0..4294967295");
    Actor()->set_money(static_cast<u32>(amount), true);
    return result(L);
}
int set_time(lua_State* L) {
    const double factor = luaL_checknumber(L, 1);
    if (!ready() || !Level().Server->game) return result(L, "No loaded single-player game");
    if (!std::isfinite(factor) || factor < .01 || factor > 1000000) return result(L, "Time factor must be in 0.01..1000000");
    Level().Server->game->SetGameTimeFactor(float(factor));
    return result(L);
}
int get_communities(lua_State* L) {
    lua_newtable(L);
    if (!ready()) return 1;
    // The initialized relation registry, not a hard-coded faction list.
    for (int i = 0; i <= CHARACTER_COMMUNITY::GetMaxIndex(); ++i) {
        const shared_str id = CHARACTER_COMMUNITY::IndexToId(i);
        lua_createtable(L, 0, 2);
        text_field(L, "section", id.c_str()); text_field(L, "name", *CStringTable().translate(id.c_str()));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}
int set_community(lua_State* L) {
    LPCSTR id = luaL_checkstring(L, 1);
    if (!ready()) return result(L, "No living actor in a loaded single-player game");
    const auto index = CHARACTER_COMMUNITY::IdToIndex(id, -1, true);
    if (index < 0) return result(L, "Community is not registered");
    CHARACTER_COMMUNITY community; community.set(index);
    Actor()->SetCommunity(index); Actor()->ChangeTeam(community.team(), 0, 0);
    return result(L);
}
int get_music_catalog(lua_State* L) {
    string_path path; FS.update_path(path, "$game_sounds$", "music\\");
    FS_FileSet files; FS.file_list(files, path, FS_ListFiles | FS_ClampExt, "*.ogg");
    lua_newtable(L); int index = 1;
    for (const auto& file : files) {
        const std::string name = std::string("music\\") + file.name.c_str();
        lua_createtable(L, 0, 2);
        text_field(L, "section", name.c_str()); text_field(L, "name", name.c_str());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}
int get_squad_catalog(lua_State* L) {
    lua_pushvalue(L, 1);
    luabind::detail::lua_reference argument_ref;
    argument_ref.set(L);
    const luabind::object argument(L, argument_ref, true);
    const auto converted = luabind::object_cast_nothrow<CScriptIniFile*>(argument);
    lua_newtable(L);
    if (converted.empty() || !*converted) return 1;
    const CInifile& ini = **converted;
    int index = 1;
    for (const CInifile::Sect* section : ini.sections()) {
        LPCSTR name = section->Name.c_str();
        if (!ini.line_exist(name, "npc") && !ini.line_exist(name, "npc_random")) continue;
        if (ini.line_exist(name, "spawner_hidden") && ini.r_bool(name, "spawner_hidden")) continue;
        LPCSTR label = ini.line_exist(name, "spawner_name") ? ini.r_string(name, "spawner_name") : name;
        lua_createtable(L, 0, 2);
        text_field(L, "section", name); text_field(L, "name", *CStringTable().translate(label));
        lua_rawseti(L, -2, index++);
    }
    return 1;
}
int play_music(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    if (!clean_text(name, 200) || name.substr(0, 6) != "music\\" || name.find("..") != std::string::npos || name.find(':') != std::string::npos)
        return result(L, "Select a sound inside the music directory");
    string_path path;
    if (!FS.exist(path, "$game_sounds$", (name + ".ogg").c_str())) return result(L, "Sound is not available");
    music.destroy(); music.create(name.c_str(), st_Music, sg_Undefined); music.play(nullptr, sm_2D);
    return result(L);
}
int stop_music(lua_State* L) { music.destroy(); return result(L); }

bool safe_sequence(XML_NODE node) {
    for (XML_NODE child = node.firstChild(); child; child = child.nextSibling()) {
        const std::string name = child.elementValue() ? child.elementValue() : "";
        if (name.find("function") != std::string::npos || name == "action" || name == "persistent") return false;
        if (!safe_sequence(child)) return false;
    }
    return true;
}
int get_video_catalog(lua_State* L) {
    lua_newtable(L);
    string_path path;
    if (!FS.exist(path, "$game_config$", "ui\\infernis_spawner_videos.xml")) return 1;
    CUIXml xml; xml.Load(CONFIG_PATH, UI_PATH, "infernis_spawner_videos.xml");
    int index = 1;
    for (XML_NODE child = xml.GetRoot().firstChild(); child; child = child.nextSibling()) {
        LPCSTR name = child.elementValue();
        if (!name || !name[0] || !xml.GetNodesNum(child, "item") || !safe_sequence(child)) continue;
        lua_createtable(L, 0, 2); text_field(L, "section", name); text_field(L, "name", name);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}
int play_video(lua_State* L) {
    LPCSTR name = luaL_checkstring(L, 1);
    if (g_tutorial) return result(L, "Close the active tutorial before previewing video");
    string_path path;
    if (!FS.exist(path, "$game_config$", "ui\\infernis_spawner_videos.xml")) return result(L, "Video catalog is not installed");
    CUIXml xml; xml.Load(CONFIG_PATH, UI_PATH, "infernis_spawner_videos.xml");
    XML_NODE node = xml.NavigateToNode(xml.GetRoot(), name, 0);
    if (!node || !xml.GetNodesNum(node, "item") || !safe_sequence(node)) return result(L, "Video sequence is missing or contains callbacks");
    g_tutorial = xr_new<CUISequencer>(); g_tutorial->Start(name, "infernis_spawner_videos.xml");
    return result(L);
}

class PointMarker final : public CMapLocation {
    Fvector point;
    std::string level;
public:
    PointMarker(const Fvector& p, LPCSTR map) : CMapLocation("blue_location", u16(-1)), point(p), level(map) {
        m_owner_se_object = nullptr; m_flags.set(eHideInOffline | eTTL | ePosToActor, FALSE);
        SetSerializable(false); SetHint("Spawner waypoint"); Update();
    }
    bool Update() override {
        m_position_global = point; m_cached.m_Position.set(point.x, point.z);
        m_cached.m_LevelName = level; m_cached.m_Direction.set(0, 0);
        m_cached.m_updatedFrame = Device.dwFrame; return m_cached.m_Actuality = true;
    }
};
void clear_markers() {
    if (g_pGameLevel) for (auto* marker : markers) Level().MapManager().RemoveMapLocation(marker);
    markers.clear();
}
int mark_waypoint(lua_State* L) {
    const std::string animation = luaL_optstring(L, 1, ""), signal = luaL_optstring(L, 2, "");
    const bool flags = lua_toboolean(L, 3) != 0;
    if (!ready()) return result(L, "No loaded actor");
    if ((!animation.empty() && !waypoint_identifier(animation)) || (!signal.empty() && !waypoint_identifier(signal)))
        return result(L, "Animation/signal must contain only letters, digits and underscores");
    if (points.size() >= 512) return result(L, "Maximum 512 waypoints per path");
    if (!points.empty() && point_level != Level().name().c_str()) return result(L, "Export or reset the path before changing level");
    point_level = Level().name().c_str();
    const auto& position = Actor()->Position();
    if (!finite_point(position.x, position.y, position.z)) return result(L, "Actor position is invalid");
    const auto lv = Actor()->ai_location().level_vertex_id(); const auto gv = Actor()->ai_location().game_vertex_id();
    if (!ai().level_graph().valid_vertex_id(lv) || !ai().game_graph().valid_vertex_id(gv)) return result(L, "Actor has no valid AI location");
    points.push_back({Actor()->Position(), lv, gv, animation.c_str(), signal.c_str(), flags});
    auto* marker = xr_new<PointMarker>(Actor()->Position(), point_level.c_str());
    SLocationKey key("blue_location", u16(-1)); key.location = marker;
    Level().MapManager().Locations().push_back(key); markers.push_back(marker);
    return result(L);
}
std::string waypoint_text(LPCSTR name, bool closed) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9);
    out << "; level = " << point_level.c_str() << "\n[" << name << "]\npoints = ";
    for (size_t i = 0; i < points.size(); ++i) out << (i ? "," : "") << 'p' << i;
    out << '\n';
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i]; const std::string prefix = "p" + std::to_string(i) + ":";
        out << prefix << "name = wp" << i;
        if (!p.animation.empty()) out << "|a=" << p.animation.c_str();
        if (!p.signal.empty()) out << "|sig=" << p.signal.c_str();
        out << '\n';
        if (p.flags) out << prefix << "flags = 0x1\n";
        out << prefix << "position = " << p.p.x << ',' << p.p.y << ',' << p.p.z << '\n';
        out << prefix << "game_vertex_id = " << p.gv << '\n' << prefix << "level_vertex_id = " << p.lv << '\n';
        if (i + 1 < points.size()) out << prefix << "links = p" << i + 1 << "(1)\n";
        else if (closed && points.size() > 1) out << prefix << "links = p0(1)\n";
    }
    return out.str();
}
int export_waypoints(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1); const bool closed = lua_toboolean(L, 2) != 0;
    if (!waypoint_identifier(name)) return result(L, "Path name must contain only letters, digits and underscores");
    if (points.empty()) return result(L, "Mark at least one point before export");
    const auto text = waypoint_text(name.c_str(), closed);
    const auto filename = "infernis_spawner_" + name + ".ltx";
    if (!write_file(filename.c_str(), text)) return result(L, "Could not write waypoint export");
    points.clear(); point_level.clear(); clear_markers(); return result(L);
}
int preview_waypoints(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    if (!waypoint_identifier(name)) return result(L, "Invalid path name");
    result(L); text_field(L, "text", waypoint_text(name.c_str(), lua_toboolean(L, 2) != 0).c_str());
    number_field(L, "count", points.size()); return 1;
}
int reset_waypoints(lua_State* L) { points.clear(); point_level.clear(); clear_markers(); return result(L); }
int print_position(lua_State* L) {
    const std::string comment = luaL_optstring(L, 1, "position");
    if (!ready()) return result(L, "No loaded actor");
    if (!clean_text(comment, 512)) return result(L, "Invalid comment");
    std::string previous; string_path path;
    if (FS.exist(path, "$app_data_root$", "infernis_spawner_positions.txt") && !read_file("infernis_spawner_positions.txt", previous))
        return result(L, "Position export exceeds 256 KiB; archive it before continuing");
    std::ostringstream line; line.imbue(std::locale::classic()); const auto& p = Actor()->Position();
    line << std::setprecision(9) << Level().name().c_str() << ' ' << p.x << ',' << p.y << ',' << p.z
        << " lv=" << Actor()->ai_location().level_vertex_id() << " gv=" << Actor()->ai_location().game_vertex_id()
        << " heading=" << Actor()->Direction().getH() << " ; " << comment << '\n';
    const auto text = previous + line.str();
    if (text.size() > 256 * 1024) return result(L, "Position export exceeds 256 KiB; archive it before continuing");
    return result(L, write_file("infernis_spawner_positions.txt", text) ? "" : "Could not write position export");
}
} // namespace

void reset(bool remove) {
    music.destroy(); points.clear(); point_level.clear();
    if (remove) clear_markers(); else markers.clear();
}
void register_api(lua_State* L) {
    static const luaL_Reg functions[] = {
        {"actor_state", actor_state}, {"get_bookmarks", get_bookmarks}, {"add_bookmark", add_bookmark},
        {"teleport_bookmark", teleport_bookmark}, {"teleport_position", teleport_position},
        {"move_ahead", move_ahead}, {"set_hud", set_hud},
        {"set_money", set_money}, {"set_time_factor", set_time},
        {"get_communities", get_communities}, {"set_community", set_community},
        {"get_music_catalog", get_music_catalog}, {"play_music", play_music}, {"stop_music", stop_music},
        {"get_squad_catalog", get_squad_catalog},
        {"get_video_catalog", get_video_catalog}, {"play_video", play_video},
        {"mark_waypoint", mark_waypoint}, {"export_waypoints", export_waypoints},
        {"preview_waypoints", preview_waypoints}, {"reset_waypoints", reset_waypoints},
        {"print_position", print_position}, {nullptr, nullptr}
    };
    luaL_register(L, "infernis_spawner", functions); lua_pop(L, 1);
}
} // namespace infernis_spawner::tools
