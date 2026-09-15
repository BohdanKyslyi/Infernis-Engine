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
