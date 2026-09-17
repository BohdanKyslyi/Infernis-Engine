# Виклик функцій будь-якого Lua-скрипта з LTX-логіки

Умови та ефекти в condlist тепер підтримують повне ім'я функції у форматі
`назва_скрипта.назва_функції`.

## Ефект

Файл `gamedata/scripts/my_quest.script`:

```lua
function start_scene(actor, npc, p)
    local scene_name = p and p[1]
    printf("start scene: %s", tostring(scene_name))
end
```

Виклик із LTX:

```ini
on_info = {+start_my_scene} %=my_quest.start_scene(intro)% next_section
```

## Умова

Файл `gamedata/scripts/my_quest.script`:

```lua
function actor_is_ready(actor, npc, p)
    return actor ~= nil and actor:alive()
end
```

Виклик із LTX:

```ini
on_info = {=my_quest.actor_is_ready} next_section
```

Параметри передаються так само, як раніше: третім аргументом `p` у вигляді
таблиці рядків.

Старий короткий синтаксис повністю сумісний:

```ini
%=give_info(test_info)%
{=actor_alive} next_section
```

Без назви скрипта ефекти й надалі шукаються у `xr_effects.script`, а умови — у
`xr_conditions.script`.
