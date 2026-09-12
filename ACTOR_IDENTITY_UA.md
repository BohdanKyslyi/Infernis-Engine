# Динамічні дані головного героя

Infernis Engine дозволяє змінювати ім'я, іконку, угруповання, ранг і репутацію
головного героя з ігрової логіки. Зміни зберігаються в сейвах і відображаються
у вікнах, які використовують `CUICharacterInfo`, зокрема в інвентарі.

## Використання в логіці

```ini
[sr_idle@0]
on_info = {+change_character_to_bodian} %=change_actor_name(st_bodian) =change_actor_community(csky) =change_actor_icon(ui_inGame2_Bodian) =change_actor_rank(100) =change_actor_reputation(25)% sr_idle@1

[sr_idle@1]
```

- `change_actor_name(string_table_id)` задає ім'я. Параметр може бути ключем із
  `string_table` або звичайним текстом.
- `change_actor_icon(texture_id)` задає назву UI-текстури портрета.
- `change_actor_community(community_id)` змінює угруповання та команду актора.
- `change_actor_rank(value)` задає абсолютне числове значення рангу.
- `change_actor_reputation(value)` задає абсолютне числове значення репутації.

Значення рангу й репутації не додаються до поточних: виклик із `100` встановить
рівно `100`.

## Прямі Lua-методи

Ті самі зміни можна виконувати з будь-якого скрипта:

```lua
db.actor:set_character_name("st_bodian")
db.actor:set_character_icon("ui_inGame2_Bodian")
db.actor:set_character_community("csky", 0, 0)
db.actor:set_character_rank(100)
db.actor:set_character_reputation(25)
```

Щоб повернути портрет із `characters_desc_general`, передайте порожній рядок
прямому методу:

```lua
db.actor:set_character_icon("")
```
