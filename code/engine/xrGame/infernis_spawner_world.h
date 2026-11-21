#pragma once

#include "infernis_spawner_service.h"
#include "infernis_spawner_world_rules.h"

struct lua_State;

namespace infernis_spawner {
WorldKind world_type(LPCSTR section);
SpawnResult spawn_world(const SpawnRequest& request, double radius = 3.0);
SpawnResult teleport_destination(LPCSTR destination);
void register_world_api(lua_State* L);
} // namespace infernis_spawner
