#pragma once
struct lua_State;
namespace infernis_spawner::tools {
void register_api(lua_State* L);
void reset(bool remove_markers);
}
