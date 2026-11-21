#pragma once

struct lua_State;

namespace infernis_spawner {
enum class SpawnTarget { inventory, ahead, crosshair, invalid };

struct SpawnRequest {
    LPCSTR section = "";
    SpawnTarget target = SpawnTarget::inventory;
    double quantity = 1;
    double distance = 0;
    bool ammo_units = false;
};

struct SpawnResult {
    u32 created = 0;
    u32 quantity = 0;
    xr_string error;
    bool ok() const { return error.empty(); }
};

// A type check, independent of the catalog's visibility and category overrides.
bool inventory_type(LPCSTR section);
SpawnTarget spawn_target(LPCSTR name);
SpawnResult spawn(const SpawnRequest& request);
void register_spawn_api(lua_State* L);
} // namespace infernis_spawner
