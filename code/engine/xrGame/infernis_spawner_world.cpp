#include "pch_script.h"
#include "infernis_spawner_world.h"
#include "infernis_spawner_service_rules.h"
#include "object_factory.h"
#include "Actor.h"
#include "Level.h"
#include "ai_space.h"
#include "ai_object_location.h"
#include "level_graph.h"
#include "game_graph.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "xrServer.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "xrServer_Objects_ALife.h"
#include "space_restrictor.h"
#include "restriction_space.h"
#include "string_table.h"
#include "character_info.h"
#include "specific_character.h"
#include "../xrEngine/Environment.h"
#include <cerrno>
#include <limits>

namespace infernis_spawner {
namespace {
xr_string value(LPCSTR section, LPCSTR key) {
    if (!pSettings || !pSettings->line_exist(section, key)) return "";
    LPCSTR raw = pSettings->r_string(section, key);
    if (!raw) return "";
    xr_string result = raw;
    const auto first = result.find_first_not_of(" \t\r\n\"");
    if (first == xr_string::npos) return "";
    return result.substr(first, result.find_last_not_of(" \t\r\n\"") - first + 1);
}

bool bool_value(LPCSTR section, LPCSTR key) {
    const xr_string text = value(section, key);
    return !_stricmp(text.c_str(), "true") || !_stricmp(text.c_str(), "on") ||
           !_stricmp(text.c_str(), "yes") || text == "1";
}

bool finite_position(const Fvector& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

SpawnResult failure(LPCSTR message) {
    SpawnResult result;
    result.error = message;
    return result;
}

int lua_result(lua_State* L, const SpawnResult& result) {
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, result.ok()); lua_setfield(L, -2, "ok");
    lua_pushinteger(L, result.created); lua_setfield(L, -2, "created");
    lua_pushinteger(L, result.quantity); lua_setfield(L, -2, "quantity");
    lua_pushstring(L, result.error.c_str()); lua_setfield(L, -2, "error");
    return 1;
}

void string_field(lua_State* L, LPCSTR key, LPCSTR text) {
    lua_pushstring(L, text ? text : "");
    lua_setfield(L, -2, key);
}

bool valid_world_section(LPCSTR section, WorldKind expected) {
    if (!pSettings || !section || !section[0] || !pSettings->section_exist(section)) return false;
    const xr_string class_id = value(section, "class");
    if (class_id.empty() || class_id.size() > 8 || world_class(class_id) != expected) return false;
    if (!object_factory().has_class(TEXT2CLSID(class_id.c_str()))) return false;
    if (bool_value(section, "spawner_hidden")) return false;
    if (expected == WorldKind::npc) {
        const xr_string profile = value(section, "character_profile");
        const ITEM_DATA* item = CCharacterInfo::FindById(profile.c_str());
        if (!item) return false;
        XML_NODE node = item->_xml->NavigateToNode(item->_xml->GetRoot(), "character", item->pos_in_file);
        if (!node) return false;
        LPCSTR specific = item->_xml->Read(node, "specific_character", 0, "");
        if (specific && specific[0]) {
            const ITEM_DATA* character = CSpecificCharacter::FindById(specific);
            if (!character) return false;
            XML_NODE actual = character->_xml->NavigateToNode(character->_xml->GetRoot(), "specific_character", character->pos_in_file);
            if (!actual || !character->_xml->Read(actual, "visual", 0, "")[0]) return false;
        } else if (!item->_xml->Read(node, "class", 0, "")[0]) return false;
    }
    if (expected == WorldKind::monster || expected == WorldKind::physics) {
        const xr_string visual = value(section, "visual");
        if (visual.empty() || !_stricmp(visual.c_str(), "default")) return false;
    }
    if (expected == WorldKind::anomaly) {
        const xr_string hit = value(section, "hit_type");
        if (!valid_anomaly_hit(hit)) return false;
        const xr_string power = value(section, "max_start_power");
        if (power.empty()) return false;
        char* end = nullptr; errno = 0;
        const double parsed = strtod(power.c_str(), &end);
        if (errno || end == power.c_str() || *end || !std::isfinite(parsed) || parsed < 0 ||
            parsed > std::numeric_limits<float>::max()) return false;
    }
    return true;
}

xr_string display_name(LPCSTR section) {
    xr_string key = value(section, "spawner_name");
    if (key.empty()) key = value(section, "inv_name");
    if (key.empty()) return section;
    CStringTable table;
    const shared_str translated = table.translate(key.c_str());
    LPCSTR text = translated.c_str();
    return text && text[0] && xr_strcmp(text, key.c_str()) ? text : key;
}

struct InitData { WorldKind kind; double radius; };

bool initialize_world(CSE_Abstract* entity, void* context) {
    const auto& data = *static_cast<const InitData*>(context);
    const auto kind = data.kind;
    if (kind == WorldKind::npc && !smart_cast<CSE_ALifeHumanStalker*>(entity)) return false;
    if (kind == WorldKind::monster && !smart_cast<CSE_ALifeMonsterAbstract*>(entity)) return false;
    if (kind == WorldKind::physics && !smart_cast<CSE_ALifeObjectPhysic*>(entity)) return false;
    if (kind == WorldKind::anomaly) {
        auto* zone = smart_cast<CSE_ALifeCustomZone*>(entity);
        if (!zone) return false;
        const float radius = static_cast<float>(data.radius);
        CShapeData::shape_def shape;
        shape.data.sphere.P.set(0.f, 0.f, 0.f);
        shape.data.sphere.R = radius;
        shape.type = CShapeData::cfSphere;
        zone->assign_shapes(&shape, 1);
        zone->m_space_restrictor_type = RestrictionSpace::eRestrictorTypeNone;
        zone->m_owner_id = u32(-1);
        if (auto* anomaly = smart_cast<CSE_ALifeAnomalousZone*>(entity)) {
            anomaly->m_artefact_spawn_count = 0;
            anomaly->m_artefact_position_offset = 0;
        }
    }
    return true;
}

bool current_context(game_sv_Single*& game, CActor*& actor) {
    if (!g_pGameLevel || !IsGameTypeSingle() || !Level().Server || !Level().Server->game ||
        !ai().get_alife() || !ai().get_level_graph() || !ai().get_game_graph()) return false;
    game = smart_cast<game_sv_Single*>(Level().Server->game);
    actor = Actor();
    return game && actor && actor->g_Alive();
}

bool smart_destination(const CSE_ALifeDynamicObject* object) {
    if (!object) return false;
    const xr_string id = value(object->s_name.c_str(), "class");
    return id == "SMRTTRRN" || (bool_value(object->s_name.c_str(), "spawner_location") &&
           smart_cast<const CSE_ALifeSmartZone*>(object) != nullptr);
}

int get_world_catalog(lua_State* L) {
    const LPCSTR category_name = luaL_checkstring(L, 1);
    const WorldKind requested = world_category(category_name);
    if (requested == WorldKind::none)
        return luaL_error(L, "infernis_spawner.get_world_catalog: unknown category '%s'", category_name);
    struct Entry { xr_string section, name; };
    xr_vector<Entry> entries;
    if (pSettings) for (const CInifile::Sect* item : pSettings->sections()) {
        LPCSTR section = item->Name.c_str();
        if (valid_world_section(section, requested)) entries.push_back({section, display_name(section)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const int result = _stricmp(a.name.c_str(), b.name.c_str());
        return result ? result < 0 : a.section < b.section;
    });
    lua_createtable(L, static_cast<int>(entries.size()), 0);
    for (u32 i = 0; i < entries.size(); ++i) {
        lua_createtable(L, 0, 3);
        string_field(L, "section", entries[i].section.c_str());
        string_field(L, "name", entries[i].name.c_str());
        string_field(L, "kind", world_kind_name(requested));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int get_weather_catalog(lua_State* L) {
    lua_newtable(L);
    if (!g_pGamePersistent) return 1;
    struct Entry { xr_string section, name; bool fx; };
    xr_vector<Entry> entries;
    const auto& env = g_pGamePersistent->Environment();
    for (const auto& item : env.WeatherCycles)
        if (!item.second.empty()) entries.push_back({item.first.c_str(), xr_string("[cycle] ") + item.first.c_str(), false});
    for (const auto& item : env.WeatherFXs)
        if (!item.second.empty()) entries.push_back({item.first.c_str(), xr_string("[FX] ") + item.first.c_str(), true});
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    for (u32 i = 0; i < entries.size(); ++i) {
        lua_createtable(L, 0, 3);
        string_field(L, "section", entries[i].section.c_str());
        string_field(L, "name", entries[i].name.c_str());
        lua_pushboolean(L, entries[i].fx); lua_setfield(L, -2, "fx");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int get_locations(lua_State* L) {
    const bool nearby = lua_toboolean(L, 1) != 0;
    const double range = luaL_optnumber(L, 2, 20.0);
    if (!valid_distance(range))
        return luaL_error(L, "infernis_spawner.get_locations: range must be in 0..1000 metres");
    lua_newtable(L);
    if (!ai().get_alife() || !ai().get_game_graph() || !Actor() || !std::isfinite(range) || range < 0 || range > 1000) return 1;
    int index = 1;
    const auto& registry = ai().alife().objects().objects();
    for (const auto& pair : registry) {
        const CSE_ALifeDynamicObject* object = pair.second;
        if (!smart_destination(object) || !ai().game_graph().valid_vertex_id(object->m_tGraphID)) continue;
        const auto* vertex = ai().game_graph().vertex(object->m_tGraphID);
        if (nearby) {
            if (vertex->level_id() != ai().level_graph().level_id() || Actor()->Position().distance_to(object->o_Position) > range) continue;
        }
        string64 token; xr_sprintf(token, "object:%u", object->ID);
        lua_createtable(L, 0, 5);
        string_field(L, "section", token);
        string_field(L, "name", object->name_replace());
        string_field(L, "kind", "smart");
        string_field(L, "object_name", object->name_replace());
        lua_pushinteger(L, object->ID); lua_setfield(L, -2, "object_id");
        lua_pushnumber(L, Actor()->Position().distance_to(object->o_Position)); lua_setfield(L, -2, "distance");
        lua_rawseti(L, -2, index++);
    }
    if (!nearby) {
        xr_set<u32> levels;
        const u32 count = ai().game_graph().header().vertex_count();
        for (u32 id = 0; id < count; ++id) {
            const auto* vertex = ai().game_graph().vertex(id);
            if (!finite_position(vertex->level_point()) || vertex->level_vertex_id() == u32(-1) ||
                !levels.insert(vertex->level_id()).second) continue;
            string64 token; xr_sprintf(token, "vertex:%u", id);
            LPCSTR level_name = ai().game_graph().header().level(vertex->level_id()).name().c_str();
            lua_createtable(L, 0, 3);
            string_field(L, "section", token); string_field(L, "name", level_name); string_field(L, "kind", "level");
            lua_rawseti(L, -2, index++);
        }
    }
    return 1;
}

int get_nearby_smarts(lua_State* L) {
    const double range = luaL_optnumber(L, 1, 20.0);
    lua_settop(L, 0);
    lua_pushboolean(L, true);
    lua_pushnumber(L, range);
    return get_locations(L);
}

int lua_spawn_world(lua_State* L) {
    SpawnRequest request;
    request.section = luaL_checkstring(L, 1);
    request.distance = luaL_optnumber(L, 2, 0);
    request.quantity = luaL_optnumber(L, 3, 1);
    const double radius = luaL_optnumber(L, 4, 3);
    request.target = spawn_target(luaL_optstring(L, 5, "ahead"));
    return lua_result(L, spawn_world(request, radius));
}

int lua_teleport(lua_State* L) { return lua_result(L, teleport_destination(luaL_checkstring(L, 1))); }
} // namespace

WorldKind world_type(LPCSTR section) {
    if (!pSettings || !section || !pSettings->section_exist(section)) return WorldKind::none;
    return world_class(value(section, "class"));
}

SpawnResult spawn_world(const SpawnRequest& request, double radius) {
    if (request.target != SpawnTarget::ahead && request.target != SpawnTarget::crosshair)
        return failure("World objects support only ahead or crosshair target");
    if (!request.section || !request.section[0] || xr_strlen(request.section) >= sizeof(string64))
        return failure("Invalid section name (expected 1..63 bytes)");
    if (!valid_distance(request.distance)) return failure("Distance must be a finite value in 0..1000 metres");
    const BatchPlan batch = plan_batch(request.quantity, false);
    if (batch.error[0]) return failure(batch.error);
    const WorldKind kind = world_type(request.section);
    if (kind == WorldKind::none || !valid_world_section(request.section, kind))
        return failure("Section is not a supported or valid NPC, monster or anomaly");
    if (kind == WorldKind::anomaly && !valid_anomaly_radius(radius))
        return failure("Anomaly radius must be a finite value in (0..100] metres");

    game_sv_Single* game = nullptr; CActor* actor = nullptr;
    if (!current_context(game, actor)) return failure("A loaded single-player ALife game with a living actor is required");
    Fvector position = actor->Position();
    if (request.target == SpawnTarget::crosshair) {
        collide::rq_result hit;
        if (Level().ObjectSpace.RayPick(Device.vCameraPosition, Device.vCameraDirection, 50.f, collide::rqtBoth, hit, actor)) {
            position.mad(Device.vCameraPosition, Device.vCameraDirection, hit.range); position.y += .2f;
        } else position.mad(Device.vCameraPosition, Device.vCameraDirection, 3.f);
    } else position.mad(actor->Position(), actor->Direction(), static_cast<float>(request.distance));
    if (!finite_position(position) || !ai().level_graph().valid_vertex_position(position))
        return failure("Spawn position is outside the level AI graph");
    const u32 lv = ai().level_graph().vertex_id(position);
    if (!ai().level_graph().valid_vertex_id(lv)) return failure("Spawn position has no valid AI vertex");
    if ((kind == WorldKind::npc || kind == WorldKind::monster) &&
        !creature_position_valid(ai().level_graph().inside(lv, position),
        ai().level_graph().vertex_plane_y(ai().level_graph().vertex(lv), position.x, position.z) - position.y))
        return failure("Creature spawn position is too far from the AI plane");
    const auto& cross = ai().cross_table();
    if (lv >= cross.header().level_vertex_count()) return failure("Spawn vertex is outside the level cross table");
    const auto gv = cross.vertex(lv).game_vertex_id();
    if (!ai().game_graph().valid_vertex_id(gv) || ai().game_graph().vertex(gv)->level_id() != ai().level_graph().level_id())
        return failure("Spawn position has no game vertex on the current level");
    InitData init{world_type(request.section), radius};
    SpawnResult result;
    for (u32 i = 0; i < batch.objects; ++i) {
        CSE_Abstract* object = game->alife().spawn_item_initialized(request.section, position, lv, gv,
            ALife::_OBJECT_ID(-1), initialize_world, &init);
        if (!object) { result.error = "The server rejected the world spawn; earlier objects remain created"; break; }
        ++result.created; ++result.quantity;
    }
    return result;
}

SpawnResult teleport_destination(LPCSTR destination) {
    const DestinationToken token = parse_destination(destination);
    if (!token.valid) return failure("Invalid destination token");
    game_sv_Single* game = nullptr; CActor* actor = nullptr;
    if (!current_context(game, actor)) return failure("No living actor in a loaded single-player game");
    GameGraph::_GRAPH_ID gv;
    u32 lv; Fvector position;
    if (token.object) {
        const CSE_ALifeDynamicObject* object = ai().alife().objects().object(token.id, true);
        if (!smart_destination(object)) return failure("Destination object is unavailable or not marked spawner_location");
        gv = object->m_tGraphID; lv = object->m_tNodeID; position = object->o_Position;
    } else {
        gv = token.id;
        if (!ai().game_graph().valid_vertex_id(gv)) return failure("Destination game vertex does not exist");
        const auto* vertex = ai().game_graph().vertex(gv);
        lv = vertex->level_vertex_id(); position = vertex->level_point();
    }
    if (!ai().game_graph().valid_vertex_id(gv) || !finite_position(position)) return failure("Destination data is invalid");
    const auto* vertex = ai().game_graph().vertex(gv);
    if (vertex->level_id() == ai().level_graph().level_id()) {
        if (!ai().level_graph().valid_vertex_id(lv) || !ai().level_graph().valid_vertex_position(position))
            return failure("Destination is outside the current level graph");
        Fmatrix transform = actor->XFORM(); transform.c = position; actor->ForceTransform(transform);
    } else {
        lv = vertex->level_vertex_id(); position = vertex->level_point();
        NET_Packet packet; packet.w_begin(M_CHANGE_LEVEL); packet.w_u16(gv); packet.w_u32(lv); packet.w_vec3(position);
        packet.w_vec3(Fvector().set(0.f, 0.f, 0.f)); Level().Send(packet, net_flags(TRUE));
    }
    return {};
}

void register_world_api(lua_State* L) {
    static const luaL_Reg functions[] = {
        {"get_world_catalog", get_world_catalog}, {"get_weather_catalog", get_weather_catalog},
        {"get_locations", get_locations}, {"get_nearby_smarts", get_nearby_smarts},
        {"spawn_world", lua_spawn_world}, {"teleport", lua_teleport}, {nullptr, nullptr}
    };
    luaL_register(L, "infernis_spawner", functions);
    lua_pop(L, 1);
}
} // namespace infernis_spawner
