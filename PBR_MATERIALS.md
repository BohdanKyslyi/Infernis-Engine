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
