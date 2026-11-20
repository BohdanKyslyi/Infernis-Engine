#pragma once
#include "infernis_spawner_runtime_state.h"

class CActor;
class IReader;
class IWriter;
struct lua_State;

namespace infernis_spawner::runtime {
void on_game_start();
void on_actor_update(CActor& actor);
void on_actor_spawn(CActor& actor);
void on_actor_destroy(CActor& actor);
void on_info(const CActor& actor, const char* id);
void render();
void release_ui();
bool enabled(Option flag);
void save(IWriter& stream);
void load(IReader& stream);
void register_api(lua_State* L);
} // namespace infernis_spawner::runtime
