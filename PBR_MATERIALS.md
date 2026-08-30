# Infernis Engine PBR materials

Infernis Engine provides a metallic/roughness PBR path for the R4/DX11
renderer while keeping legacy X-Ray materials unchanged.

## Enabling PBR

Open the base texture's `.thm` metadata and select the material type
`Infernis Engine -> PBR`. The texture must use a bump map. The renderer then
compiles a dedicated `USE_PBR` shader variant and marks the material in the
G-buffer so sun, local lights, environment lighting, and the combine pass all
use the PBR path.

After changing shader source or a calibration mode, delete `shaders_cache`
before launching the game.

## Texture set

For a base texture named `material.dds`, the PBR set is:

| Texture | Channel | Data |
| --- | --- | --- |
| `material.dds` | RGB | Albedo/base color |
| `material_bump.dds` | R | Height |
| `material_bump.dds` | G | Legacy normal payload; not sampled by the PBR reconstruction |
| `material_bump.dds` | A | Normal X |
| `material_bump.dds` | B | Normal Y |
| `material_bump#.dds` | R | Metallic |
| `material_bump#.dds` | G | Roughness |
| `material_bump#.dds` | B | Reserved for future use |
| `material_bump#.dds` | A | Ambient occlusion |

Normal Z is reconstructed in the shader. Albedo is color data; normal,
height, metallic, roughness, and AO are data textures and must not receive an
additional artistic gamma correction during packing.

## Value conventions

- Metallic: `0` for dielectrics and `1` for conductors. Avoid arbitrary gray
  values unless the surface intentionally blends two material types.
- Roughness: `0` is mirror-smooth and `1` is fully rough. The renderer applies
  a small minimum and geometric specular anti-aliasing for stability.
- AO: `1` is unoccluded and `0` is fully occluded. AO affects indirect
  lighting; it is not a replacement for direct-light shadows.
- Metal base color represents colored normal-incidence reflectance (F0).
  Very dark metallic Albedo produces a physically dark metal because metals
  have no diffuse fallback.

## Production lighting path

- GGX/Trowbridge-Reitz distribution with Smith visibility and Schlick Fresnel.
- Separate calibrated sun, local-light, and ambient energy scales.
- Dedicated `env_s0`/`env_s1` environment cubemaps for specular IBL.
- Roughness-selected cubemap mips across the verified LOD range.
- Multiple-scattering energy compensation for rough GGX surfaces.
- Legacy materials continue through the original X-Ray lighting path.

## Material calibration

`apply_pbr_stage2_30_material_override.py` can temporarily override the
material response without changing the G-buffer diagnostics:

```text
py apply_pbr_stage2_30_material_override.py dielectric_rough
py apply_pbr_stage2_30_material_override.py dielectric_smooth
py apply_pbr_stage2_30_material_override.py metal_rough
py apply_pbr_stage2_30_material_override.py metal_smooth
py apply_pbr_stage2_30_material_override.py off
```

The two metal modes use a neutral calibration F0 of `0.22`; authored materials
are never forced to that value. Always restore `off` for normal gameplay.

## Local-metal audit

`apply_pbr_stage2_36_local_metal_audit.py` diagnoses a metallic material that
looks correct in environment lighting but responds weakly to a local light such
as the actor's headlamp. It coordinates the existing G-buffer and local-light
diagnostic selectors so stale modes cannot overlap accidentally:

```text
py apply_pbr_stage2_36_local_metal_audit.py orm
py apply_pbr_stage2_36_local_metal_audit.py albedo
py apply_pbr_stage2_36_local_metal_audit.py normal
py apply_pbr_stage2_36_local_metal_audit.py local_cosine
py apply_pbr_stage2_36_local_metal_audit.py local_specular
py apply_pbr_stage2_36_local_metal_audit.py production
```

The `orm` output is `R=metallic`, `G=roughness`, and `B=hemi*AO`. The Albedo
and normal modes display the values that survived the deferred G-buffer. The
local cosine probe checks position, light direction, and `NdotL` without GGX;
the specular mode keeps only the local-light GGX/Fresnel lobe. Always restore
`production` and delete `shaders_cache` after completing the audit.

## Local-metal response calibration

Stage 2.37 separates the two mechanisms that can make a conductor respond too
weakly to X-Ray's mathematical point lights. The multi-scatter mode restores a
conservative broad, F0-tinted conductor lobe. The source-radius mode combines
the authored GGX variance with a finite-source variance so a local light does
not behave like an infinitely small delta emitter:

```text
py apply_pbr_stage2_37_local_metal_response.py baseline
py apply_pbr_stage2_37_local_metal_response.py multiscatter
py apply_pbr_stage2_37_local_metal_response.py source_radius
py apply_pbr_stage2_37_local_metal_response.py combined
```

The corrections affect only PBR metallic response from local lights. Sun, IBL,
dielectric diffuse, and legacy materials retain their production paths. Keep
Stage 2.30 authored and Stage 2.36 production during this test. Restore
`baseline` and delete `shaders_cache` after calibration.

## Conductor transport calibration

Stage 2.38 follows the Stage 2.37 can-lid test: finite-source broadening made
edge highlights wider, but neither it nor conservative direct multi-scattering
restored the broad body response. Inverting Metallic only appeared brighter
because it re-enabled X-Ray's bridged dielectric diffuse lobe.

The next A/B test separates the remaining transport mismatch. `f0_bridge`
partly bridges metallic F0 toward X-Ray's presentation domain. The bounded
`irradiance_fill` reuses local and sky irradiance as an F0-tinted,
roughness-weighted conductor reflection; it does not add Lambert diffuse:

```text
py apply_pbr_stage2_38_conductor_transport.py baseline
py apply_pbr_stage2_38_conductor_transport.py f0_bridge
py apply_pbr_stage2_38_conductor_transport.py irradiance_fill
py apply_pbr_stage2_38_conductor_transport.py combined
```

Use the original, non-inverted Metallic map. Keep Stage 2.30 authored,
Stage 2.36 production, and Stage 2.37 baseline during the test. Sun geometry,
dielectric PBR materials, and legacy materials remain on their established
paths. Restore `baseline` and delete `shaders_cache` after calibration.

## Conductor energy-route reference

Stage 2.39 is an intentionally strong upper-bound audit. Stage 2.38 showed that
partially bridging F0 and adding a conservative irradiance fill changed the can
lid only slightly. This test therefore separates the two sources of incident
specular energy instead of applying another small global multiplier:

```text
py apply_pbr_stage2_39_conductor_reference.py baseline
py apply_pbr_stage2_39_conductor_reference.py local_reference
py apply_pbr_stage2_39_conductor_reference.py ibl_reference
py apply_pbr_stage2_39_conductor_reference.py combined_reference
```

`local_reference` adds a broad, roughness-weighted, F0-tinted upper bound only
to PBR conductors under local lights. `ibl_reference` uses the validated diffuse
sky irradiance as an upper bound for energy missing from the ordinary X-Ray
environment cubemap mip. `combined_reference` enables both probes. None of the
modes re-enables metallic Lambert diffuse or affects dielectric/legacy
materials.

Keep Stage 2.30 authored, Stage 2.36 production, Stage 2.37 baseline, and Stage
2.38 baseline. Capture the same open/closed can poses with the flashlight on;
also capture `ibl_reference` with it off. Restore `baseline` and delete
`shaders_cache` after the audit.

## Combined conductor gain calibration

Stage 2.40 keeps the Stage 2.39 `combined_reference` transport selected and
calibrates only its local-light and IBL reference strengths. The two strengths
move together so the conductor keeps the balance observed in the reference
test, while dielectric PBR and legacy materials remain unchanged:

```text
py apply_pbr_stage2_40_conductor_gain.py baseline
py apply_pbr_stage2_40_conductor_gain.py combined_100
py apply_pbr_stage2_40_conductor_gain.py combined_110
py apply_pbr_stage2_40_conductor_gain.py combined_115
py apply_pbr_stage2_40_conductor_gain.py combined_120
```

`combined_100` reproduces Stage 2.39 exactly. The remaining modes add 10, 15,
or 20 percent to both F0-tinted conductor reference terms; they do not add
Lambert diffuse. Compare the same open and closed can poses with the flashlight
on. `combined_115` is the preferred production candidate. Restore `baseline`
and delete `shaders_cache` after calibration.

## Specular sky source audit

Stage 2.41 replaces the Stage 2.39/2.40 irradiance-fill experiment with a real
reflection-source test. X-Ray binds the small `sky_texture_env` cubemaps as
`env_s0/env_s1`, while the full visible sky cubemaps are available as
`sky_s0/sky_s1`. Diffuse irradiance stays on `env_s*`; only specular IBL is
switched during this audit:

```text
py apply_pbr_stage2_41_specular_sky.py baseline
py apply_pbr_stage2_41_specular_sky.py env_auto_lod
py apply_pbr_stage2_41_specular_sky.py sky_linear
py apply_pbr_stage2_41_specular_sky.py sky_native
```

`env_auto_lod` isolates the old fixed-LOD assumption. `sky_linear` follows the
IX-Ray resource split and keeps the private PBR gamma-to-linear transport.
`sky_native` uses the same reflection vector, full sky cubemap, and
roughness-selected mip but bypasses the manual specular gamma decode to expose
a possible double conversion. None of these modes adds diffuse light to a
metal or reuses diffuse irradiance as a conductor fill.

The switcher automatically restores the Stage 2.30 material override and
Stages 2.37-2.40 to baseline so an older diagnostic cannot contaminate this
test. Capture the same open and closed can poses outdoors with the flashlight
on, plus one dark-room frame for `sky_linear` and `sky_native`. Restore
`baseline` and delete `shaders_cache` after the audit.

## Stage 2.42 - shadow-aware HUD conductor transport

Stage 2.41 proved that the full sky cubemap provides useful directional metal
reflections, but it also exposed two independent limitations of the classic
renderer: the weather cubemap has no indoor visibility, and a delta local light
can miss the visible lobe of a rough conductor completely.

`apply_pbr_stage2_42_hud_conductor.py` separates those routes:

- `baseline` restores the Stage 2.41 baseline;
- `local_shadowed` adds an F0-coloured integrated conductor response inside the
  ordinary local-light pass, so spotlight attenuation and shadows still apply;
- `hud_occlusion` keeps `sky_linear` for the world but suppresses the unoccluded
  weather cubemap on near-camera metallic surfaces;
- `combined` enables both corrections and is the Stage 2.42 candidate.

This is deliberately an audit bridge, not a replacement for HUD SSLR.  The
occlusion mode removes invalid through-wall sky energy; a later screen-space
reflection pass can replace it with scene-visible indoor reflections.

## Stage 2.43 - inline HUD SSLR

Stage 2.42 proved that suppressing the weather cubemap without replacing its
energy only turns conductors black. Stage 2.43 therefore leaves the cubemap as
a fallback and ray-marches the current G-buffer for close metallic surfaces.
A valid screen-space hit reflects the hit surface's accumulated direct light;
inside a room this lets the lid reflect a flashlight-lit wall rather than the
outdoor weather. Rays that leave the screen or miss geometry retain the
`sky_linear` cubemap.

```text
py apply_pbr_stage2_43_hud_sslr.py hit_mask
py apply_pbr_stage2_43_hud_sslr.py sslr
py apply_pbr_stage2_43_hud_sslr.py baseline
```

The hit mask is green where the ray found visible scene geometry and red where
the cubemap fallback remains. This first implementation is an inline,
single-frame bridge; a dedicated reflection render target and temporal filter
can follow after the ray/depth convention is validated in game.
