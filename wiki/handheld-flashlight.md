# Ручний HUD-ліхтар

Ручний ліхтар використовує слот детектора та стандартну клавішу показу детектора. Для
предмета треба вказати `class = D_FLALIT`, `slot = 9` і HUD-секцію з
`attach_place_idx = 1`.

## Приклад секції предмета

```ini
[device_hand_flashlight]:identity_immunities
$spawn                         = "devices\hand flashlight"
class                          = D_FLALIT
cform                          = skeleton
visual                         = dynamics\devices\hand_flashlight\hand_flashlight.ogf
hud                            = device_hand_flashlight_hud
slot                           = 9
animation_slot                 = 7
default_to_ruck                = true
sprint_allowed                 = true
control_inertion_factor        = 1.0

inv_name                       = st_device_hand_flashlight_name
inv_name_short                 = st_device_hand_flashlight_name
description                    = st_device_hand_flashlight_descr
inv_weight                     = 0.35
cost                           = 1200
inv_grid_width                 = 1
inv_grid_height                = 1
inv_grid_x                     = 0
inv_grid_y                     = 0

snd_draw                       = interface\item_usage\flashlight_draw
snd_holster                    = interface\item_usage\flashlight_holster

; Затримка ввімкнення після початку anm_show, у мілісекундах.
light_start_time               = 650

; spot — спрямований ліхтар, point — всебічне світло для свічки/запальнички.
light_type                     = spot
light_color                    = 1.0, 0.92, 0.78, 1.0
light_range                    = 18.0
light_cone_angle               = 55.0
light_shadow                   = true
light_hud_mode                 = true
light_texture                  = internal\internal_light_torch_r2

; Точка світла відносно кістки HUD-моделі.
light_bone                     = light_bone
light_offset                   = 0.0, 0.0, 0.0
light_orientation              = 0.0, 0.0, 0.0

; Необов'язковий looped particle, наприклад полум'я запальнички.
light_particles                = none
light_particles_bone           = light_bone
light_particles_offset         = 0.0, 0.0, 0.0
light_particles_orientation    = 0.0, 0.0, 0.0

[device_hand_flashlight_hud]:hud_base
item_visual                    = dynamics\devices\hand_flashlight\hand_flashlight_hud.ogf
attach_place_idx               = 1

hands_position                 = 0.0, 0.0, 0.0
hands_orientation              = 0.0, 0.0, 0.0
hands_position_16x9            = 0.0, 0.0, 0.0
hands_orientation_16x9         = 0.0, 0.0, 0.0

anm_show                       = hand_flashlight_draw
anm_show_fast                  = hand_flashlight_draw_fast
anm_hide                       = hand_flashlight_holster
anm_hide_fast                  = hand_flashlight_holster_fast
anm_idle                       = hand_flashlight_idle
```

## Налаштування позиції

`light_offset` та `light_particles_offset` задаються в метрах у локальних координатах
вибраної кістки. Наприклад, зміщення на 2 см вліво, 5 см ближче та 10 см нижче
задається як вектор приблизно `-0.02, -0.10, -0.05`; конкретні знаки осей залежать
від орієнтації кістки в HUD-моделі.

Радіус світла задається стандартним ключем `light_range`; як синонім також
підтримується `light_radius`. Обидва значення вимірюються в метрах, а
`light_orientation` і `light_particles_orientation` — у градусах.

Для запальнички достатньо змінити `light_type` на `point`, поставити теплий
`light_color`, зменшити `light_range` і вказати looped particle полум'я. Якщо
партикл не потрібен, треба залишити `light_particles = none` або прибрати ключ.
