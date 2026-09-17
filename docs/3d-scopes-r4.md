# 3D scope prototype (R4 / DX11)

This prototype renders the world twice while an enabled optic is active according to
`[weapon_scopes] scope_render_mode`. The
first pass uses the optic's FOV, skips the weapon HUD and UI, and copies the world image
into `$user$scope_lens`. The second pass draws the normal camera and HUD; the material
`models\lense_scope` samples that image on the physical lens mesh and overlays the
reticle from its base texture.
By default, the lens pass starts as soon as aiming begins; the lens mesh follows the weapon's
normal aim animation. The lens is drawn after final screen compositing so HUD depth
and normal postprocessing cannot restore the optic housing in the aperture.
While active, the captured world image covers HUD weapon
parts behind the optical aperture. The shader tests HUD depth when aiming is
inactive, then brings only the active aperture forward; the scope housing and
the area outside the aperture remain part of the HUD model. A white-key optic
needs centered UVs on the lens mesh for the circular aperture to fit its frame.
Reticle textures with a transparent background use their original alpha for
the reticle. When the texture has opaque black corners instead, the shader
uses its colored pixels together with alpha as the reticle mask. This keeps
colored markings visible and prevents the black backdrop covering the view.
For a dark reticle, author the texture with transparent background and opaque
markings: black markings on an opaque black background cannot be separated.
For a reticle with white glass, black graduations, and colored illumination,
set `scope_lens_white_key = true` in that optic's section. This opt-in mode
uses the white area as clear glass, retains black and colored markings, and
fades out the black canvas beyond a centered circular UV aperture (radius
0.855–0.865 in UV space). The world image remains opaque within this aperture,
so the HUD scope body cannot bleed into it through a wide alpha transition.
Only near-black strokes and distinctly colored marks from the reticle texture
are overlaid as ink. Mid-gray scratches and glass wear gently darken the captured
world while leaving the lens opaque over the weapon HUD.
Keep the lens art centered in the texture. It does not
require a transparent alpha channel; `scope_lens_shadow_strength = 0` disables
the separate eye-box shading if a completely clear view is desired.

To try it with a weapon, set `[weapon_scopes] enable_3d_scopes = true` in
`gamedata/configs/infernis_engine/engine_external.ltx`. In the weapon section for a
permanent scope, or the `scope_name` optic section for an attachable scope, set
`scope_3d = true` and `scope_lens_fov = 20` (degrees, valid range 5–90). Put a lens mesh
in the first-person HUD model, assign `models\lense_scope`, and give its base texture
an alpha-channel reticle. A 2D optic works as before while the global switch is off;
the global switch defaults to off until a scope model is available for a game test.

Choose when the second world projection runs in `[weapon_scopes]`:

| `scope_render_mode` | Activation |
| --- | --- |
| `performance` | Once the aiming transition finishes. |
| `balanced` (default) | From the start of the aiming transition. |
| `quality` | Whenever the equipped 3D scoped weapon is active, including hip fire. |

All three modes still require `enable_3d_scopes = true`, an attached or permanent
scope with `scope_3d = true`, and a valid `scope_lens_fov`. Only `quality` runs the
second world pass while the player is not aiming. Detector overlays still require
aiming. The setting is read when frames render, so it does not require rebuilding.

`[weapon_scopes] scope_lens_chromatic_aberration = 0.8` shifts the captured
world's red and blue samples outward and inward by up to 0.8 screen pixels near
the edge of the lens. The center and reticle remain sharp. Values are clamped
to 0–3 pixels; use `0` to disable the effect without changing the shader. This
adds two scene texture samples per normal lens pixel while enabled.

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
; Optional R4 glass settings, per optic. Values below are the defaults.
scope_lens_eye_relief = 0.12
scope_lens_shadow_strength = 0.40
scope_lens_reflection_strength = 0.025
; Use only with a centered white-background reticle texture:
; scope_lens_white_key = true

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

In normal 3D mode, the lens uses its skinned HUD position and tangent frame to
shift a soft pupil shadow as the eye moves off axis. `scope_lens_eye_relief`
controls that shift (0–0.5), `scope_lens_shadow_strength` controls edge darkness
(0–0.8), and `scope_lens_reflection_strength` controls a subtle moving highlight
(0–0.15). Put overrides on the optic wrapper or its `scope_name` section; omitted
keys use the defaults above. Night and contrast modes keep their own appearance.
The highlight is procedural and does not require the 3DSS reflection texture.
The pupil shadow starts at the outer edge of the lens, leaving its viewing area
clear when the eye is centered. Set `scope_lens_shadow_strength = 0` on an optic
to disable the shadow completely when checking its reticle texture.

If the 3D mode needs an explicit override, set `scope_lens_effect = contrast` or
`scope_lens_effect = nightvision` in the appropriate `scopes_sect` entry. By
default, the engine derives the mode from `scope_nightvision` and that section's
`pp_eff_name`. The `* ScopeLensEffect:` log line reports the chosen config
sections and numeric mode (0=normal, 1=contrast, 2=night vision). The
`* ScopeLensShader:` line confirms which mode actually reached the R4 lens
material; the new shader file must also be installed for the effect to appear.

If the 2D overlay still appears, look for `* ScopeLens:` in the game log after aiming.
The `R4`, `global`, and `scope_3d` values must all be `1`, `lens_fov` must be between
5 and 90, and `active` must be `1`. `optic` shows which section takes priority over
the weapon section for an attachable scope. The shader material loads independently
of these settings; the lens mesh position comes from the HUD model and its bones.

The prototype shares the full-screen render targets with the normal view and copies
one full-screen LDR texture each aimed frame. It supports R4 only. Test at runtime on
DX11 with and without MSAA, including local lights, sun shadows, zoom transitions,
underbarrel grenade mode, a renderer reset and a resolution change. A separate lower
resolution viewport and lens distortion are follow-up work after that game test.
