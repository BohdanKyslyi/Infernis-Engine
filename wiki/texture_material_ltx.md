# Texture material sidecar LTX

Infernis Engine can load runtime texture-material properties from a plain-text
LTX file placed beside the texture. The sidecar uses the same relative path and
base name as the texture:

```text
gamedata/textures/act/brick.dds
gamedata/textures/act/brick.thm
gamedata/textures/act/brick.ltx
```

The engine loads THM data first and then applies LTX values. Consequently, an
LTX key overrides the matching THM property while omitted keys keep their THM
values. A complete LTX can also be used without a THM.

```ini
[texture]
type = image

; oren_nayar_blin / blin_phong / phong_metal / metal_oren_nayar / pbr
material = pbr
material_weight = 0.0

detail_texture = detail\detail_grnd_grass
detail_scale = 1.0
detail_diffuse = true
detail_bump = false

; none / use / parallax
bump_mode = parallax
bump_texture = act\brick_bump
```

All keys are optional. The supported `type` values are `image`, `cube_map`,
`bump_map`, `normal_map`, and `terrain`. Runtime rendering descriptors are
created for image, terrain, and normal-map entries, matching existing THM
behaviour.

An empty or unrelated LTX file is ignored unless it contains a `[texture]`
section. `[texture_material]` is accepted as a compatibility alias.
