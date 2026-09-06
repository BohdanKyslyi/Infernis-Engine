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

## Persistent HUD lifecycle

Окрім одноразового consumable-режиму, контролер має окремий persistent lifecycle:

```text
anm_show -> anm_idle -> anm_hide
```

HUD-секція задає motion aliases звичайним способом:

```ini
[anm_pda_hud]:base_consumable_hud
item_visual      = dynamics\devices\dev_pda\dev_pda_hud.ogf
attach_place_idx = 0

anm_show = pda_draw
anm_idle = pda_idle
anm_hide = pda_holster

snd_show = interface\pda_draw
snd_hide = interface\pda_holster
```

- `anm_show` запускається один раз після штатного ховання зброї;
- після завершення `anm_show` контролер переходить в `anm_idle`;
- `anm_idle` є необов'язковим: без нього HUD лишається у логічному idle-стані
  на поточному циклі;
- `anm_hide` також необов'язковий: без нього HUD від'єднується одразу після
  запиту на закриття;
- запит на закриття під час `anm_show` запам'ятовується, а `anm_hide`
  запускається після повного завершення show-анімації.

Звуки persistent lifecycle також необов'язкові:

- `snd_show` запускається синхронно з `anm_show`;
- `snd_hide` запускається синхронно з `anm_hide`;
- якщо відповідного motion alias немає і фаза пропускається, її звук також не
  відтворюється;
- обидва звуки є HUD-звуками: 2D, нециклічними та автоматично зупиняються під
  час завершення або скасування lifecycle;
- підтримуються стандартні варіанти `snd_show1`, `snd_show2`, `snd_hide1`,
  `snd_hide2` тощо, а також параметри гучності й затримки після імені звуку.

Наприклад:

```ini
snd_show  = interface\pda_draw,    0.8, 0.0
snd_show1 = interface\pda_draw_2,  0.8, 0.0
snd_hide  = interface\pda_holster, 0.8, 0.0
```

Внутрішній C++ API:

```cpp
controller->StartHudAnimation(hud_section);
controller->StartHudAnimation(hud_section, true); // allow inventory item use in idle
controller->IsHudAnimationActive();
controller->IsHudAnimationIdle();
controller->RequestHudAnimationHide();
```

Consumable-шлях `Start(CInventoryItem*)` не переходить у persistent lifecycle:
для старої їжі `anm_show` і надалі є повною одноразовою анімацією використання,
`snd_using_anim` лишається її окремим звуком, а відсутність `anm_idle`,
`anm_hide`, `snd_show` та `snd_hide` нічого не змінює.

## Підключення PDA

Імерсивна анімація PDA вмикається через HUD-секцію в `engine_external.ltx`:

```ini
[items_animations]
enable_consumables_animations = true
enable_pda_animations         = true
pda_hud                       = anm_pda_hud
```

- `enable_pda_animations` необов'язковий і типово вважається `true`;
- `pda_hud` — секція persistent HUD із `item_visual`, `anm_show` та
  необов'язковими `anm_idle`, `anm_hide`, `snd_show`, `snd_hide`;
- якщо `pda_hud` не задано, дорівнює `none`, його секції не існує або анімацію
  неможливо запустити, рушій безпечно відкриває PDA старим миттєвим способом.

Послідовність відкриття:

```text
PDA hotkey -> weapon/detector hide -> anm_show + snd_show
           -> logical idle -> original 2D PDA interface
```

Послідовність закриття:

```text
PDA hotkey / close button / script -> close 2D interface
                                   -> anm_hide + snd_hide
                                   -> detach HUD -> restore weapon
```

Контролер відстежує фактичний стан `CUIPdaWnd`, тому hide-послідовність працює
не лише для стандартного хоткея, але й для кнопки закриття, скриптового
`HidePdaMenu()` та загального закриття діалогів. У разі смерті актора, зміни
рівня або скасування контролера pending-відкриття очищається, а 2D PDA не
залишається завислим на екрані.

## Підключення рюкзаків та інвентарю

Анімація інвентарю є опційною властивістю екіпірованого рюкзака. Фізична
секція рюкзака вказує свою HUD-секцію ключем `hud`:

```ini
[backpack_stalker]:backpack
hud = anm_backpack_stalker_hud

[backpack_scientific]:backpack
hud = anm_backpack_scientific_hud
```

Завдяки цьому різні рюкзаки можуть використовувати цілком незалежні моделі,
motions та звуки:

```ini
[anm_backpack_stalker_hud]:base_consumable_hud
item_visual      = dynamics\backpacks\backpack_stalker_hud.ogf
attach_place_idx = 0

anm_show = backpack_stalker_draw
anm_idle = backpack_stalker_idle
anm_hide = backpack_stalker_holster

snd_show = interface\backpack_stalker_draw
snd_hide = interface\backpack_stalker_holster

[anm_backpack_scientific_hud]:base_consumable_hud
item_visual      = dynamics\backpacks\backpack_scientific_hud.ogf
attach_place_idx = 0

anm_show = backpack_scientific_draw
anm_idle = backpack_scientific_idle
anm_hide = backpack_scientific_holster
```

`anm_idle`, `anm_hide`, `snd_show` та `snd_hide` лишаються необов'язковими за
тими самими правилами persistent lifecycle. `anm_show` потрібен лише для
секцій, які справді вмикають анімоване відкриття через `hud`.

### Глобальний рюкзак без окремого слота

`engine_external.ltx` може задавати одну базову HUD-секцію для всіх гравців,
незалежно від того, чи ввімкнена система `BACKPACK_SLOT`:

```ini
[items_animations]
enable_backpack_animations = true
backpack_hud                = anm_backpack_default_hud
```

- `enable_backpack_animations` вмикає або вимикає і глобальну модель, і
  персональні HUD-секції предметів; без параметра зберігається значення `true`;
- `backpack_hud` — необов'язковий глобальний fallback; штатно в конфігу стоїть
  `none`, доки для нього не додано готову модель та motions;
- HUD екіпірованого рюкзака має пріоритет над `backpack_hud`;
- якщо в екіпірованого рюкзака явно задано `hud = none`, анімація вимикається
  саме для нього без переходу на глобальний fallback.

Отже, для спрощеного варіанта достатньо не вмикати слот рюкзаків, створити одну
HUD-секцію на зразок `anm_backpack_default_hud` і вказати її в `backpack_hud`.
Без персональної або глобальної HUD-секції, з вимкненим прапорцем чи з
помилковою секцією інвентар відкривається старим миттєвим способом.

Послідовність відкриття:

```text
Inventory hotkey -> equipped BACKPACK_SLOT item -> its hud section
                 -> otherwise global backpack_hud
                 -> weapon/detector hide -> anm_show + snd_show
                 -> logical idle -> original 2D inventory interface
```

Послідовність закриття:

```text
Inventory hotkey / close button / script -> close 2D interface
                                         -> anm_hide + snd_hide
                                         -> detach HUD -> restore weapon
```

Рушій запам'ятовує HUD-секцію рюкзака на старті послідовності. Тому навіть якщо
гравець переставить або зніме рюкзак уже у відкритому інвентарі, закриття
коректно завершить lifecycle тієї моделі, яку було показано. Фактичний стан
`CUIActorMenu` відстежується щокадрово, тож hide-анімація охоплює стандартний
хоткей, кнопку закриття, скриптовий `HideActorMenu()` і загальне закриття
діалогів.

### Використання їжі у відкритому інвентарі

У backpack idle контролер продовжує приховувати зброю та блокувати сходи, але
повертає інвентарю можливість надсилати item-use запити:

- предмет без валідної consumable-анімації застосовується одразу, а інвентар і
  рюкзак лишаються відкритими;
- анімований предмет ставиться в чергу, інвентар закривається, повністю
  програється `anm_hide` рюкзака, після чого автоматично стартує звичайна
  consumable-послідовність цього предмета;
- повторні запити під час show/hide або вже запущеної consumable-анімації
  блокуються, як і раніше.
