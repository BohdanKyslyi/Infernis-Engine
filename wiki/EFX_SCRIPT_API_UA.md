# Виклик EFX із Lua-скриптів

EFX більше не потрібно викликати через функції з `xr_effects.script`.
Будь-який файл `.script` може звернутися до окремого модуля `noir_efx`.

## Готовий пресет

```lua
local started = noir_efx.set_preset("cave")
```

Назва має відповідати секції в `configs/environment/noirEnvZone.ltx`.
Функція повертає `true`, якщо запит передано рушію, або `false`, якщо назва
порожня чи дорівнює `none`.

## Вимкнення скриптового перевизначення

```lua
noir_efx.disable_override()
```

Після цього рушій знову використовує фізичну EFX-зону рівня.

## Ручні параметри

```lua
local applied = noir_efx.set_override(
    -1000, -- room
    -100,  -- room_hf
    1.49,  -- decay_time
    0.54,  -- decay_hf_ratio
    0.007, -- reflections_delay
    0.011, -- reverb_delay
    0.0,   -- room_rolloff
    1.0,   -- diffusion
    -2602, -- reflections, optional
    200,   -- reverb, optional
    -5     -- air_hf, optional
)
```

Перші вісім значень обов'язкові. Останні три можна не передавати — модуль
підставить значення SDK за замовчуванням.

Для сумісності доступні також повні назви:

```lua
noir_efx.set_efx_preset("cave")
noir_efx.set_efx_override(...)
noir_efx.disable_efx_override()
```

Старі виклики з LTX-логіки залишаються робочими без змін:

```ini
%=set_efx_preset(cave)%
%=disable_efx%
```
