#include "pch_script.h"
#include "infernis_spawner_service.h"
#include "infernis_spawner_service_rules.h"
#include "infernis_spawner_catalog_rules.h"
#include "object_factory.h"
#include "Actor.h"
#include "ai_space.h"
#include "ai_object_location.h"
#include "Level.h"
#include "level_graph.h"
#include "game_graph.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "xrServer.h"
#include "xrServer_Objects_ALife_Items.h"
#include <cerrno>

// Reuse the engine's native ALife paths, including online-parent M_SPAWN processing.
// These functions are also the C++ implementation behind alife():create/create_ammo.
CSE_Abstract* CALifeSimulator__spawn_item2(CALifeSimulator*, LPCSTR, const Fvector&, u32,
                                          GameGraph::_GRAPH_ID, ALife::_OBJECT_ID);
CSE_Abstract* CALifeSimulator__spawn_ammo(CALifeSimulator*, LPCSTR, const Fvector&, u32,
                                         GameGraph::_GRAPH_ID, ALife::_OBJECT_ID, int);

namespace infernis_spawner {
namespace {
xr_string class_id(LPCSTR section) {
    if (!pSettings || !section || !pSettings->section_exist(section) ||
        !pSettings->line_exist(section, "class"))
        return "";
    LPCSTR raw = pSettings->r_string(section, "class");
    if (!raw || !raw[0])
        return "";
    const shared_str id = pSettings->r_string_wb(section, "class");
    return id.c_str() ? id.c_str() : "";
}

bool finite_position(const Fvector& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

unsigned ammo_box_size(LPCSTR section) {
    if (!pSettings->line_exist(section, "box_size"))
        return 0;
    LPCSTR value = pSettings->r_string(section, "box_size");
    if (!value || !value[0])
        return 0;
    char* end = nullptr;
    errno = 0;
    const long size = strtol(value, &end, 10);
    return !errno && end != value && !*end && size > 0 && size <= 65535
               ? static_cast<unsigned>(size) : 0;
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

int lua_spawn(lua_State* L) {
    SpawnRequest request;
    request.section = luaL_checkstring(L, 1);
    request.target = spawn_target(luaL_optstring(L, 2, "inventory"));
    request.quantity = luaL_optnumber(L, 3, 1);
    request.distance = luaL_optnumber(L, 4, 0);
    return lua_result(L, spawn(request));
}

int lua_spawn_ammo(lua_State* L) {
    SpawnRequest request;
    request.section = luaL_checkstring(L, 1);
    request.quantity = luaL_checknumber(L, 2);
    request.target = spawn_target(luaL_optstring(L, 3, "inventory"));
    request.distance = luaL_optnumber(L, 4, 0);
    request.ammo_units = true;
    return lua_result(L, spawn(request));
}
} // namespace

bool inventory_type(LPCSTR section) {
    return class_category(class_id(section).c_str()) != Category::none;
}

SpawnTarget spawn_target(LPCSTR name) {
    if (name && !xr_strcmp(name, "inventory")) return SpawnTarget::inventory;
    if (name && !xr_strcmp(name, "ahead")) return SpawnTarget::ahead;
    if (name && !xr_strcmp(name, "crosshair")) return SpawnTarget::crosshair;
    return SpawnTarget::invalid;
}

SpawnResult spawn(const SpawnRequest& request) {
    if (request.target == SpawnTarget::invalid)
        return failure("Unknown spawn target (inventory, ahead or crosshair)");
    if (!valid_distance(request.distance))
        return failure("Distance must be a finite value in 0..1000 metres");
    if (!request.section || !request.section[0] || xr_strlen(request.section) >= sizeof(string64))
        return failure("Invalid section name (expected 1..63 bytes)");
    if (!pSettings || !pSettings->section_exist(request.section))
        return failure("Section does not exist in the loaded settings");

    const xr_string id = class_id(request.section);
    if (id.empty() || id.size() > 8 || class_category(id.c_str()) == Category::none)
        return failure("Section is not a supported inventory item type");
    if (!object_factory().has_class(TEXT2CLSID(id.c_str())))
        return failure("Section class is not registered in the object factory");
    if (request.ammo_units && !ammo_class(id.c_str()))
        return failure("spawn_ammo requires an ammo section, not a weapon or grenade");
    const unsigned box_size = ammo_class(id.c_str()) ? ammo_box_size(request.section) : 0;
    if (ammo_class(id.c_str()) && !box_size)
        return failure("Invalid ammo box_size (expected 1..65535)");
    const BatchPlan batch = plan_batch(request.quantity, request.ammo_units,
                                      request.ammo_units ? box_size : 0);
    if (batch.error[0])
        return failure(batch.error);

    if (!g_pGameLevel || !IsGameTypeSingle() || !Level().Server || !Level().Server->game ||
        !ai().get_alife() || !Actor() || !Actor()->g_Alive())
        return failure("A loaded single-player ALife game with a living actor is required");
    game_sv_Single* game = smart_cast<game_sv_Single*>(Level().Server->game);
    if (!game || !ai().get_level_graph() || !ai().get_game_graph())
        return failure("Level and game graphs are not available");

    CActor* actor = Actor();
    Fvector position = actor->Position();
    if (request.target == SpawnTarget::ahead) {
        const Fvector direction = actor->Direction();
        if (!finite_position(direction))
            return failure("Actor direction is invalid");
        position.mad(actor->Position(), direction, static_cast<float>(request.distance));
    } else if (request.target == SpawnTarget::crosshair) {
        if (!finite_position(Device.vCameraPosition) || !finite_position(Device.vCameraDirection))
            return failure("Camera position or direction is invalid");
        collide::rq_result hit;
        if (Level().ObjectSpace.RayPick(Device.vCameraPosition, Device.vCameraDirection,
                                        50.f, collide::rqtBoth, hit, actor)) {
            position.mad(Device.vCameraPosition, Device.vCameraDirection, hit.range);
            position.y += 0.2f;
        } else
            position.mad(Device.vCameraPosition, Device.vCameraDirection, 3.f);
    }

    const CLevelGraph& graph = ai().level_graph();
    if (!finite_position(position))
        return failure("Spawn position is invalid");
    // Attached items use their parent's AI location, including when the actor stands off-mesh.
    // vertex_id asserts outside its bounding box: world positions must be checked first.
    if (request.target != SpawnTarget::inventory && !graph.valid_vertex_position(position))
        return failure("Spawn position is outside the level AI graph");
    const u32 level_vertex = request.target == SpawnTarget::inventory
                                 ? actor->ai_location().level_vertex_id() : graph.vertex_id(position);
    if (!graph.valid_vertex_id(level_vertex))
        return failure("There is no AI vertex at the requested spawn position");
    const CGameLevelCrossTable& cross_table = ai().cross_table();
    if (level_vertex >= cross_table.header().level_vertex_count())
        return failure("Spawn vertex is outside the level cross table");
    const GameGraph::_GRAPH_ID game_vertex = cross_table.vertex(level_vertex).game_vertex_id();
    if (!ai().game_graph().valid_vertex_id(game_vertex) ||
        ai().game_graph().vertex(game_vertex)->level_id() != graph.level_id())
        return failure("Spawn position has no valid game vertex on the current level");

    const ALife::_OBJECT_ID parent = request.target == SpawnTarget::inventory
                                       ? actor->ID() : ALife::_OBJECT_ID(-1);
    SpawnResult result;
    for (unsigned index = 0; index < batch.objects; ++index) {
        CSE_Abstract* entity = request.ammo_units
            ? CALifeSimulator__spawn_ammo(&game->alife(), request.section, position, level_vertex,
                                          game_vertex, parent, rounds_in_box(batch, index))
            : CALifeSimulator__spawn_item2(&game->alife(), request.section, position, level_vertex,
                                           game_vertex, parent);
        if (!entity) {
            result.error = "The server rejected the spawn; any earlier objects in this request remain created";
            break;
        }
        ++result.created;
        result.quantity += request.ammo_units ? rounds_in_box(batch, index) : 1;
    }
    if (!result.ok())
        Msg("! [infernis_spawner] [%s]: %s (%u objects created)", request.section,
            result.error.c_str(), result.created);
    return result;
}

void register_spawn_api(lua_State* L) {
    static const luaL_Reg functions[] = {
        {"spawn", lua_spawn}, {"spawn_ammo", lua_spawn_ammo}, {nullptr, nullptr}
    };
    luaL_register(L, "infernis_spawner", functions);
    lua_pop(L, 1);
}
} // namespace infernis_spawner
