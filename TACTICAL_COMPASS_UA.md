# Тактичний компас Infernis Engine

Нативна горизонтальна шкала напрямків у HUD одиночної гри. Вона орієнтується за напрямком камери; під шкалою відображаються маркери точок карти. Компас працює без Lua-скриптів чи MCM аддону для Anomaly.

## Налаштування

У `gamedata/configs/infernis_engine/engine_external.ltx`:

```ini
[ui_extensions]
enable_tactical_compass = true
```

Відсутній ключ вважається увімкненим для сумісності зі старими конфігами. Розміщення, розмір і дистанція маркерів задаються в `gamedata/configs/ui/ui_tactical_compass.xml`:

```xml
<tactical_compass x="332" y="24" width="360" height="40"
                  show_markers="1" marker_max_distance="500">
```

Маркери можна вимкнути окремо (`show_markers="0"`). Наразі підтримуються типи точок карти, назви яких містять `task_location` / `quest_location`, `treasure` / `stash`, `level_changer` / `transition`. Для офлайн-цілей відображається лише позиція на поточному рівні, якщо вона доступна в `CMapLocation`. Показуються до 16 найближчих точок, список перевіряється раз на 200 мс; самі позиції й шкала оновлюються разом із камерою.

Текстури шкали, рамки й маркерів взято з наданого архіву Tactical Compass 1.1 (автори, зазначені в його скриптах: Strogglet15, Tronex, RavenAscendant, explorerbee; попередній дизайн A.N.T.H.O.L.O.G.Y.). Це окремий HUD-компонент: MCM-налаштування та всі категорії маркерів аддону Anomaly сюди не перенесені.
