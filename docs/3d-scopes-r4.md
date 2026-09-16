# 3D scope prototype (R4 / DX11)

This prototype renders the world twice while an actor aims through an enabled optic. The
first pass uses the optic's FOV, skips the weapon HUD and UI, and copies the world image
into `$user$scope_lens`. The second pass draws the normal camera and HUD; the material
`models\lense_scope` samples that image on the physical lens mesh and overlays the
reticle from its base texture.

To try it with a weapon, set `[weapon_scopes] enable_3d_scopes = true` in
`gamedata/configs/infernis_engine/engine_external.ltx`. In the weapon section for a
permanent scope, or the `scope_name` optic section for an attachable scope, set
`scope_3d = true` and `scope_lens_fov = 20` (degrees, valid range 5–90). Put a lens mesh
in the first-person HUD model, assign `models\lense_scope`, and give its base texture
an alpha-channel reticle. A 2D optic works as before while the global switch is off;
the global switch defaults to off until a scope model is available for a game test.

For an attachable optic, its entry in the weapon's `scopes_sect` list can override
the weapon HUD aiming position and rotation. An inherited base section works too:

```ini
[scope_ar_3d]
aim_hud_offset_pos = 0.0, 0.0, 0.0
aim_hud_offset_rot = 0.0, 0.0, 0.0
aim_hud_offset_pos_16x9 = 0.0, 0.0, 0.0
aim_hud_offset_rot_16x9 = 0.0, 0.0, 0.0
scope_3d = true
scope_lens_fov = 20

[scope_susat_custom_ar]:scope_ar_3d
scope_name = wpn_addon_scope_susat_custom
scope_zoom_factor = 30
scope_dynamic_zoom = on
scope_nightvision = scope_contrast
scope_alive_detector = scope_detector
```

For each key, the selected `scopes_sect` entry takes priority over the inventory
item section named by `scope_name`, then the weapon section where applicable.
Each aim value is independent: if absent from both optic sections, the corresponding
HUD value is used.
On a widescreen display, an optic's `_16x9` value takes priority over its unsuffixed
value; if both are absent, the weapon HUD's `_16x9` value is used. Removing or
switching the optic restores the appropriate offsets automatically. Existing
`scope_zoom_factor` controls the weapon's zoom for that optic; `scope_lens_fov`
sets the lens FOV at its strongest dynamic zoom. With `scope_dynamic_zoom = on`,
changing zoom scales the lens FOV with the weapon's zoom factor. A permanently mounted scope keeps its
aiming offsets in the weapon HUD section and its zoom settings in the weapon section.

For each optic variant, override `scope_lens_fov` in its own `scopes_sect` entry
when a different fixed magnification is desired. `scope_nightvision` sections
whose `pp_eff_name` contains `contrast` or `nightvision` enable a lens-only
contrast or green night-vision approximation; the original `.ppe` is still used
for legacy 2D scopes. `scope_alive_detector` draws corner markers around up to
eight living targets currently visible to the actor, inside the lens image only.
Its existing `scope_detector` sound and target-update logic remains active.

If the 2D overlay still appears, look for `* ScopeLens:` in the game log after aiming.
The `R4`, `global`, and `scope_3d` values must all be `1`, `lens_fov` must be between
5 and 90, and `active` must be `1`. `optic` shows which section takes priority over
the weapon section for an attachable scope. The shader material loads independently
of these settings; the lens mesh position comes from the HUD model and its bones.

The prototype shares the full-screen render targets with the normal view and copies
one full-screen LDR texture each aimed frame. It supports R4 only. Test at runtime on
DX11 with and without MSAA, including local lights, sun shadows, zoom transitions,
underbarrel grenade mode, a renderer reset and a resolution change. A separate lower
resolution viewport, image inversion, lens distortion and night vision are follow-up
work after that game test.
