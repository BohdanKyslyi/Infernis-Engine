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

## Приховування UI та PPE

Будь-яка controller HUD-секція може тимчасово прибрати ігрові індикатори й
приціл та запустити одноразовий PPE із заданою затримкою:

```ini
[anm_helmet_dressing_hud]:animated_item_hud
item_visual     = dynamics\equipments\helmet_dressing_hud.ogf
anm_show        = helmet_dressing

disable_ui      = true
ppe_effect      = effects\fade_in.ppe
ppe_effect_timer = 1000
```

- `disable_ui` необов'язковий і типово дорівнює `false`. Зі значенням `true`
  контролер ховає HUD-індикатори та приціл одразу після закриття меню й повертає
  саме той стан видимості, який був до початку lifecycle;
- `ppe_effect` — шлях відносно `gamedata\anims`; розширення `.ppe` можна не
  вказувати. Значення `none`, відсутній або невалідний файл безпечно вимикають
  PPE та залишають пояснення в логові для помилкового шляху;
- `ppe_effect_timer` — затримка запуску в мілісекундах від початку першого
  HUD-motion; типово `0`. Таймер не скидається під час переходів
  `anm_show -> anm_idle -> anm_hide`;
- PPE не обрізається штатним завершенням Controller: одноразовий файл дограє
  власну тривалість, що дозволяє виконати `function_on_stop` і відкрити PDA вже
  під час переходу. Аварійний `Cancel()` прибирає ще активний ефект.

Обидва параметри використовують ту саму систему пріоритетів секцій, що й
camera effector. Старі HUD-секції без них працюють без змін.

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
controller->StartHudAnimationOnce(hud_section);   // anm_show + snd_show only
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

## Анімації вдягання екіпірування

Одноразові анімації вдягання броні, шолома й рюкзака вмикаються глобально та
мають окремі HUD-посилання для кожного типу слота:

```ini
[items_animations]
enable_dressing_animations = true
outfit_dressing_hud        = anm_outfit_dressing_hud
helmet_dressing_hud        = anm_helmet_dressing_hud
backpack_dressing_hud      = anm_backpack_dressing_hud
```

Типова HUD-секція використовує лише `anm_show` та необов'язковий `snd_show`:

```ini
[anm_outfit_dressing_hud]:base_consumable_hud
item_visual      = dynamics\equipments\outfit_dressing_hud.ogf
attach_place_idx = 0
anm_show         = outfit_dressing
snd_show         = interface\outfit_dressing
block_movement   = true
```

`anm_idle`, `anm_hide` і `snd_hide` для dressing lifecycle не читаються: після
закінчення `anm_show` HUD від'єднується, а зброя повертається. Кожна фізична
секція екіпірування може замінити глобальне посилання або вимкнути анімацію
лише для себе:

```ini
[scientific_outfit]:outfit_base
dressing_hud = anm_scientific_outfit_dressing_hud

[light_helmet]:helmet_base
dressing_hud = none
```

Анімація запускається лише після ручного перенесення предмета у відповідний
слот у відкритому інвентарі актора. Відновлення слотів із сейва та скриптові
операції не запускають dressing lifecycle. Старт відкладається до наступного
кадру, щоб drag-and-drop UI встиг завершити перенесення `CUICellItem` між
контейнерами до закриття інвентарю.

Послідовність для звичайного інвентарю:

```text
Move to slot -> item is equipped -> inventory closes
             -> weapon/detector hide -> anm_show + snd_show
             -> detach HUD -> restore weapon
```

Якщо інвентар уже відкритий з backpack HUD у фазі `idle`, нова анімація
автоматично стає в чергу: спочатку програється `anm_hide` рюкзака, а потім
dressing `anm_show`. Якщо прапорець вимкнений, HUD не заданий, його секції або
`anm_show` не існує чи motion не завантажився з OMF, предмет однаково лишається
в екіпірованому слоті, а причина fallback записується в лог.

### Блокування руху

Необов'язковий параметр `block_movement` читається з будь-якої controller HUD-
секції та типово дорівнює `false`. Зі значенням `true` на весь lifecycle
блокуються ходьба, біг, стрибок, присідання, нахили та поворот камери мишею або
клавішами. Псевдонім `block_move` підтримується для сумісності, але якщо задано
обидва параметри, пріоритет має `block_movement`. Наприклад, для перевдягання
броні його можна ввімкнути, а для шолома не вказувати або явно задати
`block_movement = false`.

Під час заміни броні її gameplay-властивості та third-person модель
застосовуються одразу, але HUD рук попередньої броні зберігається до завершення
`anm_hide` рюкзака. Перед самим dressing `anm_show` контролер синхронізує руки
з уже екіпірованою новою бронею.

## Сюжетні HUD-анімації з логіки

Одноразовий Controller можна запускати як ефект `xr_effects` без предмета в
інвентарі. Це дає сюжетним сценам той самий lifecycle, що й анімованому
використанню предметів: штатне ховання зброї та детектора, тимчасові руки й
`item_visual`, `anm_show`, `snd_show`, camera effector, локальний HUD FOV і
необов'язкове блокування руху.

```ini
[sr_idle@0]
on_info = {+zat_need_hud !is_hud_controller_active} %=activate_hud_controller(zat_wpn_ak74_cutscene_1)% sr_idle@1

[sr_idle@1]
```

`is_hud_controller_active` повертає `true` для будь-якого активного режиму
Controller: їжі, PDA/рюкзака, dressing, mutant looting або сюжетної анімації.
Умова з `!` тому не дозволить сцені перервати вже запущене використання
предмета. Сам ефект також безпечно відмовляється від запуску, якщо Controller
зайнятий, актор мертвий, секції немає або її HUD/motion невалідний; причина
записується в лог.

Приклад сюжетної HUD-секції:

```ini
[zat_wpn_ak74_cutscene_1]:animated_item_hud
item_visual      = dynamics\weapons\wpn_ak74\wpn_ak74_hud.ogf
anm_show         = zat_ak74_cutscene_1
snd_show         = characters_voice\scenario\zat_ak74_cutscene_1
cam_eff_name     = camera_effects\zat_ak74_cutscene_1.anm
block_movement   = true
hud_fov_degrees  = 82
function_on_stop = infernis_core.prepare_for_new_cutscene
```

`function_on_stop` є необов'язковим повним ім'ям Lua-функції без аргументів.
Вона викликається рівно один раз лише після штатного завершення анімації — вже
після від'єднання HUD, зупинки звуку/effector та розблокування актора. Тому
callback може одразу запускати наступний Controller. Під час `Cancel()` callback
не викликається: аварійне скасування не повинно помилково просувати сюжет.
Відсутній параметр, порожнє значення, `none` або неіснуюча функція безпечні;
для неіснуючої функції рушій лише залишає повідомлення в логові.

Для прямого Lua-коду також доступні низькорівневі функції:

```lua
local started = level.activate_hud_controller("zat_wpn_ak74_cutscene_1")
local busy = level.is_hud_controller_active()
```
