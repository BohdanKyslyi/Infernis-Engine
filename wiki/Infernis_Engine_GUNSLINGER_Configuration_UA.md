# GUNSLINGER в Infernis Engine — довідник конфігурації

Довідник для пакувальника збірки, автора зброї та розробника рушія. Наведені фрагменти LTX додаються в уже робочі секції або успадковуються від них. Вони не замінюють повний конфіг зброї з класом, моделями, звуками й балістикою.

- [Глобальні опції та мутанти](#глобальні-опції-та-мутанти)
- [Карта секцій і перенесення зброї](#карта-секцій-і-перенесення-зброї)
- [Зброя, боєприпаси й балістика](#зброя-боєприпаси-й-балістика)
- [HUD, поза та інерція](#hud-поза-та-інерція)
- [Анімації, звуки й таймінги](#анімації-звуки-й-таймінги)
- [Приціли, обвіси й модернізація](#приціли-обвіси-й-модернізація)
- [Детектори, ліхтарі та ЛЦВ](#детектори-ліхтарі-та-лцв)
- [Гранати, підствольники та РПГ-7](#гранати-підствольники-та-рпг-7)
- [Гаусс, вогнемет і електроніка](#гаусс-вогнемет-і-електроніка)
- [Хоткеї, Lua та перевірка збірки](#хоткеї-lua-та-перевірка-збірки)

## Глобальні опції та мутанти

Файл `gamedata/configs/infernis_engine/engine_external.ltx`. Відредагуйте ефективну секцію збірки й перезапустіть гру. Значення всіх семи перемикачів за замовчуванням — `true`. Щоб зняти всі додаткові ефекти мутантів GUNSLINGER, достатньо одного рядка:

```ini
[gunslinger_ie]
enable_monster_extensions = false
```

Для індивідуального налаштування встановіть загальний ключ у `true` і вимикайте потрібні дії.

| Ключ | Ефект `false` |
|---|---|
| `enable_monster_extensions` | Вимикає всі наведені нижче доповнення мутантів навіть тоді, коли окремі ключі мають `true`. |
| `controller_weapon_attack` | Контролер застосовує звичайну псиатаку. Захоплення зброї, сценарний постріл і примусовий запуск підствольника вимикаються. |
| `burer_enhanced_stamina_wave` | Удар по витривалості використовує базовий розрахунок за вагою зброї та штатне вибивання. Прибирає посилений удар, знос і окрему HUD-реакцію `burer_kicks`. |
| `burer_tactics` | Прибирає додатковий вибір атаки й утримання щита на основі прицілювання гравця, гранат, заряджених гранатометів і лікування. Прибирає спеціальне переривання лікування. |
| `burer_telekinetic_shooting` | Бюрер зберігає телекінез із киданням предметів. Піднята зброя не стріляє під його контролем. |
| `boar_impact` | Кабан завдає звичайної шкоди ударом. Прибирає додаткову втрату витривалості, знос і вибивання зброї через цей удар. |
| `pseudogiant_impact` | Звичайний удар і тупіт зберігають штатну шкоду та ефекти. Прибирає додаткові втрату витривалості, знос і вибивання зброї. |

Штатна гравітаційна хвиля бюрера — окрема атака. Її параметри залишаються в секції монстра `Gravi_*`. У [gunslinger_ie] немає перемикачів зброї, анімацій, оптики чи електроніки. Після зміни опцій можна перевіряти на тому самому сейві, завантаженому після перезапуску.

### Баланс для збірки

`[gunslinger_base]` — секція даних GUNSLINGER, доступна через `system.ltx` і його включення.

| Параметри | Призначення |
|---|---|
| `boar_stamina_hit_k`, `pseudogiant_stamina_hit_k`, `pseudogiant_threaten_stamina_hit_k` | Множники витривалості від сили удару. Типове значення `1`. `threaten` застосовується до тупоту. |
| `boar_hit_*`, `pseudogiant_hit_*`, `pseudogiant_threaten_*` | Для кожного префікса `*_unconditional_weapon_drop_dist` визначає радіус безумовної спроби роззброєння в метрах. Типово `0` для звичайних ударів і `5` для тупоту. `*_conditiondecmin` і `*_conditiondecmax` задають діапазон зносу зброї після успішної спроби. |
| `burer_superstaminahit_value`, `burer_superstaminahit_dist` | Сила й дистанція посиленої хвилі. Типово `0` і `10` м. Нульова сила вимикає саме посилення. Бюрер також враховує прицілювання й ознаку снайперської зброї. |
| `burer_superstaminahit_conditiondecmin`, `burer_superstaminahit_conditiondecmax` | Діапазон зносу магазинової зброї від посиленої хвилі. Типовий мінімум `0`, відсутній максимум дорівнює мінімуму. |
| `burer_forceshield_dist`, `burer_forceantiaim_dist` | Дистанції реакції на загрозу зброєю та близький ніж. Типово `3` і `7` м. |
| `burer_aim_shield_delay_const`, `burer_aim_shield_delay_random`, `burer_shielded_risky_factor` | Стала й випадкова частини затримки щита в мс. Типово `500` і `1000`. Останній ключ — імовірність завершення щита після штатного таймера під загрозою, типово `0.02`. |
| `disable_planned_kicks` | Вимикає HUD-аніматори запланованих реакцій від ударів. Для керування витривалістю та роззброєнням використовуйте перемикачі [gunslinger_ie]. |

У **секції бюрера** `Tele_Shooting_From_Weapon_Enable` і `Tele_Max_Pickuped_Weapons` задають локальний дозвіл і ліміт зброї, що може стріляти одночасно. Типово `true` і `1`. Глобальний `false` має пріоритет. `weight_to_stamina_hit`, `weapon_drop_stamina_k`, `weapon_drop_velocity`, `shield_time`, `shield_cooldown` налаштовують базову поведінку. У **секції зброї** `is_burer_see_sniper_weapon = true` розширює реакцію бюрера на зброю.

HUD-групи реакцій — `[burer_kicks]`, `[boar_front_kicks]`, `[boar_back_kicks]`, `[pseudogiant_front_kicks]`, `[pseudogiant_back_kicks]`, `[pseudogiant_threaten_kicks]`. У групі `count` і `animator_0`…`animator_{count-1}` посилаються на секції предметів-аніматорів із `hud`. HUD-аніматор із `attach_place_idx = 0` відтворює `anm_show`. Рушій перебирає доступні кандидати. Перевірте, що кліпи справді є в OMF поточних рук.

## Карта секцій і перенесення зброї

| Секція | Що в ній має бути |
|---|---|
| `[wpn_*]` | Клас і World-модель `visual`, `hud`, боєприпаси, зум камери, параметри стрільби, сумісність обвісів, більшість звуків. |
| `[wpn_*_hud]` | `item_visual`, `attach_place_idx`, пози рук і предмета, точки пострілу/гільз, анімації та часові ключі, ADS-поза, інерція, HUD FOV. |
| Секція обвісу / wrapper прицілу | `scope_name` на сам приціл, окремі `aim_hud_offset_*`, маски кісток та параметри варіанта на цій зброї. Типове місце для спільних параметрів лінзи — секція самого прицілу. |
| Секція ефекту апгрейду | Значення ефекту, наприклад `hud`, `laser_installed`, `supports_detector`, `pistol_slot_allowed`, накладки іконок. Дерево апгрейдів зв’язує групи `elements`, вузли `section` з ефектами та дочірні `effects`. |
| `[ammo_*]`, секція гранати або ракети | `hit_type` / `material`, класи й моделі снарядів, параметри вибуху та детонації. |
| `[gunslinger_base]` / `[gunslinger_ie]` | Загальні налаштування донорських механік та окремі глобальні перемикачі Infernis відповідно. |

Спочатку перенесіть робочі базові секції `[wpn_*]` і `[wpn_*_hud]`, відповідні OGF/OMF, патрони, звуки, текстури та шейдери. Перевірте базові `anm_show`, `anm_hide`, `anm_idle`, `anm_shoot` або `anm_shots`, `anm_reload`. Для `[wpn_*_hud]` звірте `hands_position`, `hands_orientation`, `item_position`, `item_orientation`, `fire_bone`, `fire_point`, `fire_point2`, `shell_bone`, `shell_point`. Додавайте тільки параметри тих механік, для яких є моделі, кістки, звуки та кліпи.

### Сумісність старої зброї

| Ситуація | Що перевірити |
|---|---|
| Старий формат зуму | Залиште `gunslinger_zoom = false` або не вказуйте ключ. Для перенесеної зброї з кратністю замість старих значень FOV явно поставте `gunslinger_zoom = true`. Перевірте значення окремо в секціях зброї та прицілів. |
| Ванільний HUD без нових рухів | `gunslinger_hud_movement`, `gunslinger_movement_idles`, `enable_directions_anm_idle_aim`, `use_advanced_ammo_bones` типово вимкнені. Підключайте кожен після імпорту відповідних ресурсів. |
| Рушій уже має `chamber_round` | `chamber_round` має пріоритет над донорським `ammo_in_chamber`. `minus_ammo_in_usual_reloads` і `minus_ammo_in_bore` змінюють видимий облік кісток патронів, а не реальну місткість. |
| Стара модель збереглась у сейві | `force_reset_visual_on_load = true` примушує перечитувати `visual`. `banned_visuals` блокує лише вказані моделі. Для випадку SIG-552 перелічіть неправильні шляхи з конкретної збірки. Перевірте і сейв, і апгрейд, що міняє візуал. |
| Є додатковий пістолетний слот | В [slots] потрібні `enable_modular_slots = true` і `pistol_slot = true`. Зброя без `pistol_slot_allowed` використовує звичайні правила слотів. Секція `[pistol_slot_blocker]` набуває змісту через успадковане `pistol_slot_allowed = 0`. Дозвіл апгрейдів зброї накопичується до `1.0`. Броня може дозволити слот власним значенням від `1.0`. Втрата дозволу повертає предмет у рюкзак. |
| Альтернативний ADS або підствольник | У кожного режиму свої поза, зум та кліпи. Перевірте ADS від стегна, пряме перемикання між прицілами й повернення з режиму підствольника. |

### Мінімальний профіль для магазинової зброї

Зразок зміни до існуючих секцій. Імена кліпів та позу залишайте від своєї моделі.

```ini
[wpn_example]
hud = wpn_example_hud
gunslinger_zoom = true
scope_zoom_factor = 1
ironsight_zoom_factor = 1
chamber_round = true
supports_detector = true

[wpn_example_hud]
attach_place_idx = 0
gunslinger_hud_movement = true
zoom_inertion = true
inertion_aim_speed = 10
inertion_aim_origin_offset = 0.01
aim_hud_offset_pos = 0, 0, 0
aim_hud_offset_rot = 0, 0, 0
```

## Зброя, боєприпаси й балістика

| Ключ / група | Де і як застосовується |
|---|---|
| `fire_modes`, `max_queue_size` | Зброя або відповідний ефект апгрейду. `fire_modes` — список режимів, `-1` означає автоматичний. Додатний `max_queue_size` додатково обмежує чергу. Для Гаусса перевірте з урахуванням `recharge_time`. |
| `base_dispersioned_bullets_count`, `base_dispersioned_bullets_speed`, `base_dispersioned_bullets_time_delta` | Перші кулі черги. Останній ключ — інтервал у секундах. Для подвійного циклу АН-94 можна поставити `0.035`. За відсутності окремого інтервалу використовується стандартний темп. |
| `singleshoots_time_delta` | Окремий інтервал між одиночними пострілами в секундах. Нуль лишає штатний темп. |
| `ammo_mag_size_for_type_N` | Місткість для індексу `N` у поточному списку `ammo_class`, починаючи з нуля. Рушій враховує секції ефектів модернізації. Перевіряйте після зміни типу та апгрейду. |
| `use_light_misfire`, `light_misfire_start_condition`, `light_misfire_end_condition`, `light_misfire_start_probability`, `light_misfire_end_probability` | У секції зброї. Окремий легкий збій залежно від стану зброї, з власною реакцією HUD та `snd_light_misfire`. `disable_light_misfires_with_detector` у HUD блокує його з детектором. |
| `gunslinger_last_round_jam`, `gunslinger_jam_after_shot` | У HUD. Сценарії клину на останньому набої й переходу в клин після пострілу. Узгодьте з кліпами `anm_shoot*_jammed*`, `anm_fakeshoot*_jammed` та `anm_reload_jammed_last*`. |
| `detector_disp_factor` | У зброї, типово `1`. Множник розкиду при працюючому детекторі в руці. |
| `bullet_point_offset_hud`, `bullet_point_offset_world`, `hud_recalc_koef` | Перші два ключі — у зброї. Останній — у HUD. Налаштовують початок пострілу в адаптованій балістиці. Початок пробивається трасою від камери, щоб не стріляти крізь стіну біля ствола. |
| `g_real_shooting` | Консольний перемикач балістики. Шлях GUNSLINGER для гравця активується перемикачем або наявністю одного з `bullet_point_offset_hud`, `bullet_point_offset_world`, `hud_recalc_koef`. У цій гілці ADS від першої особи використовує вісь камери. Від стегна напрям обирається з урахуванням складності, ЛЦВ і перемикача. |
| `hit_type`, `material` | У боєприпасі. Наприклад `hit_type = burn` і `material = objects\bullet_fire` для пального. Матеріал має існувати у збірці. Без ключів діють штатні параметри. |

У `[gunslinger_base]` `fov` та `hud_fov` служать опорними величинами для корекції траєкторії (типово `65` і `40`). `hud_fov` конкретного предмета задає проєкцію його моделі. В ефектах апгрейду враховуються `crosshair_inertion` і `fire_dispersion_condition_factor`.

## HUD, поза та інерція

| Параметри | Де і як застосовуються |
|---|---|
| `aim_hud_offset_pos`, `aim_hud_offset_rot`, `gl_hud_offset_pos`, `gl_hud_offset_rot` | HUD-поза для ADS і підствольника. Обертання в радіанах. Для широкого екрана додаються варіанти `_16x9`. Для знімної оптики wrapper/приціл можуть мати власний ADS-зсув. |
| `alter_zoom_allowed`, `alter_aim_hud_offset_pos`, `alter_aim_hud_offset_rot`, `alter_scope_zoom_factor` | Вмикають альтернативний ADS і задають позу та зум. Пози також мають `_16x9`. Перевірте HUD і wrapper, оскільки альтернативний приціл може бути прив’язаний до певної оптики. |
| `alter_zoom_direct_switch_speed` | HUD або приціл. Керує швидкістю прямого переходу між двома ADS. Нуль означає миттєвий перехід. Без ключа використовується звичайний час повороту зброї в ADS. |
| `hud_fov`, `hud_fov_factor`, `hud_fov_zoom_factor`, `hud_fov_alter_zoom_factor`, `hud_fov_gl_zoom_factor` | HUD-проєкція та її множники для різних режимів. Для прицілів застосовуються налаштовані override. FOV камери та FOV моделі рук налаштовуються окремо. |
| `viewport_near` | Опційна ближня площина HUD конкретного предмета, глобальна — [hud_extensions]. Звіряйте при обрізанні зброї або рук біля екрана. |
| `hud_inertion`, `zoom_inertion` | Вмикають HUD-інерцію предмета та інерцію в ADS відповідно. Для зброї без ADS-інерції лишіть `zoom_inertion` відсутнім або `false`. |
| `inertion_*_speed`, `*_origin_offset`, `*_pitch_offset_r`, `*_pitch_offset_n`, `*_pitch_offset_d` | Для hip використовується `inertion_`, для ADS — `inertion_aim_`, для підствольника — `inertion_gl_`, після префікса йде назва поля, наприклад `inertion_aim_pitch_offset_r`. `speed` керує згасанням, `origin_offset` — відставанням, `pitch_offset_*` — статичною поправкою від нахилу камери. |
| `inertion_default_*`, `inertion_aim_default_*` | Значення в [gunslinger_base]. За Stage 48.1 відсутні ADS-коефіцієнти нахилу мають нульовий fallback, явні значення враховуються. Приціл може множити hip-параметри через `inertion_<поле>_factor` і перевизначати відповідні ADS/GL-поля. |
| `gunslinger_hud_movement` | У HUD. Активує пози руху, присідання, нахилу і пов’язані з ними профілі камери. |
| `hud_move_<дія>_offset_pos`, `hud_move_<дія>_offset_rot`, `hud_aim_move_<дія>_offset_pos`, `hud_aim_move_<дія>_offset_rot` | Вектори в HUD. `_16x9` має пріоритет на широкому екрані. Дії включають `left`, `right`, `forward`, `back`, `llookout`, `rlookout`, переходи `to_crouch`, `from_crouch`, `to_slow_crouch`, `from_slow_crouch`, `to_llookout`, `from_llookout`, `to_rlookout`, `from_rlookout`. Код читає тільки дії відповідного режиму, наприклад `forward` і `back` тут працюють у hip. |
| `<перехід>_time`, `hud_move_speed_pos`, `hud_move_speed_rot` | Тривалість переходів у секундах, типові швидкості пози й повороту `0.1` та `0.4`. |
| `hud_move_stabilize_factor`, `hud_move_weaponhide_factor`, `hud_move_unzoom_factor`, `hud_move_slow_factor`, `hud_move_crouch_factor`, `hud_move_slow_crouch_factor`, `hud_move_crouch_strafe_factor`, `hud_move_slow_crouch_strafe_factor`, `hud_move_llookout_offset_speed_factor`, `hud_move_rlookout_offset_speed_factor` | Множники стабілізації, ховання, ADS, повільного руху, присідання, бокового руху та швидкості нахилів. |

`gunslinger_camera_smoothing` і `gunslinger_camera_jump_inertia` читаються з HUD, коли використовується GUNSLINGER-профіль руху. `actor_camera_speed_factor` — у зброї або ефекті апгрейду. У [gunslinger_base] доступні `default_actor_camera_speed` (`18`), `actor_camera_speed_pow` (`1.2`), `actor_camera_air_velocity_factor` (`0.015`), `actor_camera_air_max_offset` (`0.06`) та три групи приземлення. `actor_camera_landing_*`, `actor_camera_landing2_*` підтримують `time`, `offset`, `speed_factor`, `speed_pow_factor`. `actor_camera_finish_landing_*` підтримує `time`, `speed_factor`, `speed_pow_factor`.

Якщо сітка однаково зміщена при будь-якому напрямку погляду, перевіряйте ADS-позу, кістки та HUD FOV. Якщо зміщення міняє знак при погляді вгору й униз за нерухомої камери, перевіряйте `inertion_aim_pitch_offset_*` і `inertion_aim_default_pitch_offset_*`. Видиме відставання лише під час руху задається `speed` та `origin_offset`.

## Анімації, звуки й таймінги

Рушій шукає псевдонім у HUD-секції, а потім відповідний кліп в анімаціях рук і предмета. `anm_* = кліп_рук, кліп_предмета, множник_швидкості` — форма з усіма полями. Перевіряйте актуальний OMF для рук і HUD після апгрейду. Варіанти автоматично підхоплюються лише в тих станах, у яких рушій їх шукає. Додавайте конкретні аліаси з донорського HUD і тестуйте їх у грі.

| Стан | Аліаси та супутні ключі |
|---|---|
| Діставання, ховання й очікування | `anm_show`, `anm_hide`, `anm_idle`, `anm_idle_aim`, `anm_idle_aim_scope`, варіанти `_empty` / `_jammed`. За відсутності спеціального кліпу використовується доступний базовий. |
| Вхід і вихід з ADS | `anm_idle_aim_start`, `anm_idle_aim_end` та їхні `_empty` / `_jammed`. Звуки `snd_aim_start`, `snd_aim_end` у зброї. `allow_halfaimstate` задається у HUD та в сумісної оптики. Порядок натискання ADS і вогню визначає, яка дія першою почнеться. |
| Рух та біг | `anm_idle_moving*`, `anm_idle_sprint*`, `anm_idle_sprint_start*`, `anm_idle_sprint_end*`, `anm_idle_aim_moving*`. `gunslinger_movement_idles` і `enable_directions_anm_idle_aim` вмикають розширений підбір. Для спринту `snd_sprint_start`, `snd_sprint_end`, `disable_sprint_start_snd`, `disable_sprint_end_snd` у зброї. |
| Постріл | `anm_shoot`, `anm_shoot_aim`, `anm_shoot_aim_scope`, `anm_shots` і варіанти `_last`, `_sil`, `_jammed`, `_last_sil`, `_jammed_sil` залежно від класу й стану. |
| Сухий спуск, клин, осічка | `anm_fakeshoot`, `anm_fakeshoot_aim`, `anm_fakeshoot_empty`, `anm_fakeshoot_aim_empty`, `anm_fakeshoot_jammed`, `anm_fakeshoot_aim_jammed`, `anm_shoot_lightmisfire`, `anm_shoot_lightmisfire_aim`. |
| Перезаряджання | `anm_reload`, `anm_reload_empty`, `anm_reload_jammed`, `anm_reload_jammed_last`, `anm_reload_ammochange`, `anm_reload_empty_ammochange` та варіанти `_detector`. У зброї задавайте відповідні `snd_reload*` і `snd_changecartridgetype` за наявності. |
| Огляд | Хоткей `wpn_inspect`. `anm_boring` / `anm_bore` та `_jammed`, у класах зброї також спеціальні варіанти. `snd_boring` у зброї, `disable_bore` у HUD. |
| Селектор | HUD `use_firemode_change_anim`, `anm_changefiremode_from_<режим>_to_<режим>` (число або `a` для авто). Варіанти стану та обвісів залежать від класу. |
| Зняти/встановити глушник | `use_silattach_anim`, `use_sildetach_anim` і `anm_attach_sil`, `anm_detach_sil` у HUD. |
| Розрядити магазин | `use_unloadmag_anim` і `anm_unload_mag` у HUD. Прив’язуйте таймінг виймання набоїв до кліпу. |
| Початок і змішування черги | `mix_shoot_after_idle`, `mix_shoot_after_reload`, `mix_shoot_after_shoot_in_queue`, `cyclic_shoot_animations`, `cyclic_shoot_start_animation`, `cyclic_shoot_sound`, `stop_all_sounds_before_idle`. Кліпи `anm_shoot_start`, `anm_shoot_start_aim` та основна анімація вогню. |
| Маски | `mask_firemode_N`, `mask_firemode_a` — рядкові суфікси варіантів для селектора, анімацій руху, вогню та перезарядки. Наприклад, якщо значення містить підкреслення, воно стає частиною імені аліаса. Перевіряйте порядок суфіксів у `MaskedHudMotion`, `WeaponActionMotion` або `FindLauncherMotion`. |

Розширені рухи від прицілювання в магазиновій зброї використовують сім’ю `anm_idle_aim_moving…`. Відсутність донорського кліпу може дати звичайний idle замість очікуваного напрямку. Використовуйте позначку stop-at-end для переходів, завершення яких має повернути зброю до idle. Заміна `hud` в апгрейді має нести всі потрібні базові кліпи або успадкувати їх.

Частина звукових варіантів читається безпосередньо з секції зброї. Наприклад `snd_reload_jammed_last`, `snd_reload_jammed_detector`, `snd_reload_jammed_last_detector`, `snd_attach_sil`, `snd_detach_sil`, `snd_changefiremode`, `snd_unload_mag`, `snd_breechblock`, `snd_suicide`, `snd_stop_suicide`. Для HUD з окремими рухами глушника або детектора перевірте і звуки обох варіантів.

### Часові ключі та звук конкретного руху

У HUD суфікс після префікса — **повний вибраний аліас**, наприклад `lock_time_start_anm_reload_empty_detector`. Часи в секундах, якщо нижче не вказано інше.

| Шаблон | Призначення |
|---|---|
| `lock_time_<anm>` | Тривалість блокування дії або спеціального руху. |
| `lock_time_start_<anm>`, `lock_time_end_<anm>` | Мітки, за якими дія застосовується, знімається блокування чи завершується фаза. Точний зміст залежить від дії. Перевіряйте кадр зміни магазину, глушника, перемикача ЛЦВ або удару. |
| `immediate_unhide_<anm>` | Дозвіл одразу повернути детектор після вибраної перезарядки. |
| `snd_<anm>`, `snd_exclusive_<anm>` | Звук та ексклюзивне відтворення для конкретного HUD-руху там, де це перевіряє код класу. Стандартні `snd_*` зброї лишаються запасним варіантом. |
| `autoshoot_<anm>` | Підтримувані дії можуть відкласти спуск до завершення руху. |
| `ejection_delay_<anm>` | Затримка оновлення видимих набоїв у мс для конкретного руху. |

### Дробовики та барабани

- **Помпові/автоматичні з поштучним заряджанням.** У зброї `save_cartridge_in_ammochange` (типово `true`), `need_final_close_anims`, `need_first_shoot_anims`, `no_jam_in_first_shot`. У HUD `add_cartridge_in_open`, `empty_preload_mode`, `revolver_reload`, `first_state_after_jammed`. Звіряйте `anm_open`, `anm_add_cartridge`, `anm_close`, `anm_shoot_first` і потрібні варіанти `_empty`, `_first`, `_preloaded`, `_final`. Звукові варіанти у зброї — `snd_open_weapon_empty`, `snd_add_cartridge_empty`, `snd_add_cartridge_preloaded`, `snd_close_weapon_empty`, `snd_close_weapon_preloaded`.
- **Двостволки.** Донорський шлях визначається наявністю `anm_shoot_1`, `anm_shoot_2`, `anm_reload_0`. Налаштуйте варіанти для кількості заряджених стволів, стану `_jammed_N`, а також `no_jam_fire` і `aim_scope_anims` у HUD. `use_scope_anims` належить до оптики. Повторіть тест після апгрейду HUD і зміни `ammo_class`.
- **Пістолети.** `snd_close` у секції зброї може використовуватися на переході з порожнього стану. Старі пістолети без нового HUD-руху лишаються на базовому сценарії.

### Кістки боєприпасів і World-анімації

| Параметри | Де і як застосовуються |
|---|---|
| `use_advanced_ammo_bones`, `ammo_params_section`, `ammo_params_section_N` | HUD. Прив’язують секцію з `configuration_N`, де `N` у конфігурації — видимий лічильник. У `ammo_params_section_N` це **індекс типу** в `ammo_class`. |
| `ammo_params_toggle_shooting`, `ammo_params_changing_when_reload_starts`, `ammo_params_use_previous_shot_type`, `ammo_params_use_last_cartridge_type` | HUD. Час перемикання наборів кісток і джерело типу набою. |
| `minus_ammo_in_usual_reloads`, `minus_ammo_in_bore` | HUD. Поправка видимого лічильника для анімацій із патроном у патроннику. |
| `use_ammo_bones`, `ammo_bones_prefix`, `ammo_hide_bones_prefix`, `ammo_var_bones_prefix`, `start_ammo_bone_index`, `end_ammo_bone_index`, `ammo_divisor_up`, `ammo_divisor_down` | Старий режим на кшталт ГШ-18 і СР-1М. Усі ключі в HUD. Умовою запуску є `use_ammo_bones = true` і відсутній/вимкнений `use_advanced_ammo_bones`. Дільник типовий `1`. |
| `additional_ammo_bone_when_jammed` | В обраній `ammo_params_section` для розширеного режиму або в HUD для старого. |
| `firemode_bones_total`, `firemode_bones_N`, `firemode_bones_a` | У HUD, застосовуються до HUD і World. `total` перелічує всі кістки селектора, інші ключі — видимі для режиму. SIG-550 використовує, зокрема, `firemode_bones_1`, `firemode_bones_3` та `firemode_bones_a`. |
| `use_world_anims`, `wanm_idle`, `wanm_draw`, `wanm_holster`, `wanm_shoot`, `wanm_reload` | У зброї. Потрібні анімації у World-моделі. Варіанти підбираються за останнім пострілом, порожнім/заклиненим станом, режимом вогню та підствольником. Початкова поза застосовується до створення фізичної оболонки. |

## Приціли, обвіси й модернізація

| Параметри | Призначення |
|---|---|
| `gunslinger_zoom`, `scope_zoom_factor`, `ironsight_zoom_factor`, `nonscoped_zoom_factor`, `gl_zoom_factor`, `zoom_rotate_time` | Зум камери та тривалість переходу. `gunslinger_zoom = true` означає формат кратності, наприклад `1.25`, а старий формат слід зберігати для ванільних секцій. Параметри прицілу можуть перекривати значення зброї. |
| `zoom_mouse_sense_koef`, `zoom_gl_mouse_sense_koef` | Множники чутливості ADS / підствольника, типово `1`. Для основного ADS параметр може читатися з оптики. Для ADS діють і поправки апгрейдів. |
| `collimator`, `scope_3d`, `need_lens_frame`, `scope_lens_fov` | Коліматор або 3D-лінза. Для PiP потрібні сумісні модель, матеріал, активний R2/R3/R4 та [shader_extensions] `enable_3d_scopes = true`. `need_lens_frame` може активувати донорську лінзу без явного `scope_3d`. На R1 використовуйте наявний fallback. |
| `scope_render_mode` | У [shader_extensions]. Режим роботи PiP, підтримуються `balanced`, `performance`, `quality`. |
| `min_lens_factor`, `max_lens_factor`, `lens_factor_levels_count`, `lens_speed`, `lens_gyro_sound_period`, `force_zoom_sound` | Межі, кроки, швидкість і звук регулювання лінзи. `scope_lens_fov` має пріоритет у виборі явного FOV. Звуки `snd_scope_zoom_minus`, `snd_scope_zoom_plus`, `snd_scope_zoom_gyro` задавайте в секції зброї. |
| `scope_lens_effect`, `scope_nightvision`, `scope_alive_detector`, `scope_dynamic_zoom`, `scope_abberation` | Шейдерний профіль, нічний режим, виявлення цілей, регульований зум і аберація. `scope_abberation` пишеться саме так. |
| `scope_lens_eye_relief`, `scope_lens_reflection_strength`, `scope_lens_shadow_strength`, `scope_lens_white_key` | Налаштування проєкції та вигляду лінзи. Перенесіть разом із відповідним матеріалом OGF. |
| `min_night_brightness`, `max_night_brightness`, `steps_brightness`, `default_brightness_step`, `jitter_brightness` | Яскравість, число кроків, початковий крок і шум. Звуки `snd_scope_brightness_minus` та `snd_scope_brightness_plus` — у зброї. |
| `zoom_dof`, `disable_zoom_dof` | Загальний профіль near/focus/far та вимкнення для зброї. |
| `zoom_dof_<near/focus/far>`, `gl_dof_<near/focus/far>`, `alter_zoom_dof_<near/focus/far>`, `lens_dof_<near/focus/far>` | Профілі HUD для ADS, підствольника, альтернативного ADS і лінзи. Лінзовий профіль може читатися з оптики. |
| `zoom_in_dof_speed`, `gl_in_dof_speed`, `alter_zoom_in_dof_speed`, `zoom_out_dof_speed` | Швидкість входу та згасання DoF. |
| `zoom_hide_crosshair`, `zoom_hide_ui` | Приховання хрестика та індикаторів. Перший ключ — у HUD, другий — у зброї й застосовується для постійної донорської 3D-лінзи в ADS. |

### Сумісність і видимість

- У зброї, wrapper або секції обвісу перевіряйте `restricted_scope_and_sil`, `restricted_scope_and_gl`, `restricted_gl_and_sil`. Пара перевіряється в обидві сторони. Конфліктний обвіс гравець знімає вручну.
- `prohibit_aim_for_grenade_mode` обмежує ADS з активним підствольником. `banned_upgrades` у секції зброї — список заборонених префіксів апгрейдів.
- `bones`, `overriding_show_bones`, `overriding_hide_bones` налаштовують кістки підключеного прицілу. `no_scope_overriding_show_bones`, `no_scope_overriding_hide_bones` — правила зброї без прицілу. Для підствольника у зброї діють `def_hide_bones_override_when_gl_attached` та `def_hide_bones_grenade`.
- Ефект апгрейду може додавати `show_bones`, `hide_bones`, `hide_bones_override`, `hide_bones_override_when_silencer_attached`, `hide_bones_override_when_scope_attached`, `hide_bones_override_when_gl_attached`, `hide_bones_override_grenade`. Назви кісток повинні існувати в потрібній моделі.
- Для заміни HUD використовуйте `hud` в ефекті. Умовні пари в цій же секції — `hud_when_silencer_is_attached` + `hud_silencer`, `hud_when_scope_is_attached` + `hud_scope`, `hud_when_gl_is_attached` + `hud_gl`. Перший ключ пари читається як bool, другий — як ім’я секції. Пріоритет умов: глушник, знімний підствольник, приціл. Значення `skip_reassign` пропускає заміну. Новий HUD потребує `item_visual` та того самого `attach_place_idx`.
- Для шару іконки зброї використовуйте `element_icon_N`, `element_icon_offset_x_N`, `element_icon_offset_y_N`, `element_icon_always_front_N`, `element_icon_banned_addons_mask_N`, `element_icon_hide_N`. У секції шару задаються `inv_grid_x`, `inv_grid_y`, `inv_grid_width`, `inv_grid_height`, за потреби `icons_texture`.
- В ефекті апгрейду накладки мають ключі `upgrade_addon_icon`, `upgrade_addon_icons_hide`, `upgrade_addon_icon_offset_x`, `upgrade_addon_icon_offset_y`, `upgrade_addon_always_front`, `upgrade_addon_icon_banned_addons_mask`, `inv_addons_correction_x`, `inv_addons_correction_y`. `inv_grid_*` в ефекті додає поправки до прямокутника іконки. Для іконок дерева модернізації є `upgr_icons_texture` та `upgrade_icon_texture`.

## Детектори, ліхтарі та ЛЦВ

### Детектор і ручний ліхтар

- `supports_detector` читається з секції предмета й ефектів апгрейду. `false` явно забороняє сумісність, `true` дозволяє. Без ключа використовується базове правило предмета. Детектор може підібрати сумісну зброю з іншого слота. `detects_zones` у секції детектора керує його участю в HUD-індикації зон.
- У HUD зброї `use_prepare_detector_anim`, `use_finish_detector_anim` вмикають окремі рухи. Аліаси `anm_prepare_detector*`, `anm_draw_detector*`, `anm_finish_detector*` і `anm_reload*_detector` мають бути сумісними з анімаціями лівої руки. `use_jammed_detector_snd` та `use_jammed_last_detector_snd` дозволяють відповідні звукові варіанти. `immediate_unhide_<anm>` задається для конкретного руху.
- Ручний ліхтар читає нативні `light_bone`, `light_dir_bone`, `light_texture`, `light_offset`, `light_orientation`, `light_color`, `light_range` / `light_radius`, `light_cone_angle`, `light_shadow`, `light_hud_mode`, `light_start_time`, `light_type`, `light_particles`, `light_particles_bone`, `light_particles_offset`, `light_particles_orientation`. Нативні `light_*` шукаються спершу в предметі, потім у HUD. `light_start_time` у мс. Для донорського ліхтаря з `torch_installed` доступні `torch_*` нижче і `torch_dir_bone`, `torch_aim_attach_offset_<x/y/z>`, `torch_enable_time_<anm>`, `torch_disable_time_<anm>`.

### Тактичний ліхтар і лазер

| Де | Параметри |
|---|---|
| Зброя або ефект апгрейду | `torch_installed = true`, `laser_installed = true`. Решта ключів ефекту задаються в секції, яка встановлює відповідний обвіс. |
| Пози ліхтаря | `torch_light_bone`, `light_directions_by_bones`, `light_dir_bone`, `torch_cone_bones`. Вектори по компонентах `_x`, `_y`, `_z` читаються для `torch_attach_offset`, `torch_world_attach_offset`, `torch_omni_attach_offset`, `torch_omni_world_attach_offset`. |
| Світло і glow | `torch_spot_texture`, `torch_spot_angle`, `torch_render_type`, `torch_render_shadow`, `torch_omni_type`, `torch_omni_shadow`, `torch_range`, `torch_r2_range`, `torch_omni_range`, `torch_r2_omni_range`. Кольори `torch_color`, `torch_r2_color`, `torch_omni_color`, `torch_r2_omni_color` задаються окремими компонентами `_r/_g/_b/_a`. `create_glow`, `torch_glow_texture`, `torch_glow_radius` — ореол. `torch_breaking_params` — профіль відмов від зносу. |
| Лазер | `laserdot_attach_bone`, `laserdot_attach_offset_<x/y/z>`, `laserdot_world_attach_offset_<x/y/z>`, `laser_ray_bones`. У HUD `disable_laserdot_when_gl_enabled`, `disable_laserray_when_gl_enabled`. |
| Партикли ЛЦВ | `particles_count`, `laserdot_particle_N`, `laserdot_dist_N`. Починайте з `N = 0`. Імена партиклів повинні збігатися з іменами ресурсів збірки. |
| Розмір точки на дистанції | `laserdot_dists_count` і пари `laserdot_dist_treshold_N`, `laserdot_dist_scale_N` від `N = 1`. `treshold` — донорське написання. Межі подавайте в порядку зростання. Крива впливає на розмір екранної точки. Центр визначає траса ЛЦВ. |
| Анімації й звук | `anm_torch_on/off`, `anm_laser_on/off` — окремі аліаси `_on` та `_off` у HUD. `snd_torch_on/off`, `snd_laser_on/off` — відповідні звуки. Час перемикання можна задавати `lock_time_start_<anm>` / `lock_time_end_<anm>`. |
| Відмови | `laser_breaking_params` і `laser_problems_level` в секції лазера. Якщо електроніка гасить ЛЦВ, він припиняє поправку напрямку балістики від стегна. `collimator_sights_bones`, `collimator_breaking_params`, `collimator_problems_level`, `hide_collimator_sights_in_alter_zoom` у зброї керують коліматором на кшталт P90. |

### Екіпірування, швидкий удар і багнет

- Для налобного ліхтаря/ПНБ у зброї або HUD перевірте `disable_headlamp_anim`, `disable_nv_anim`, кліпи `anm_headlamp_on`, `anm_headlamp_off`, `anm_nv_on`, `anm_nv_off`. У [gunslinger_base] `headlamp_enable_animator`, `headlamp_disable_animator`, `nv_enable_animator`, `nv_disable_animator` посилаються на предмети з `action_animator = true` та `hud`. Рушій також використовує існуючі [items_animations] `enable_headlamp_animation`, `enable_night_vision_animation`, `headlamp_hud`, `night_vision_hud` і окремі `*_on_hud` / `*_off_hud`.
- Для HUD-предметів і аніматорів з дією на мітці звірте `action_timing` / `timing` у мс і донорський `mark_anm_show` у секундах. Під час перевірки переривання лікування бюрером рушій визначає лікувальні предмети, зокрема, через додатні `eat_health`, `wounds_heal_perc`, `boost_health_restore`, `boost_bleeding_restore`.
- [items_animations] `enable_quick_kick`, `quick_kick_hud` (застарілий alias `quick_knife_hud`). У HUD швидкого ножа `quick_kick_use_knife_pose` типово `true`. У секції ножа `snd_kick_1` або наявний звук удару.
- Багнет робиться ефектом апгрейду з `kick_hit_power`, `kick_distance` та рухом `anm_kick` у HUD. Додатково `kick_hit_impulse`, `kick_hit_type`, `kick_hit_count`, `kick_ap`, `kick_wallmark_size`, `kick_material`, `kick_disp_ver`, `kick_disp_hor`, `snd_kick`. `lock_time_start_anm_kick` прив’язує удар до кадру.
- Вимикання багнета — `disable_kick_anim`, `disable_kick_anim_when_scope_attached`, `disable_kick_anim_when_sil_attached`, `disable_kick_anim_when_gl_attached`, `disable_kick_anim_when_gl_enabled`. Задаються залежно від зброї та ефекту. Якщо дія недоступна, швидкий удар може скористатися ножем зі слота.

## Гранати, підствольники та РПГ-7

| Ключі | Секція й ефект |
|---|---|
| `enable_quick_throw_grenades`, `supports_quick_throw` | Глобальний дозвіл у [items_animations] і дозвіл конкретної гранати. За `supports_quick_throw = false` цей предмет не запускає швидкий кидок. Для HUD передбачені `anm_throw_quick` або `anm_throw_begin` → `anm_throw`. |
| `checkout_bones` | У гранаті. Кістки чеки в моделі, які оновлюються при зміні стану. |
| `throw_point_bone`, `throw_point`, `third_person_throw_point_offset` | Точка випуску гранати/болта. Параметри секції предмета й HUD. Кістка і `throw_point` потрібні для узгодження моменту випуску з рукою. `third_person_throw_point_offset` — зміщення від третьої особи. |
| `explosion_on_hit`, `explosion_hit_types`, `explosive_while_not_activated` | У гранаті. Детонація від пошкодження, перелік прийнятних типів удару та дозвіл для неактивованої гранати. Наявність `explosion_on_hit` явно вибирає гілку цього правила. |
| `explosion_on_kick`, `delay_time`, `safe_time`, `min_explosion_speed`, `deactivate_on_minimal_speed_contact` | Контактний підрив. Часи в мс, мінімальна швидкість у м/с. Для РГН/РГО звірте затримку `500` і поріг `3` у донорському профілі. |
| `rocket_class`, `fake_grenade_name` | Зв’язок зі снарядом у зброї та набої. Потрібні валідні секції з моделями/класами, особливо при зміні типу гранати чи завантаженні сейву. |
| `snd_load_grenade`, `snd_change_grenade`, `snd_switch` | У зброї. Заряджання, зміна типу гранати, перемикання режиму. |
| `anm_switch` / `anm_switch_g`, `anm_reload_g`, `anm_reload*_w_gl`, `anm_shots_g`, `anm_shots_w_gl`, `anm_idle_g_aim`, `anm_idle_w_gl_aim` | Аліаси HUD підствольника. `_g` — режим гранатомета, `_w_gl` — гвинтівка з приєднаним підствольником. Перевіряйте обидва режими з порожньою і зарядженою зброєю. |
| `controller_can_shoot_gl`, `controller_shoot_gl_min_dist` | У HUD, дозвіл і мінімальна дистанція примусового пострілу контролером. Типово `false` і `10` м. Залежить від [gunslinger_ie] `controller_weapon_attack`. |

**РПГ-7.** У секції зброї `rocket_misfunc_start_condition`, `rocket_misfunc_end_condition`, `rocket_misfunc_start_probability`, `rocket_misfunc_end_probability` задають імовірність несправного запуску за зносом. За несправності заряд витрачається і вибухає біля установки. Перевіряйте також загальний поріг електронних відмов.

Реактивний струмінь задають `reactive_hit_dist`, `reactive_hit_power`, `reactive_hit_impulse`, `reactive_hit_buck`, `reactive_hit_buck_disp`, `reactive_hit_type` (числовий тип), `reactive_hit_bullet_material`. Для відбиття — `reactive_hit_reverse_buck`, `reactive_hit_reverse_disp`, `reactive_hit_reverse_disp2`, `reactive_hit_reverse_power`, `reactive_hit_reverse_k`. Нульова дистанція/потужність залишає струмінь неактивним. Потрібна перевірка з відкритим простором та зі стіною позаду.

## Гаусс, вогнемет і електроніка

### Гаусс і його дисплей

- `recharge_time` у зброї — час підзаряджання. Ефект апгрейду може додати поправку. `max_queue_size` працює і для Гаусса. Перевіряйте готовність після виходу з ADS та зміну режиму під час підзаряджання.
- `autoaim_time`, `autoaim_modes`, `autoaim_only_alive`, `autoaim_ignore_dead`, `autoaim_shot_cancellation`, `autoaim_shot_after_key_released` — параметри автоспуску в секції зброї або ефекті. Разом із `fire_modes` відтворюють режими донорських апгрейдів. Не переносіть режим `A` окремо від відповідних кліпів і масок.
- Розкладений бортовий комп’ютер використовує маску `_auto` і відповідні аліаси спокою, спринту, прицілювання та пострілу. Переходи селектора задаються через `anm_changefiremode_from_1_to_a` / `anm_changefiremode_from_a_to_1` із кліпами на кшталт `gauss_mui_on` / `gauss_mui_off`.
- Комплект матеріалів OGF має включати `models\compscreen`, `models\energy_gauss` і потрібні шари `models\gauss_display_*`. На R2–R4 дисплей і шкала прив’язані до кількості набоїв, стану та фактичного часу підзаряджання. На R1 використовується спрощений статичний матеріал. Для старої моделі з кісткою `mui_amount_12` і `configuration_12` є fallback видимих кісток при більшій кількості набоїв. Перевіряйте обидва підходи на потрібних рендерах.
- `actor_can_shoot = false` у секції квестового Гаусса блокує спуск гравця, включно з відкладеним пострілом. Типове значення — `true`.

### Викид і несправності

| Ключі | Ефект |
|---|---|
| `misfire_after_problems_level` | У зброї або ефекті апгрейду. Типовий поріг `10`. `0` вимикає цю відмову. |
| `laser_problems_level`, `collimator_problems_level` | Пороги згасання відповідних обвісів. Параметри від зносу `laser_breaking_params`, `collimator_breaking_params` задаються незалежно. |
| `play_blowout_anim`, `blowout_anim_level`, `anm_blowout`, `anm_hide_emerg`, `snd_hide_emerg` | HUD відкритого КПК/інвентарю. Типовий поріг `4.5`. Екстрене закриття використовує відповідний кліп і звук за наявності. |
| `prohibit_suicide`, `suicide_by_animation`, `custom_suicide_shot`, `suicide_delay` | HUD зброї для сценарію контролера. `suicide_by_animation = true` і доступний `anm_suicide` потрібні для захоплення. `anm_stop_suicide` повертає зброю після скасування, `anm_shoot_suicide` та варіанти `_sil`, `_last_suicide` задають постріл. |

### Вогнемет і горіння

Вогнемет використовує `cyclic_shoot_animations`, `cyclic_shoot_start_animation`, `cyclic_shoot_sound`, початковий кліп `anm_shoot_start` і основний цикл. Пальне налаштовуйте в [ammo_*] через `hit_type` і матеріал `material`, а візуальний ефект — окремими ресурсами зброї.

У [gunslinger_base] `enable_actor_burning` (типово `false`) вмикає тривале горіння. `actor_burn_restore_speed` задає природне згасання, `burn_restore_materials` — список матеріалів для прискореного згасання, `burn_restore_material_speed` — добавку швидкості. Коефіцієнти згасання в цих LTX читаються на мілісекунду. Знос спорядження враховує `burn_immunity` костюма та шолома.

`burn_animator` у [gunslinger_base] посилається на предмет анімації гасіння. В його секції `burn_restore`, `hud`, `flame_particles`. У HUD ефект полум’я може слідувати за `fire_bone` та `fire_point`. Відсутність партиклу допускає відпрацювання самої дії.

## Хоткеї, Lua та перевірка збірки

| Дія | Ім’я прив’язки |
|---|---|
| Огляд зброї | `wpn_inspect` |
| Альтернативний ADS | `wpn_zoom_alter` |
| Тактичний ліхтар | `wpn_torch` |
| ЛЦВ | `wpn_laser` |
| Яскравість оптики | `scope_brightness_plus`, `scope_brightness_minus` |
| Швидка граната, швидкий удар | `quick_grenade`, `quick_kick` |

Прив’язки доступні через налаштування керування за наявності відповідних записів `ui_keybinding.xml` і локалізації. У консолі синтаксис `bind wpn_laser kL` прив’язує конкретну клавішу. Розряджання магазину викликається з інвентарю. Lua Workbench доданий у цій гілці як окремий інструмент рушія.

| Lua / callback | Призначення |
|---|---|
| `level.electronics_break()`, `level.electronics_restore()` | Збільшити або зменшити цільовий рівень перешкод на крок. |
| `level.electronics_reset()`, `level.electronics_apply()` | Скинути перешкоди або одразу застосувати цільове значення. |
| `level.is_ui_shown()`, `level.indicators_shown()` | Запити стану індикації гри та того, чи поточний HUD/зброя дозволяють індикатори. |
| `on_actor_boltthrow` | Lua-колбек випуску болта зі штатного кидка. |
| `inventory_upgrades.gunsl_can_disassemble_item`, `inventory_upgrades.gunsl_question_disassemble_item`, `inventory_upgrades.gunsl_effect_disassemble` | Скриптові перевірка, текст підтвердження та ефект розбирання у техніка. Для порту потрібні відповідні функції та локалізація. |

### Коротка матриця перевірки

1. **Стара зброя.** Чистий сейв і старий сейв. Огляд, постріл, перезарядка, слот, відсутність кліпів і апгрейдів GUNSLINGER.
2. **Перенесена зброя.** Повний/порожній магазин, патронник, останній постріл, осічка, клин, усунення клину, заряджання з детектором, зміна типу боєприпасу.
3. **Збірки HUD.** Стандартні й широкоформатні пози. Основний ADS, альтернативний ADS, пряме перемикання, нахил камери на нерухомій зброї. Повторіть із незалежним HUD після апгрейду.
4. **Оптика та обвіси.** R1 fallback та потрібні R2–R4, коліматор, лінія і точка ЛЦВ, PiP із SSS, зміна кратності, яскравості й стану електроніки. Перевірте дозволені й заборонені пари обвісів та накладки іконок.
5. **Контролер і мутанти.** Почніть з усіх перемикачів увімкненими. У тій самій ситуації перевірте кожен окремий `false`, далі спільний `false` з індивідуальними `true`. Для бюрера перевірте гравітаційну хвилю, базовий удар по витривалості, щит і телекінез. Для кабана та псевдогіганта перевірте звичайну шкоду поруч із вимкненим додатковим роззброєнням. Контролер має повернутися до базової псиатаки.
6. **Гранати й важка зброя.** Швидкий, слабкий і звичайний кидок, контакт після захисного часу, кидок болта, РПГ біля стіни, підзаряджання та дисплей Гаусса, вогнемет і гасіння на потрібних матеріалах.

### Де перевіряти реалізацію

| Якщо треба уточнити точний порядок fallback | Файли у `code/engine/xrGame` |
|---|---|
| Зброя, HUD, оптика, видимість і пристрої | `Weapon.cpp`, `WeaponMagazined.cpp`, `player_hud.cpp`, `WeaponUpgrade.cpp` |
| Кидок, балістика, важка зброя | `Missile.cpp`, `Grenade.cpp`, `Bolt.cpp`, `WeaponFire.cpp`, `WeaponAmmo.cpp`, `WeaponRPG7.cpp`, `WeaponMagazinedWGrenade.cpp` |
| Дробовики та детектори | `WeaponAutomaticShotgun.cpp`, `weaponBM16.cpp`, `CustomDetector.cpp`, `Flashlight.cpp` |
| Реакції й електроніка | `Actor.cpp`, `ItemUseController.cpp`, `Level.cpp`, `ai/monsters/burer/burer.cpp`, `ai/monsters/controller/controller_psy_hit.cpp` |
| Модернізація й Lua | `inventory_item_upgrade.cpp`, `ui/UICellCustomItems.cpp`, `ui/UIActorMenu_script.cpp`, `level_script.cpp` |
