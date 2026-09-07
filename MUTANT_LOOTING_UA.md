# Нативне зрізання частин тіл мутантів

Механіка працює без Lua та без відкриття інвентарю трупа. Звичайний `Use` біля
налаштованого мертвого мутанта ховає зброю, програє необов'язкову HUD-анімацію,
видає результати рецепта актору й назавжди позначає тушу обробленою.

## Глобальні параметри

Параметри розташовані в
`gamedata/configs/infernis_engine/engine_external.ltx`:

```ini
[items_animations]
enable_mutant_looting_animations = true
mutant_looting_hud               = mutant_looting_hud
enable_mutant_looting_particles  = true
mutant_looting_particle_timing   = 1350
```

- `enable_mutant_looting_animations` вмикає спільну анімацію зрізання.
- `mutant_looting_hud` посилається на controller HUD-секцію.
- `enable_mutant_looting_particles` окремо дозволяє кров'яний particle. Типове
  значення — `false`.
- `mutant_looting_particle_timing` задає запуск particle у мілісекундах від
  початку `anm_show`. Значення, довше за анімацію, затискається до її кінця.

Particle навмисно належить лише анімованому сценарію. Якщо анімації глобально
вимкнені, HUD або `anm_show` невалідні, спрацьовує миттєвий fallback без particle.

## Підключення рецепта до мутанта

У базову секцію конкретного виду мутанта треба додати одне посилання:

```ini
[m_boar_e]:boar_base
mutant_loot_section = mutant_loot_boar
```

`mutant_loot_section = none` або відсутність параметра повністю залишає стару
поведінку мутанта.

## Секція рецепта

```ini
[mutant_loot_boar]
use_tip       = st_mutant_loot_use
particle      = explosions\effects\blood_explosion
particle_bone = bip01_spine

item_1 = mutant_boar_leg, 1, 1.0
item_2 = mutant_boar_hoof, 2, 0.45
item_3 = mutant_boar_eye,  1, 0.10
```

Кожен запис має формат:

```text
item_N = section, count, probability
```

- `section` — чинна секція предмета в конфігах.
- `count` — додатна ціла кількість предметів у цьому результаті.
- `probability` — незалежний шанс у діапазоні від `0.0` до `1.0`.

Усі `item_N` перевіряються незалежно, тому з однієї туші можуть випасти кілька
різних типів частин. Підтримується також один неіндексований ключ `item`.
Невалідні рядки ігноруються з поясненням `MutantLoot` у логові. Якщо валідних
рядків не лишилося, нативна механіка для цієї секції не активується.

`use_tip` — необов'язковий string-table id підказки. Без нього використовується
`character_use` із секції мутанта, а потім штатний `monstr_character_use`.

`particle` і `particle_bone` необов'язкові. `particle = none` вимикає ефект для
конкретного рецепта. Якщо `particle_bone` не задано, рушій пробує старий параметр
мутанта `bone_impuls_abscission`, а потім кореневу кістку. Відсутня кістка також
безпечна: у лог записується fallback до root. Particle створюється без `Hit`,
тобто туша не отримує додаткового імпульсу чи пошкодження.

## HUD-секція

```ini
[mutant_looting_hud]:base_consumable_hud
item_visual      = dynamics\weapons\wpn_knife\wpn_knife_hud.ogf
attach_place_idx = 0
anm_show         = mutant_looting
snd_show         = interface\mutant_looting
action_timing    = 1550
block_movement   = true
```

- `anm_show` — єдина потрібна фаза. `anm_idle` та `anm_hide` не читаються.
- `snd_show` — необов'язковий звук.
- `action_timing` — момент видачі предметів у мілісекундах. Для сумісності
  читається також `timing`. Без обох параметрів результат видається наприкінці
  анімації.
- `block_movement = true` блокує рух, нахили та поворот камери на весь час
  анімації. Можна використати короткий псевдонім `block_move`; якщо присутні
  обидва ключі, пріоритет має `block_movement`.
- Camera effector підключається так само, як для інших анімацій контролера:
  через `cam_eff_name` або автоматичний файл за назвою реально обраного motion.

## Поведінка та сумісність

- Тушу можна обробити через 3 секунди після смерті й лише на відстані до 2.5 м.
- Рецепт резервується на час анімації. Скасування до `action_timing` звільняє
  резерв; після видачі результат не може дублюватися.
- Стан «уже зрізано» зберігається у server object та переживає save/load,
  online/offline переходи.
- Для мутанта з валідним `mutant_loot_section` старий інвентар трупа, Lua
  `use_callback` та legacy `Spawn_Inventory_Item_Section` не видають дублікати.
- `Shift+Use` не запускає рецепт і лишається доступним для штатного перетягування
  фізичного тіла, але так само не відкриває старий інвентар і не викликає Lua.
- Якщо глобальна анімація вимкнена або її HUD/motion відсутні, предмети однаково
  видаються миттєво, а причина fallback записується в лог.

Для діагностики шукайте повідомлення з префіксом `MutantLoot` у `xray_*.log`.
