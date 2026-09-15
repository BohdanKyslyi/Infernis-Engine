# Addon packs

Place `addons` beside `gamedata` in the game directory. Each pack starts with the *contents* of gamedata, without another `gamedata` directory:

```text
game/
  gamedata/
    configs/system.ltx
    configs/weapons/weapons.ltx
    configs/misc/death_generic.ltx
  addons/
    my_weapon_pack/
      configs/
        mod_system_my_weapon_pack.ltx
        weapons/mod_weapons_my_weapon_pack.ltx
        misc/mod_death_generic_my_weapon_pack.ltx
      textures/ui/ui_icon_my_weapon_pack.dds
      particles/my_effect.pe
      meshes/
      scripts/
```

The engine indexes loose addon files under the corresponding gamedata paths at startup. The exact subdirectory names must match the installed game (for example, the directory behind `$game_config$`). New textures, meshes, scripts, sounds and particle files can live in separate packs. Resource lookup through the engine FS sees them as gamedata files. Remove or add packs while the game is closed, then restart.

Pack directories are loaded alphabetically (case-insensitive); when two packs provide the **same relative path**, the later pack wins. An addon file with the same path as a base gamedata file also takes precedence. These are *file* overrides, not merges. Use unique file paths where possible, especially for icons and `mod_*.ltx` files. Archived `.db/.xdb` files are not mounted from addons.

## Config additions

For any loaded read-only `name.ltx`, place `mod_name_<unique_pack_id>.ltx` in the same relative directory. The modular loader reads matching files in filename order and applies `![section]` updates to existing sections, or `[new_section]` for new ones. A matching base `name.ltx` must exist and be loaded for these additions to run. This also applies to files reached through `#include` (for example, `system.ltx` includes `infernis_engine/backpacks.ltx`, so `infernis_engine/mod_backpacks_<pack>.ltx` is picked up).

For example, a modular file next to `configs/system.ltx` may contain:

```ini
[wpn_my_weapon]
icons_texture = ui\ui_icon_my_weapon_pack
inv_grid_x = 0
inv_grid_y = 0
inv_grid_width = 5
inv_grid_height = 2
```

For trader supplies, put `mod_<trade_filename>_<pack>.ltx` next to the trader's loaded trade LTX and extend its actual section with `![supplies_section]`. For death items, `configs/misc/mod_death_generic_<pack>.ltx` extends sections from the loaded `death_generic.ltx` (including its ordinary includes). Give each pack's modular LTX a distinct filename: two files with the same virtual path cannot both load.

This is a startup-only loose-file overlay, not an addon UI or dependency manager. Existing mods that patch the same `[section]` keys still need compatible values or a chosen load order.
