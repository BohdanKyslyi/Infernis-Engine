# Видимість кісток зброї

Налаштування застосовуються до моделі зброї у світі та до `item_visual` HUD. Якщо в одній із моделей немає вказаної кістки, її пропускають. Старі `wpn_scope`, `wpn_silencer` і `wpn_launcher` продовжують працювати.

```ini
[wpn_example]
def_hide_bones = rail, sight, scope_pu, scope_rakurs
def_show_bones = stock, barrel
scopes_sect = scope_pu_example, scope_rakurs_example

[scope_pu_example]
scope_name = wpn_addon_scope_pu
bones = scope_pu
overriding_hide_bones = sight

[scope_rakurs_example]
scope_name = wpn_addon_scope_rakurs
bones = scope_rakurs
overriding_hide_bones = sight

; Секція ефекту апгрейду (значення поля section у вузлі апгрейду).
[up_sect_example_rail]
show_bones = rail, sight
hide_bones = old_sight
```

Спочатку застосовуються `def_hide_bones` і `def_show_bones` зі зброї. Далі йдуть `hide_bones` та `show_bones` встановлених апгрейдів у порядку встановлення. Наприкінці активний приціл застосовує `overriding_hide_bones` і `bones`. Для глушника й підствольника ті самі два поля можна задати в секції, названій `silencer_name` чи `grenade_launcher_name`. Для прицілу ці поля беруться із секції `scopes_sect`; якщо поля там немає, рушій шукає його в секції `scope_name`.

Усі `bones` доступних змінних аддонів спершу приховуються, щоб зняття чи заміна аддона прибрали його зовнішність. Для інших змінних частин, наприклад рейки, додайте їх до `def_hide_bones`. Секції апгрейдів, що містять лише `show_bones`/`hide_bones`, також вважаються змістовним ефектом апгрейду.
