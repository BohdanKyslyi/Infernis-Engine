# Індивідуальний viewport near для HUD-секцій

Будь-яка HUD-секція може необов'язково перевизначити ближню площину HUD-проєкції.
Механіка працює для зброї, детекторів і тимчасових моделей
`ItemUseController`.

## Глобальне значення

Значення за замовчуванням, як і раніше, задається в `engine_external.ltx`:

```ini
[hud_extensions]
viewport_near = 0.2
```

Якщо активна HUD-секція не має власного параметра, рушій використовує саме це
значення.

## Локальне значення

Параметр додається безпосередньо до потрібної HUD-секції:

```ini
[wpn_ak74_hud]
item_visual      = dynamics\weapons\wpn_ak74\wpn_ak74_hud.ogf
attach_place_idx = 0
viewport_near    = 0.05
```

Після attach цієї HUD-моделі `IE_VIEWPORT_NEAR` тимчасово стане `0.05`. Після
detach рушій автоматично відновить глобальне значення `0.2` з
`engine_external.ltx`.

## ItemUseController

Той самий параметр можна використовувати в секціях анімацій їжі, ПДА, рюкзака,
вдягання та mutant looting:

```ini
[mutant_looting_hud]:base_consumable_hud
item_visual      = dynamics\weapons\wpn_knife\wpn_knife_hud.ogf
attach_place_idx = 0
viewport_near    = 0.035
anm_show         = mutant_looting
```

Локальне значення застосовується тільки після успішного attach Controller HUD і
скидається під час завершення, скасування або fallback.

## Пріоритет і перевірка

Джерело `viewport_near` обирається так само, як джерело HUD FOV:

1. активний Controller HUD;
2. основний предмет у `attach_place_idx = 0`;
3. додатковий предмет у `attach_place_idx = 1`;
4. `[hud_extensions] viewport_near` з `engine_external.ltx`.

Якщо пріоритетна HUD-секція не має локального параметра, використовується
глобальне значення; параметр другорядного HUD-предмета не перехоплює проєкцію.

Допустимий діапазон локального значення — від `0.001` до `1.0`. Некоректне
значення ігнорується, а в лог додається повідомлення `HUD viewport near` із
назвою секції. Відсутність параметра повністю сумісна зі старими конфігами.
