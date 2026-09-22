# Швидкий удар ножем і швидкий кидок гранати

Дії керування:

```ltx
bind quick_kick kV
bind quick_grenade kG
```

## Ніж

```ltx
[items_animations]
enable_quick_kick = true
quick_kick_hud = quick_kick_hud

[quick_kick_hud]:animated_item_hud
item_visual = dynamics\weapons\wpn_knife\wpn_knife_hud.ogf
anm_show = knife_kick_1
snd_show = weapons\knife_1
action_timing = 250
```

`action_timing` — момент реального удару в мілісекундах. Якщо ключ відсутній,
удар відбувається посередині анімації. Параметри шкоди беруться з ножа у
`KNIFE_SLOT`. Сумісний псевдонім глобального ключа: `quick_knife_hud`.

## Граната

```ltx
[items_animations]
enable_quick_throw_grenades = true

[grenade_f1_hud]
anm_throw_quick = grenade_throw_quick

[grenade_f1]
snd_throw_quick = weapons\grenade_quick
```

Кидок використовує `force_const`, спрацьовує за motion mark і повертає
попередній активний слот. Без motion mark граната відпускається наприкінці
анімації. Без `anm_throw_quick` використовується стандартна послідовність
`anm_throw_begin` → `anm_throw`.
