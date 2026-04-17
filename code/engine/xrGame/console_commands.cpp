#include "pch_script.h"
// --- Engine & Core ---
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "../xrEngine/customhud.h"
#include "../xrEngine/fdemorecord.h"
#include "../xrEngine/fdemoplay.h"
#include "../xrEngine/cameraBase.h"
#include "xrMessages.h"
#include "xrserver.h"
#include "date_time.h"
#include "mt_config.h"
// --- Game & Actor ---
#include "level.h"
#include "actor.h"
#include "Actor_Flags.h"
#include "game_cl_base.h"
#include "game_cl_single.h"
#include "game_sv_single.h"
#include "hit.h"
#include "attachable_item.h"
#include "attachment_owner.h"
#include "customzone.h"
#include "inventory.h"
#include "inventoryowner.h"
#include "infoportion.h"
#include "inventory_upgrade_manager.h"
#include "character_hit_animations_params.h"
// --- Save & Load ---
#include "saved_game_wrapper.h"
#include "autosave_manager.h"
// --- ALife & Scripts ---
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "script_engine.h"
#include "script_engine_space.h"
#include "script_process.h"
#include "script_debugger.h"
#include "xrServer_Objects.h"
#include "xrserver_objects_alife_items.h"
#include "string_table.h"
#include "GameTask.h"
#include "map_manager.h"
// --- AI ---
#include "ai_space.h"
#include "ai_debug.h"
#include "ai_debug_variables.h"
#include "ai/monsters/BaseMonster/base_monster.h"
#include "level_graph.h"
#include "game_graph.h"
#include "ai_object_location.h"
// --- UI ---
#include "UIGameSP.h"
#include "UIGameCustom.h"
#include "ui/UIMainIngameWnd.h"
#include "ui/UIActorMenu.h"
#include "ui/UIStatic.h"
#include "MainMenu.h"
// --- Physics ---
#include "PHDestroyable.h"
#include "../xrphysics/iphworld.h"
#include "../xrphysics/console_vars.h"
#include "zone_effector.h"
#include "cameralook.h"
#include "attachable_item.h"
#include "attachment_owner.h"
// --- Debug ---
#ifdef DEBUG
#include "PHDebug.h"
#include "ui/UIDebugFonts.h"
#include "CharacterPhysicsSupport.h"
#include "alife_graph_registry.h"
#endif

string_path g_last_saved_game;

// Engine / System
extern u64 g_qwStartGameTime;
extern u64 g_qwEStartGameTime;
ENGINE_API extern float psHUD_FOV;
extern float psSqueezeVelocity;
extern int psLUA_GCSTEP;
ENGINE_API extern float g_console_sensitive;
extern float g_fTimeFactor;
BOOL g_bCheckTime = FALSE;

// Game & Actor
extern ESingleGameDifficulty g_SingleGameDifficulty;
extern BOOL b_toggle_weapon_aim;
extern float g_smart_cover_factor;
extern int g_upgrades_log;
extern float g_smart_cover_animation_speed_factor;
int g_keypress_on_start = 1;

// AI
extern BOOL g_ai_use_old_vision;
float g_aim_predict_time = 0.44f;
int g_AI_inactive_time = 0;

// Network
extern int x_m_x;
extern int x_m_z;
extern BOOL net_cl_inputguaranteed;
extern BOOL net_sv_control_hit;
extern int g_dwInputUpdateDelta;
int net_cl_inputupdaterate = 50;

// Flags
Flags32 g_mt_config = { mtLevelPath | mtDetailPath | mtObjectHandler | mtSoundPlayer | mtAiVision |
                        mtBullets | mtLUA_GC | mtLevelSounds | mtALife | mtMap };
Flags32 g_uCommonFlags;
enum E_COMMON_FLAGS { flAiUseTorchDynamicLights = 1 };

#ifdef DEBUG
extern float air_resistance_epsilon;
extern BOOL g_ShowAnimationInfo;
extern BOOL g_bShowHitSectors;
extern BOOL g_show_wnd_rect2;
extern BOOL g_bDrawBulletHit;
extern BOOL g_bDrawFirstBulletCrosshair;
extern LPSTR dbg_stalker_death_anim;
extern BOOL b_death_anim_velocity;
extern BOOL death_anim_debug;
extern BOOL dbg_imotion_draw_skeleton;
extern BOOL dbg_imotion_draw_velocity;
extern BOOL dbg_imotion_collide_debug;
extern float dbg_imotion_draw_velocity_scale;
float debug_on_frame_gather_stats_frequency = 0.f;

BOOL g_bDebugNode = FALSE;
u32 g_dwDebugNodeSource = 0;
u32 g_dwDebugNodeDest = 0;
Flags32 dbg_net_Draw_Flags = { 0 };
Flags32 dbg_track_obj_flags;
extern float dbg_text_height_scale;
#endif

// =========================================================================
// UTILS & MEMORY
// =========================================================================

typedef void (*full_memory_stats_callback_type)();
XRCORE_API full_memory_stats_callback_type g_full_memory_stats_callback;

static void full_memory_stats() {
    Memory.mem_compact();
    u32 _process_heap = mem_usage_impl(nullptr, nullptr);
    u32 _render = ::Render->memory_usage();
    int _eco_strings = (int)g_pStringContainer->stat_economy();
    int _eco_smem = (int)g_pSharedMemoryContainer->stat_economy();
    u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;

    if (Device.m_pRender)
        Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

    log_vminfo();
    Msg("* [ D3D ]: textures[%d K]", (m_base + m_lmaps) / 1024);
    Msg("* [x-ray]: process heap[%d K], render[%d K]", _process_heap / 1024, _render / 1024);
    Msg("* [x-ray]: economy: strings[%d K], smem[%d K]", _eco_strings / 1024, _eco_smem);

#ifdef FS_DEBUG
    Msg("* [x-ray]: file mapping: memory[%d K], count[%d]", g_file_mapped_memory / 1024,
        g_file_mapped_count);
    dump_file_mappings();
#endif
}

class CCC_MemStats : public IConsole_Command {
public:
    CCC_MemStats(LPCSTR N) : IConsole_Command(N) {
        bEmptyArgsHandled = TRUE;
        g_full_memory_stats_callback = &full_memory_stats;
    }
    virtual void Execute(LPCSTR args) { full_memory_stats(); }
};

#ifdef DEBUG
class CCC_MemCheckpoint : public IConsole_Command {
public:
    CCC_MemCheckpoint(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = FALSE; }
    virtual void Execute(LPCSTR args) { memory_monitor::make_checkpoint(args); }
    virtual void Save(IWriter* F) {}
};
#endif

bool valid_saved_game_name(LPCSTR file_name) {
    LPCSTR I = file_name, E = file_name + xr_strlen(file_name);
    for (; I != E; ++I) {
        if (!strchr("/\\:*?\"<>|^()[]%", *I))
            continue;
        return false;
    }
    return true;
}

void get_files_list(xr_vector<std::string>& files, LPCSTR dir, LPCSTR file_ext) {
    VERIFY(dir && file_ext);
    files.clear();

    FS_Path* P = FS.get_path(dir);
    P->m_Flags.set(FS_Path::flNeedRescan, TRUE);
    FS.m_Flags.set(CLocatorAPI::flNeedCheck, TRUE);
    FS.rescan_pathes();

    LPCSTR fext;
    STRCONCAT(fext, "*", file_ext);
    FS_FileSet files_set;
    FS.file_list(files_set, dir, FS_ListFiles, fext);
    u32 len_str_ext = xr_strlen(file_ext);

    for (auto itb = files_set.begin(); itb != files_set.end(); ++itb) {
        LPCSTR fn_ext = (*itb).name.c_str();
        string_path fn;
        strncpy_s(fn, sizeof(fn), fn_ext, xr_strlen(fn_ext) - len_str_ext);
        files.push_back(fn);
    }
    FS.m_Flags.set(CLocatorAPI::flNeedCheck, FALSE);
}

// =========================================================================
// DEV MODE COMMANDS
// =========================================================================

bool is_dev_mode() {
    return (strstr(Core.Params, "-dev_mode") != nullptr ||
            strstr(Core.Params, "-developer_mode") != nullptr);
}

class CCC_Mask_Dev : public CCC_Mask {
public:
    CCC_Mask_Dev(LPCSTR N, Flags32* V, u32 M) : CCC_Mask(N, V, M) {}
    virtual void Execute(LPCSTR args) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        CCC_Mask::Execute(args);
    }
};

// g_spawn_to_inventory [section] [count]
class CCC_SpawnToInventory : public IConsole_Command {
public:
    CCC_SpawnToInventory(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; }
    virtual void Execute(LPCSTR args) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        if (!g_pGameLevel || !Actor())
            return;

        char section[256];
        int count = 1;
        if (sscanf(args, "%s %d", section, &count) < 1) {
            Msg("! Usage: g_spawn_to_inventory <section> [count]");
            return;
        }
        if (!pSettings->section_exist(section)) {
            Msg("! Section [%s] doesn't exist!", section);
            return;
        }

        if (count > 250) {
            Msg("! [g_spawn_to_inventory]: Cancelled. Max count is 250.");
            count = 250;
        }

        for (int i = 0; i < count; ++i) {
            // Клієнтський метод, який гарантовано надсилає пакет про підняття предмета (без 'true'
            // в кінці!)
            Level().spawn_item(section, Actor()->Position(),
                               Actor()->ai_location().level_vertex_id(), Actor()->ID());
        }
        Msg("~ Spawned %d of [%s] to inventory.", count, section);
    }
};

// g_spawn [section] [count]
class CCC_SpawnToCrosshair : public IConsole_Command {
public:
    CCC_SpawnToCrosshair(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; }
    virtual void Execute(LPCSTR args) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        if (!g_pGameLevel || !Actor())
            return;

        char section[256];
        int count = 1;
        if (sscanf(args, "%s %d", section, &count) < 1) {
            Msg("! Usage: g_spawn <section> [count]");
            return;
        }
        if (!pSettings->section_exist(section)) {
            Msg("! Section [%s] doesn't exist!", section);
            return;
        }

        if (count > 250) {
            Msg("! [g_spawn]: Cancelled. Max count is 250.");
            count = 250;
        }

        Fvector p = Device.vCameraPosition;
        Fvector d = Device.vCameraDirection;
        collide::rq_result RQ;
        Fvector spawn_pos = Actor()->Position();

        if (Level().ObjectSpace.RayPick(p, d, 50.f, collide::rqtBoth, RQ, Actor())) {
            spawn_pos.mad(p, d, RQ.range);
            spawn_pos.y += 0.2f;
        } else {
            spawn_pos.mad(p, d, 3.0f);
        }

        u32 lvid = ai().level_graph().vertex_id(spawn_pos);
        if (!ai().level_graph().valid_vertex_id(lvid)) {
            lvid = Actor()->ai_location().level_vertex_id();
        }

        // Отримуємо правильний доступ до серверного ALife
        game_sv_Single* tpGame = smart_cast<game_sv_Single*>(Level().Server->game);
        if (tpGame) {
            for (int i = 0; i < count; ++i) {
                // 0xffff (або ALife::_OBJECT_ID(-1)) - це спавн на землю
                tpGame->alife().spawn_item(section, spawn_pos, lvid,
                                           Actor()->ai_location().game_vertex_id(), 0xffff);
            }
            Msg("~ Spawned %d of [%s] at crosshair.", count, section);
        }
    }
};

class CCC_InfoPortion : public IConsole_Command {
    bool bGive;

public:
    CCC_InfoPortion(LPCSTR N, bool give) : IConsole_Command(N), bGive(give) {
        bEmptyArgsHandled = false;
    }
    virtual void Execute(LPCSTR args) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        if (!g_pGameLevel || !Actor())
            return;

        Actor()->TransferInfo(shared_str(args), bGive);
        Msg("~ Info portion [%s] %s.", args, bGive ? "GIVEN" : "DISABLED");
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        if (is_dev_mode())
            tips.push_back("Enter info_portion name (e.g. zat_b14_stalkers_dead)");
    }
};

class CCC_JumpToLevelDev : public IConsole_Command {
public:
    CCC_JumpToLevelDev(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; }
    virtual void Execute(LPCSTR level) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        if (!ai().get_alife()) {
            Msg("! ALife simulator is needed!");
            return;
        }

        for (const auto& I : ai().game_graph().header().levels()) {
            if (I.second.name() == level) {
                ai().alife().jump_to_level(level);
                return;
            }
        }
        Msg("! There is no level \"%s\" in the game graph!", level);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        if (!is_dev_mode() || !ai().get_alife())
            return;
        for (const auto& level : ai().game_graph().header().levels())
            tips.push_back(std::string(level.second.name()));
    }
};

// =========================================================================
// GAME & ALIFE COMMANDS
// =========================================================================

class CCC_GameDifficulty : public CCC_Token {
public:
    CCC_GameDifficulty(LPCSTR N)
        : CCC_Token(N, (u32*)&g_SingleGameDifficulty, difficulty_type_token){};
    virtual void Execute(LPCSTR args) {
        CCC_Token::Execute(args);
        if (g_pGameLevel && Level().game) {
            if (GameID() != eGameIDSingle) {
                Msg("For this game type difficulty level is disabled.");
                return;
            }
            game_cl_Single* game = smart_cast<game_cl_Single*>(Level().game);
            VERIFY(game);
            game->OnDifficultyChanged();
        }
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "game difficulty"); }
};

class CCC_TimeFactorDev : public IConsole_Command {
public:
    CCC_TimeFactorDev(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; }

    virtual void Execute(LPCSTR args) {
        if (!is_dev_mode()) {
            Msg("! Command available only in -dev_mode");
            return;
        }
        if (!g_pGameLevel)
            return;

        float tf = 10.0f; // 10.0 - стандартное время в сталкере
        if (sscanf(args, "%f", &tf) < 1) {
            Msg("! Usage: time_factor <value> (default is 10.0)");
            return;
        }

        // Защита от краша при вводе гигантских чисел
        clamp(tf, 1.0f, 100000.f);

        // Эта функция ускоряет именно смену дня и ночи, погоду и A-Life
        Level().SetGameTimeFactor(tf);
        Msg("~ In-game time factor set to: %.2f", tf);
    }

    virtual void Status(TStatus& S) {
        if (g_pGameLevel)
            xr_sprintf(S, sizeof(S), "%.2f", Level().GetGameTimeFactor());
        else
            xr_strcpy(S, "10.00");
    }

    virtual void fill_tips(vecTips& tips, u32 mode) {
        if (!is_dev_mode())
            return;
        tips.push_back("10");   // Обычное
        tips.push_back("100");  // Быстрое
        tips.push_back("1000"); // Очень быстрое
    }
};

// =========================================================================
// SAVE & LOAD
// =========================================================================

class CCC_ALifeSave : public IConsole_Command {
public:
    CCC_ALifeSave(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args) {
        if (!IsGameTypeSingle()) {
            Msg("for single-mode only");
            return;
        }
        if (!g_actor || !Actor()->g_Alive()) {
            Msg("cannot make saved game because actor is dead :(");
            return;
        }

        Console->Execute("stat_memory");

        string_path S, S1;
        S[0] = 0;
        strncpy_s(S, sizeof(S), args, _MAX_PATH - 1);

        NET_Packet net_packet;
        net_packet.w_begin(M_SAVE_GAME);

        if (!xr_strlen(S)) {
            strconcat(sizeof(S), S, Core.UserName, " - ", "quicksave");
            net_packet.w_stringZ(S);
            net_packet.w_u8(0);
        } else {
            if (!valid_saved_game_name(S)) {
                Msg("! Save failed: invalid file name - %s", S);
                return;
            }
            net_packet.w_stringZ(S);
            net_packet.w_u8(1);
        }
        Level().Send(net_packet, net_flags(TRUE));

        SDrawStaticStruct* _s = CurrentGameUI()->AddCustomStatic("game_saved", true);
        LPSTR save_name;
        STRCONCAT(save_name, CStringTable().translate("st_game_saved").c_str(), ": ", S);
        _s->wnd()->TextItemControl()->SetText(save_name);

        xr_strcat(S, ".dds");
        FS.update_path(S1, "$game_saves$", S);
        MainMenu()->Screenshot(IRender_interface::SM_FOR_GAMESAVE, S1);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        get_files_list(tips, "$game_saves$", SAVE_EXTENSION);
    }
};

class CCC_ALifeLoadFrom : public IConsole_Command {
public:
    CCC_ALifeLoadFrom(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args) {
        string_path saved_game;
        strncpy_s(saved_game, sizeof(saved_game), args, _MAX_PATH - 1);

        if (!ai().get_alife()) {
            Log("! ALife simulator has not been started yet");
            return;
        }
        if (!xr_strlen(saved_game)) {
            Log("! Specify file name!");
            return;
        }
        if (!CSavedGameWrapper::saved_game_exist(saved_game)) {
            Msg("! Cannot find saved game %s", saved_game);
            return;
        }
        if (!CSavedGameWrapper::valid_saved_game(saved_game)) {
            Msg("! Cannot load saved game %s, version mismatch/corrupted", saved_game);
            return;
        }
        if (!valid_saved_game_name(saved_game)) {
            Msg("! Cannot load saved game %s, invalid file name", saved_game);
            return;
        }

        if (MainMenu()->IsActive())
            MainMenu()->Activate(false);
        Console->Execute("stat_memory");

        if (Device.Paused())
            Device.Pause(FALSE, TRUE, TRUE, "CCC_ALifeLoadFrom");

        NET_Packet net_packet;
        net_packet.w_begin(M_LOAD_GAME);
        net_packet.w_stringZ(saved_game);
        Level().Send(net_packet, net_flags(TRUE));
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        get_files_list(tips, "$game_saves$", SAVE_EXTENSION);
    }
};

class CCC_LoadLastSave : public IConsole_Command {
public:
    CCC_LoadLastSave(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args) {
        string_path saved_game = "";
        if (args)
            strncpy_s(saved_game, sizeof(saved_game), args, _MAX_PATH - 1);
        if (saved_game && *saved_game) {
            xr_strcpy(g_last_saved_game, saved_game);
            return;
        }
        if (!*g_last_saved_game) {
            Msg("! cannot load last saved game since it hasn't been specified");
            return;
        }
        if (!CSavedGameWrapper::saved_game_exist(g_last_saved_game)) {
            Msg("! Cannot find saved game %s", g_last_saved_game);
            return;
        }

        LPSTR command;
        if (ai().get_alife()) {
            STRCONCAT(command, "load ", g_last_saved_game);
            Console->Execute(command);
            return;
        }
        STRCONCAT(command, "start server(", g_last_saved_game, "/single/alife/load)");
        Console->Execute(command);
    }
    virtual void Save(IWriter* F) {
        if (*g_last_saved_game)
            F->w_printf("%s %s\r\n", cName, g_last_saved_game);
    }
};

// =========================================================================
// UTILITIES & DEMO
// =========================================================================

class CCC_FlushLog : public IConsole_Command {
public:
    CCC_FlushLog(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR) {
        FlushLog();
        Msg("* Log file has been saved successfully!");
    }
};

class CCC_ClearLog : public IConsole_Command {
public:
    CCC_ClearLog(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR) {
        LogFile->clear();
        FlushLog();
        Msg("* Log file has been cleaned successfully!");
    }
};

class CCC_DemoRecord : public IConsole_Command {
public:
    CCC_DemoRecord(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR args) {
        Console->Hide();
        LPSTR fn_;
        STRCONCAT(fn_, args, ".xrdemo");
        string_path fn;
        FS.update_path(fn, "$game_saves$", fn_);
        g_pGameLevel->Cameras().AddCamEffector(xr_new<CDemoRecord>(fn));
    }
};

class CCC_DemoPlay : public IConsole_Command {
public:
    CCC_DemoPlay(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR args) {
        if (0 == g_pGameLevel) {
            Msg("! There are no level(s) started");
            return;
        }
        Console->Hide();
        string_path fn;
        u32 loops = 0;
        LPSTR comma = strchr(const_cast<LPSTR>(args), ',');
        if (comma) {
            loops = atoi(comma + 1);
            *comma = 0;
        }
        strconcat(sizeof(fn), fn, args, ".xrdemo");
        FS.update_path(fn, "$game_saves$", fn);
        g_pGameLevel->Cameras().AddCamEffector(xr_new<CDemoPlay>(fn, 1.0f, loops));
    }
};

class CCC_MainMenu : public IConsole_Command {
public:
    CCC_MainMenu(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args) {
        bool bWhatToDo = TRUE;
        if (0 == xr_strlen(args))
            bWhatToDo = !MainMenu()->IsActive();
        if (EQ(args, "on") || EQ(args, "1"))
            bWhatToDo = TRUE;
        if (EQ(args, "off") || EQ(args, "0"))
            bWhatToDo = FALSE;
        MainMenu()->Activate(bWhatToDo);
    }
};

// =========================================================================
// PHYSICS & DEBUG COMMANDS
// =========================================================================

class CCC_PHFps : public IConsole_Command {
public:
    CCC_PHFps(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR args) {
        float step_count = (float)atof(args);
#ifndef DEBUG
        clamp(step_count, 50.f, 200.f);
#endif
        ph_console::ph_step_time = 1.f / step_count;
        if (physics_world())
            physics_world()->SetStep(ph_console::ph_step_time);
    }
    virtual void Status(TStatus& S) { xr_sprintf(S, "%3.5f", 1.f / ph_console::ph_step_time); }
};

class CCC_PHIterations : public CCC_Integer {
public:
    CCC_PHIterations(LPCSTR N) : CCC_Integer(N, &phIterations, 15, 50) {}
    virtual void Execute(LPCSTR args) {
        CCC_Integer::Execute(args);
        if (physics_world())
            physics_world()->StepNumIterations(phIterations);
    }
};

#ifdef DEBUG_CAPS
class CCC_PHGravity : public IConsole_Command {
public:
    CCC_PHGravity(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR args) {
        if (!physics_world())
            return;
        physics_world()->SetGravity(float(atof(args)));
    }
    virtual void Status(TStatus& S) {
        if (physics_world())
            xr_sprintf(S, "%3.5f", physics_world()->Gravity());
        else
            xr_sprintf(S, "%3.5f", default_world_gravity);
    }
};
#endif // DEBUG_CAPS

// =========================================================================
// REGISTRATION FUNCTION
// =========================================================================

void CCC_RegisterCommands() {
    // --- MEMORY & SYSTEM ---
    CMD1(CCC_MemStats, "stat_memory");
    CMD1(CCC_FlushLog, "flush");
    CMD1(CCC_ClearLog, "clear_log");
#ifdef DEBUG
    CMD1(CCC_MemCheckpoint, "stat_memory_checkpoint");
#endif

    // --- GAME & ALIFE ---
    CMD1(CCC_GameDifficulty, "g_game_difficulty");
    CMD4(CCC_Integer, "g_sleep_time", &psActorSleepTime, 1, 24);
    CMD1(CCC_TimeFactorDev, "time_factor");
    CMD4(CCC_Float, "con_sensitive", &g_console_sensitive, 0.01f, 1.0f);
    CMD4(CCC_Integer, "wpn_aim_toggle", &b_toggle_weapon_aim, 0, 1);
    CMD4(CCC_Integer, "keypress_on_start", &g_keypress_on_start, 0, 1);

    // --- SAVE & LOAD ---
    CMD1(CCC_ALifeSave, "save");
    CMD1(CCC_ALifeLoadFrom, "load");
    CMD1(CCC_LoadLastSave, "load_last_save");

    // --- HUD & UI ---
    CMD1(CCC_MainMenu, "main_menu");

    psHUD_Flags.set(HUD_CROSSHAIR, true);
    psHUD_Flags.set(HUD_WEAPON, true);
    psHUD_Flags.set(HUD_DRAW, true);
    psHUD_Flags.set(HUD_INFO, true);

    CMD3(CCC_Mask, "hud_weapon", &psHUD_Flags, HUD_WEAPON);
    CMD3(CCC_Mask, "hud_info", &psHUD_Flags, HUD_INFO);
    CMD3(CCC_Mask, "hud_draw", &psHUD_Flags, HUD_DRAW);
    CMD3(CCC_Mask, "hud_crosshair", &psHUD_Flags, HUD_CROSSHAIR);
    CMD3(CCC_Mask, "hud_crosshair_dist", &psHUD_Flags, HUD_CROSSHAIR_DIST);
    CMD3(CCC_Mask, "cl_dynamiccrosshair", &psHUD_Flags, HUD_CROSSHAIR_DYNAMIC);

    // Quick Slots
    CMD3(CCC_String, "slot_0", g_quick_use_slots[0], 32);
    CMD3(CCC_String, "slot_1", g_quick_use_slots[1], 32);
    CMD3(CCC_String, "slot_2", g_quick_use_slots[2], 32);
    CMD3(CCC_String, "slot_3", g_quick_use_slots[3], 32);

    // --- PLAYER FLAGS & MOVEMENT ---
    CMD3(CCC_Mask, "g_crouch_toggle", &psActorFlags, AF_CROUCH_TOGGLE);
    CMD3(CCC_Mask, "g_backrun", &psActorFlags, AF_RUN_BACKWARD);
    CMD3(CCC_Mask, "g_autopickup", &psActorFlags, AF_AUTOPICKUP);
    CMD3(CCC_Mask, "g_dynamic_music", &psActorFlags, AF_DYNAMIC_MUSIC);
    CMD3(CCC_Mask, "g_important_save", &psActorFlags, AF_IMPORTANT_SAVE);

    // --- DEV MODE COMMANDS (-dev_mode) ---
    CMD1(CCC_SpawnToInventory, "g_spawn_to_inventory");
    CMD1(CCC_SpawnToCrosshair, "g_spawn");
    CMD2(CCC_InfoPortion, "g_info", true);  // Використовуємо CMD2
    CMD2(CCC_InfoPortion, "d_info", false); // Використовуємо CMD2
    CMD1(CCC_JumpToLevelDev, "jump_to_level");

    extern Flags32 psActorFlags;
    CMD3(CCC_Mask_Dev, "g_god", &psActorFlags, AF_GODMODE);
    CMD3(CCC_Mask_Dev, "g_unlimitedammo", &psActorFlags, AF_UNLIMITEDAMMO);

    // --- PHYSICS & DEMO ---
    CMD1(CCC_PHFps, "ph_frequency");
    CMD1(CCC_PHIterations, "ph_iterations");
    CMD1(CCC_DemoPlay, "demo_play");
    CMD1(CCC_DemoRecord, "demo_record");

    // --- AI VISION / BALLISTICS ---
    CMD4(CCC_Integer, "ai_use_old_vision", &g_ai_use_old_vision, 0, 1);
    CMD4(CCC_Float, "ai_aim_predict_time", &g_aim_predict_time, 0.f, 10.f);
    extern float g_ai_aim_min_speed;
    CMD4(CCC_Float, "ai_aim_min_speed", &g_ai_aim_min_speed, 0.f, 10.f * PI);
    extern float g_ai_aim_min_angle;
    CMD4(CCC_Float, "ai_aim_min_angle", &g_ai_aim_min_angle, 0.f, 10.f * PI);
    extern float g_ai_aim_max_angle;
    CMD4(CCC_Float, "ai_aim_max_angle", &g_ai_aim_max_angle, 0.f, 10.f * PI);

#ifdef DEBUG_CAPS
    CMD4(CCC_Float, "hud_fov", &psHUD_FOV, 0.1f, 1.0f);
    extern float g_fov;
    CMD4(CCC_Float, "fov", &g_fov, 5.0f, 180.0f);
    CMD1(CCC_PHGravity, "ph_gravity");
#endif
}