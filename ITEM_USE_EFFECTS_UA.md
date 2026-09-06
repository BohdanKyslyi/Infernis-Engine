# Ефекти анімацій використання предметів

`ItemUseController` підтримує camera effector та HUD-партикл, синхронізовані з
анімацією використання consumable-предмета. Старі предмети без нових параметрів
працюють без змін.

Параметри можна вказувати у фізичній секції предмета, його use-секції,
`portion_state` або HUD-секції. Пріоритет має найконкретніша секція:

1. HUD-секція;
2. поточний `portion_state`;
3. use-секція;
4. секція предмета.

## Camera effector

Якщо окремий параметр не задано, рушій автоматично шукає файл за назвою реально
обраної HUD-анімації:

```text
gamedata\anims\camera_effects\<motion_name>.anm
```

Наприклад, для `anm_show = conserva_use` автоматичний шлях буде
`gamedata\anims\camera_effects\conserva_use.anm`.

Файл з іншою назвою можна підключити явно:

```ini
[conserva_hud_model]
cam_eff_name       = camera_effects\conserva.anm
cam_eff_cyclic     = false
cam_eff_hud_affect = false
```

- `cam_eff_name` — шлях відносно `gamedata\anims`; розширення `.anm` можна не
  вказувати.
- `cam_eff_cyclic` — циклічне відтворення; типово `false`.
- `cam_eff_hud_affect` — чи впливає effector також на HUD-камеру; типово
  `false`, як у штатних weapon action effectors.
- `cam_eff_name = none` вимикає також автоматичний пошук за назвою motion.

Camera effector починається одночасно з HUD-анімацією та гарантовано
прибирається під час її завершення або скасування.

## HUD-партикл

```ini
[anm_cigarette_hud]
use_particles             = effects\cigarette_smoke
use_particles_bone        = smoke_point
use_particles_offset      = 0, 0, 0
use_particles_orientation = 0, 0, 0
use_particles_start_time  = 1200
use_particles_stop_time   = 4300
```

- `use_particles` — ім'я ефекту або групи з Particle Editor.
- `use_particles_bone` — необов'язкова кістка HUD-моделі. Без параметра ефект
  кріпиться до кореня предмета.
- `use_particles_offset` — локальне зміщення від кістки.
- `use_particles_orientation` — локальна орієнтація у градусах.
- `use_particles_start_time` — момент запуску від початку анімації у
  мілісекундах; типово `0`.
- `use_particles_stop_time` — момент зупинки у мілісекундах; без параметра
  використовується кінець анімації.

Матриця партикла оновлюється щокадрово з item-анімації. І циклічний, і звичайний
ефект належать контролеру та видаляються разом із тимчасовим HUD-предметом.
