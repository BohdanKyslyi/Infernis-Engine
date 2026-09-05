#include "pch_script.h"
#include "../xrEngine/fdemorecord.h"
#include "../xrEngine/fdemoplay.h"
#include "../xrEngine/environment.h"
#include "../xrEngine/igame_persistent.h"
#include "ParticlesObject.h"
#include "Level.h"
#include "hudmanager.h"
#include "xrServer.h"
#include "net_queue.h"
#include "game_cl_base.h"
#include "entity_alive.h"
#include "ai_space.h"
#include "ai_debug.h"
#include "ShootingObject.h"
#include "GameTaskManager.h"
#include "Level_Bullet_Manager.h"
#include "script_process.h"
#include "script_engine.h"
#include "script_engine_space.h"
#include "team_base_zone.h"
#include "infoportion.h"
#include "patrol_path_storage.h"
#include "date_time.h"
#include "space_restriction_manager.h"
#include "seniority_hierarchy_holder.h"
#include "space_restrictor.h"
#include "client_spawn_manager.h"
#include "autosave_manager.h"
#include "ClimableObject.h"
#include "level_graph.h"
#include "mt_config.h"
#include "phcommander.h"
#include "map_manager.h"
#include "../xrEngine/CameraManager.h"
#include "level_sounds.h"
#include "car.h"
#include "trade_parameters.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "MainMenu.h"
#include "../xrEngine/XR_IOConsole.h"
#include "actor.h"
#include "player_hud.h"
#include "UI/UIGameTutorial.h"
#include "message_filter.h"
#include "demoplay_control.h"
#include "demoinfo.h"
#include "CustomDetector.h"

#include "../xrphysics/iphworld.h"
#include "../xrphysics/console_vars.h"

#ifdef DEBUG
#include "level_debug.h"
#include "ai/stalker/ai_stalker.h"
#include "debug_renderer.h"
#include "physicobject.h"
#include "phdebug.h"
#include "debug_text_tree.h"
#endif

extern CUISequencer* g_tutorial;
extern CUISequencer* g_tutorial2;

float g_cl_lvInterp = 0.1f;
u32 lvInterpSteps = 0;
BOOL g_bDebugEvents = FALSE;

extern CAI_Space* g_ai_space;
int psLUA_GCSTEP = 10;

#ifdef DEBUG
extern Flags32 dbg_net_Draw_Flags;
#endif
extern void draw_wnds_rects();

//=============================================================================
// Construction/Destruction
//=============================================================================

CLevel::CLevel() : IPureClient(Device.GetTimerGlobal()) 
{
    g_bDebugEvents = strstr(Core.Params, "-debug_ge") ? TRUE : FALSE;

    Server = nullptr;
    game = nullptr;
    game_events = xr_new<NET_Queue_Event>();

    game_configured = FALSE;
    m_bGameConfigStarted = FALSE;
    m_connect_server_err = xrServer::ErrNoError;

    eChangeRP   = Engine.Event.Handler_Attach("LEVEL:ChangeRP", this);
    eDemoPlay   = Engine.Event.Handler_Attach("LEVEL:PlayDEMO", this);
    eChangeTrack= Engine.Event.Handler_Attach("LEVEL:PlayMusic", this);
    eEnvironment= Engine.Event.Handler_Attach("LEVEL:Environment", this);
    eEntitySpawn= Engine.Event.Handler_Attach("LEVEL:spawn", this);

    m_pBulletManager = xr_new<CBulletManager>();
    m_map_manager    = xr_new<CMapManager>();
    m_game_task_manager = xr_new<CGameTaskManager>();

    m_bNeed_CrPr = false;
    m_bIn_CrPr = false;
    m_dwNumSteps = 0;
    m_dwDeltaUpdate = u32(fixed_step * 1000);
    m_dwLastNetUpdateTime = 0;
    
    m_seniority_hierarchy_holder = xr_new<CSeniorityHierarchyHolder>();
    m_level_sound_manager = xr_new<CLevelSoundManager>();
    m_space_restriction_manager = xr_new<CSpaceRestrictionManager>();
    m_client_spawn_manager = xr_new<CClientSpawnManager>();
    m_autosave_manager = xr_new<CAutosaveManager>();

#ifdef DEBUG
    m_debug_renderer = xr_new<CDebugRenderer>();
    m_level_debug = xr_new<CLevelDebug>();
    m_bEnvPaused = false;
    m_bSynchronization = false;
#endif

    m_ph_commander = xr_new<CPHCommander>();
    m_ph_commander_scripts = xr_new<CPHCommander>();
    m_ph_commander_physics_worldstep = nullptr; 

    pStatGraphR = nullptr;
    pStatGraphS = nullptr;
    pCurrentControlEntity = nullptr;

    m_dwCL_PingLastSendTime = 0;
    m_dwCL_PingDeltaSend = 1000;
    m_dwRealPing = 0;

    m_writer = nullptr;
    m_reader = nullptr;
    
    m_DemoPlay = FALSE;
    m_DemoPlayStarted = FALSE;
    m_DemoPlayStoped = FALSE;
    m_DemoSave = FALSE;
    m_DemoSaveStarted = FALSE;
    
    m_current_spectator = nullptr;
    m_msg_filter = nullptr;
    m_demoplay_control = nullptr;
    m_demo_info = nullptr;

    R_ASSERT(nullptr == g_player_hud);
    g_player_hud = xr_new<player_hud>();
    g_player_hud->load_default();

    hud_zones_list = nullptr;

    Msg("%s", Core.Params);

    m_lzo_working_memory = nullptr;
    m_lzo_working_buffer = nullptr;
}

CLevel::~CLevel() 
{
    xr_delete(g_player_hud);
    delete_data(hud_zones_list);
    hud_zones_list = nullptr;

    Msg("- Destroying level");

    Engine.Event.Handler_Detach(eEntitySpawn, this);
    Engine.Event.Handler_Detach(eEnvironment, this);
    Engine.Event.Handler_Detach(eChangeTrack, this);
    Engine.Event.Handler_Detach(eDemoPlay, this);
    Engine.Event.Handler_Detach(eChangeRP, this);

    if (physics_world()) {
        destroy_physics_world();
        xr_delete(m_ph_commander_physics_worldstep);
    }

    // Destroy Particle Systems via range-based for loop
    for (auto* particle : m_StaticParticles) {
        CParticlesObject::Destroy(particle);
    }
    m_StaticParticles.clear();

    sound_registry.clear();

    // Destroy static sounds
    for (auto* snd : static_Sounds) {
        snd->destroy();
        xr_delete(snd);
    }
    static_Sounds.clear();

    xr_delete(m_level_sound_manager);
    xr_delete(m_space_restriction_manager);
    xr_delete(m_seniority_hierarchy_holder);
    xr_delete(m_client_spawn_manager);
    xr_delete(m_autosave_manager);

#ifdef DEBUG
    xr_delete(m_debug_renderer);
    xr_delete(m_level_debug);
#endif

    ai().script_engine().remove_script_process(ScriptEngine::eScriptProcessorLevel);

    xr_delete(game);
    xr_delete(game_events);
    xr_delete(m_pBulletManager);
    
    xr_delete(pStatGraphR);
    xr_delete(pStatGraphS);

    xr_delete(m_ph_commander);
    xr_delete(m_ph_commander_scripts);
    
    pObjects4CrPr.clear();
    pActors4CrPr.clear();

    ai().unload();

    xr_delete(m_map_manager);
    delete_data(m_game_task_manager);

    CTradeParameters::clean();

    if (g_tutorial && g_tutorial->m_pStoredInputReceiver == this)
        g_tutorial->m_pStoredInputReceiver = nullptr;

    if (g_tutorial2 && g_tutorial2->m_pStoredInputReceiver == this)
        g_tutorial2->m_pStoredInputReceiver = nullptr;

    if (IsDemoPlay()) {
        StopPlayDemo();
        if (m_reader) {
            FS.r_close(m_reader);
            m_reader = nullptr;
        }
    }
    
    xr_delete(m_msg_filter);
    xr_delete(m_demoplay_control);
    xr_delete(m_demo_info);
    
    if (IsDemoSave()) {
        StopSaveDemo();
    }
    
    deinit_compression();
}

const std::string& CLevel::name() const { return map_data.m_name; }

void CLevel::GetLevelInfo(CServerInfo* si) 
{
    if (Server && game) {
        Server->GetServerInfo(si);
    }
}

void CLevel::PrefetchSound(LPCSTR name) 
{
    string_path tmp;
    xr_strcpy(tmp, name);
    xr_strlwr(tmp);
    if (strext(tmp))
        *strext(tmp) = 0;
        
    shared_str snd_name = tmp;
    
    // C++17 init-statement for cleaner scoping
    if (auto it = sound_registry.find(snd_name); it == sound_registry.end()) {
        sound_registry[snd_name].create(snd_name.c_str(), st_Effect, sg_SourceType);
    }
}

int CLevel::get_RPID(LPCSTR /*name*/) {
    return -1;
}

void CLevel::cl_Process_Event(u16 dest, u16 type, NET_Packet& P) 
{
    // C++17 Init-statement to tightly scope the objects
    if (CObject* O = Objects.net_Find(dest); !O) {
#ifdef DEBUG
        Msg("* WARNING: c_EVENT[%d] to [%d]: unknown dest", type, dest);
#endif
        return;
    } else if (CGameObject* GO = smart_cast<CGameObject*>(O); !GO) {
#ifndef MASTER_GOLD
        Msg("! ERROR: c_EVENT[%d] : non-game-object", dest);
#endif
        return;
    } else {
        if (type != GE_DESTROY_REJECT) {
            if (type == GE_DESTROY) {
                Game().OnDestroy(GO);
            }
            GO->OnEvent(P, type);
        } else { 
            // Handle GE_DESTROY_REJECT
            u32 pos = P.r_tell();
            u16 id = P.r_u16();
            P.r_seek(pos);

            bool ok = true;
            CObject* D = Objects.net_Find(id);
            if (!D) {
#ifndef MASTER_GOLD
                Msg("! ERROR: c_EVENT[%d] : unknown dest", id);
#endif
                ok = false;
            }

            CGameObject* GD = smart_cast<CGameObject*>(D);
            if (!GD) {
#ifndef MASTER_GOLD
                Msg("! ERROR: c_EVENT[%d] : non-game-object", id);
#endif
                ok = false;
            }

            GO->OnEvent(P, GE_OWNERSHIP_REJECT);
            if (ok) {
                Game().OnDestroy(GD);
                GD->OnEvent(P, GE_DESTROY);
            }
        }
    }
}

void CLevel::ProcessGameEvents() 
{
    NET_Packet P;
    u32 svT = timeServer() - NET_Latency;

    while (game_events->available(svT)) {
        u16 ID, dest, type;
        game_events->get(ID, dest, type, P);

        switch (ID) {
        case M_SPAWN: {
            u16 dummy16;
            P.r_begin(dummy16);
            cl_Process_Spawn(P);
            break;
        }
        case M_EVENT: {
            cl_Process_Event(dest, type, P);
            break;
        }
        case M_MOVE_PLAYERS: {
            u8 Count = P.r_u8();
            for (u8 i = 0; i < Count; i++) {
                u16 NetID = P.r_u16();
                Fvector NewPos, NewDir;
                P.r_vec3(NewPos);
                P.r_vec3(NewDir);

                if (CActor* OActor = smart_cast<CActor*>(Objects.net_Find(NetID)); OActor) {
                    OActor->MoveActor(NewPos, NewDir);
                } else {
                    break; 
                }
            }
            NET_Packet PRespond;
            PRespond.w_begin(M_MOVE_PLAYERS_RESPOND);
            Send(PRespond, net_flags(TRUE, TRUE));
            break;
        }
        case M_STATISTIC_UPDATE: {
            if (GameID() != eGameIDSingle)
                Game().m_WeaponUsageStatistic->OnUpdateRequest(&P);
            break;
        }
        case M_FILE_TRANSFER:
            break;
        case M_GAMEMESSAGE: {
            Game().OnGameMessage(P);
            break;
        }
        default: 
            VERIFY(0); 
            break;
        }
    }

    if (OnServer() && GameID() != eGameIDSingle) {
        Game().m_WeaponUsageStatistic->Send_Check_Respond();
    }
}

void CLevel::MakeReconnect() 
{
    if (!Engine.Event.Peek("KERNEL:disconnect")) {
        Engine.Event.Defer("KERNEL:disconnect");
        
        char const* server_options = m_caServerOptions.c_str() ? xr_strdup(*m_caServerOptions) : xr_strdup("");
        char const* client_options = m_caClientOptions.c_str() ? xr_strdup(*m_caClientOptions) : xr_strdup("");
        
        Engine.Event.Defer("KERNEL:start", size_t(server_options), size_t(client_options));
    }
}

void CLevel::OnFrame() 
{
#ifdef DEBUG
    DBG_RenderUpdate();
#endif 

    Fvector temp_vector;
    m_feel_deny.feel_touch_update(temp_vector, 0.f);

    psDeviceFlags.set(rsDisableObjectsAsCrows, GameID() != eGameIDSingle);

    Device.Statistic->TEST0.Begin();
    BulletManager().CommitEvents();
    Device.Statistic->TEST0.End();

    if (net_isDisconnected()) {
        if (OnClient() && GameID() != eGameIDSingle) {
#ifdef DEBUG
            Msg("--- I'm disconnected, so clear all objects...");
#endif
            ClearAllObjects();
        }
        Engine.Event.Defer("kernel:disconnect");
        return;
    } else {
        Device.Statistic->netClient1.Begin();
        ClientReceive();
        Device.Statistic->netClient1.End();
    }

    ProcessGameEvents();

    if (m_bNeed_CrPr)
        make_NetCorrectionPrediction();

    if (g_mt_config.test(mtMap))
        Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(m_map_manager, &CMapManager::Update));
    else
        MapManager().Update();

    if (IsGameTypeSingle() && Device.dwPrecacheFrame == 0) {
        GameTaskManager().UpdateTasks();
    }

    inherited::OnFrame();

    if (!psDeviceFlags.test(rsStatistic)) {
#ifdef DEBUG
        if (pStatGraphR)
            xr_delete(pStatGraphR);
#endif
    }
#ifdef DEBUG
    g_pGamePersistent->Environment().m_paused = m_bEnvPaused;
#endif

    g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(), game->GetEnvironmentGameTimeFactor());

    ai().script_engine().script_process(ScriptEngine::eScriptProcessorLevel)->update();
    m_ph_commander->update();
    m_ph_commander_scripts->update();

    Device.Statistic->TEST0.Begin();
    BulletManager().CommitRenderSet();
    Device.Statistic->TEST0.End();

    if (g_mt_config.test(mtLevelSounds))
        Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(m_level_sound_manager, &CLevelSoundManager::Update));
    else
        m_level_sound_manager->Update();
        
    if (g_mt_config.test(mtLUA_GC))
        Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(this, &CLevel::script_gc));
    else
        script_gc();

    if (pStatGraphR) {
        static constexpr float fRPC_Mult = 10.0f;
        static constexpr float fRPS_Mult = 1.0f;

        pStatGraphR->AppendItem(float(m_dwRPC) * fRPC_Mult, 0xffff0000, 1);
        pStatGraphR->AppendItem(float(m_dwRPS) * fRPS_Mult, 0xff00ff00, 0);
    }
}

void CLevel::script_gc() { 
    lua_gc(ai().script_engine().lua(), LUA_GCSTEP, psLUA_GCSTEP); 
}

void CLevel::OnRender() 
{
    inherited::OnRender();

    if (!game)
        return;

    Game().OnRender();
    BulletManager().Render();
    HUD().RenderUI();

#ifdef DEBUG
    draw_wnds_rects();
    physics_world()->OnRender();

    if (ai().get_level_graph())
        ai().level_graph().render();

#ifdef DEBUG_PRECISE_PATH
    test_precise_path();
#endif

    if (CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(Level().CurrentEntity()); stalker)
        stalker->OnRender();

    if (bDebug) {
        for (u32 I = 0; I < Level().Objects.o_count(); I++) {
            CObject* _O = Level().Objects.o_get_by_iterator(I);

            if (auto* stalker = smart_cast<CAI_Stalker*>(_O)) stalker->OnRender();
            if (auto* monster = smart_cast<CCustomMonster*>(_O)) monster->OnRender();
            if (auto* physic_object = smart_cast<CPhysicObject*>(_O)) physic_object->OnRender();
            if (auto* space_restrictor = smart_cast<CSpaceRestrictor*>(_O)) space_restrictor->OnRender();
            if (auto* climable = smart_cast<CClimableObject*>(_O)) climable->OnRender();
            if (auto* team_base_zone = smart_cast<CTeamBaseZone*>(_O)) team_base_zone->OnRender();

            if (GameID() != eGameIDSingle) {
                if (auto* pIItem = smart_cast<CInventoryItem*>(_O)) pIItem->OnRender();
            }

            if (dbg_net_Draw_Flags.test(dbg_draw_skeleton)) {
                if (CGameObject* pGO = smart_cast<CGameObject*>(_O); pGO && pGO != Level().CurrentViewEntity() && !pGO->H_Parent()) {
                    if (pGO->Position().distance_to_sqr(Device.vCameraPosition) < 400.0f) {
                        pGO->dbg_DrawSkeleton();
                    }
                }
            }
        }
        
        if (Server && Server->game)
            Server->game->OnRender();
            
        ObjectSpace.dbgRender();

        UI().Font().pFontStat->OutSet(170, 630);
        UI().Font().pFontStat->SetHeight(16.0f);
        UI().Font().pFontStat->SetColor(0xffff0000);

        if (Server) UI().Font().pFontStat->OutNext("Client Objects:      [%d]", Server->GetEntitiesNum());
        UI().Font().pFontStat->OutNext("Server Objects:      [%d]", Objects.o_count());
        UI().Font().pFontStat->OutNext("Interpolation Steps: [%d]", Level().GetInterpolationSteps());
        if (Server) UI().Font().pFontStat->OutNext("Server updates size: [%d]", Server->GetLastUpdatesSize());
        
        UI().Font().pFontStat->SetHeight(8.0f);
    }
    
    if (bDebug) {
        DBG().draw_object_info();
        DBG().draw_text();
        DBG().draw_level_info();
    }

    debug_renderer().render();
    DBG().draw_debug_text();

    if (psAI_Flags.is(aiVision)) {
        for (u32 I = 0; I < Level().Objects.o_count(); I++) {
            if (auto* stalker = smart_cast<CAI_Stalker*>(Objects.o_get_by_iterator(I)))
                stalker->dbg_draw_vision();
        }
    }

    if (psAI_Flags.test(aiDrawVisibilityRays)) {
        for (u32 I = 0; I < Level().Objects.o_count(); I++) {
            if (auto* stalker = smart_cast<CAI_Stalker*>(Objects.o_get_by_iterator(I)))
                stalker->dbg_draw_visibility_rays();
        }
    }
#endif
}

void CLevel::OnEvent(EVENT E, u64 P1, u64 /*P2*/) 
{
    if (E == eEntitySpawn) {
        char Name[128] = {0};
        sscanf(LPCSTR(P1), "%s", Name);
        Level().g_cl_Spawn(Name, 0xff, M_SPAWN_OBJECT_LOCAL, Fvector().set(0, 0, 0));
    } else if (E == eChangeRP && P1) {
    } else if (E == eDemoPlay && P1) {
        char* name = (char*)P1;
        string_path RealName;
        xr_strcpy(RealName, name);
        xr_strcat(RealName, ".xrdemo");
        Cameras().AddCamEffector(xr_new<CDemoPlay>(RealName, 1.3f, 0));
    } else if (E == eChangeTrack && P1) {
    } else if (E == eEnvironment) {
    }
}

void CLevel::AddObject_To_Objects4CrPr(CGameObject* pObj) 
{
    if (!pObj) return;
    if (std::find(pObjects4CrPr.begin(), pObjects4CrPr.end(), pObj) == pObjects4CrPr.end()) {
        pObjects4CrPr.push_back(pObj);
    }
}

void CLevel::AddActor_To_Actors4CrPr(CGameObject* pActor) 
{
    if (!pActor || !smart_cast<CActor*>(pActor)) return;
    if (std::find(pActors4CrPr.begin(), pActors4CrPr.end(), pActor) == pActors4CrPr.end()) {
        pActors4CrPr.push_back(pActor);
    }
}

void CLevel::RemoveObject_From_4CrPr(CGameObject* pObj) 
{
    if (!pObj) return;

    if (auto it = std::find(pObjects4CrPr.begin(), pObjects4CrPr.end(), pObj); it != pObjects4CrPr.end()) {
        pObjects4CrPr.erase(it);
    }

    if (auto it = std::find(pActors4CrPr.begin(), pActors4CrPr.end(), pObj); it != pActors4CrPr.end()) {
        pActors4CrPr.erase(it);
    }
}

void CLevel::make_NetCorrectionPrediction() 
{
    m_bNeed_CrPr = false;
    m_bIn_CrPr = true;
    u64 NumPhSteps = physics_world()->StepsNum();
    physics_world()->StepsNum() -= m_dwNumSteps;
    
    if (ph_console::g_bDebugDumpPhysicsStep && m_dwNumSteps > 10) {
        Msg("!!!TOO MANY PHYSICS STEPS FOR CORRECTION PREDICTION = %d !!!", m_dwNumSteps);
        m_dwNumSteps = 10;
    }
    
    physics_world()->Freeze();

    for (auto* pObj : pObjects4CrPr) {
        if (pObj) pObj->PH_B_CrPr();
    }

    for (u32 i = 0; i < m_dwNumSteps; i++) {
        physics_world()->Step();
        for (auto* pActor : pActors4CrPr) {
            if (pActor && !pActor->CrPr_IsActivated()) {
                pActor->PH_B_CrPr();
            }
        }
    }

    for (auto* pObj : pObjects4CrPr) {
        if (pObj) pObj->PH_I_CrPr();
    }

    if (!InterpolationDisabled()) {
        for (u32 i = 0; i < lvInterpSteps; i++) {
            physics_world()->Step();
        }
        for (auto* pObj : pObjects4CrPr) {
            if (pObj) pObj->PH_A_CrPr();
        }
    }
    
    physics_world()->UnFreeze();

    physics_world()->StepsNum() = NumPhSteps;
    m_dwNumSteps = 0;
    m_bIn_CrPr = false;

    pObjects4CrPr.clear();
    pActors4CrPr.clear();
}

u32 CLevel::GetInterpolationSteps() const { return lvInterpSteps; }

void CLevel::UpdateDeltaUpd(u32 LastTime) 
{
    u32 CurrentDelta = LastTime - m_dwLastNetUpdateTime;
    if (CurrentDelta < m_dwDeltaUpdate)
        CurrentDelta = iFloor(float(m_dwDeltaUpdate * 10 + CurrentDelta) / 11);

    m_dwLastNetUpdateTime = LastTime;
    m_dwDeltaUpdate = CurrentDelta;

    if (0 == g_cl_lvInterp)
        ReculcInterpolationSteps();
    else if (g_cl_lvInterp > 0)
        lvInterpSteps = iCeil(g_cl_lvInterp / fixed_step);
}

void CLevel::ReculcInterpolationSteps() const 
{
    lvInterpSteps = std::clamp(iFloor(float(m_dwDeltaUpdate) / (fixed_step * 1000)), 3, 60);
}

bool CLevel::InterpolationDisabled() { return g_cl_lvInterp < 0; }

void CLevel::PhisStepsCallback(u32 Time0, u32 Time1) 
{
    if (!Level().game || GameID() == eGameIDSingle)
        return;
}

void CLevel::SetNumCrSteps(u32 NumSteps) 
{
    m_bNeed_CrPr = true;
    if (m_dwNumSteps > NumSteps)
        return;
    m_dwNumSteps = NumSteps;
    if (m_dwNumSteps > 1000000) {
        VERIFY(0);
    }
}

ALife::_TIME_ID CLevel::GetStartGameTime() { return game->GetStartGameTime(); }
ALife::_TIME_ID CLevel::GetGameTime() { return game->GetGameTime(); }
ALife::_TIME_ID CLevel::GetEnvironmentGameTime() { return game->GetEnvironmentGameTime(); }

u8 CLevel::GetDayTime() 
{
    u32 dummy32, hours;
    GetGameDateTime(dummy32, dummy32, dummy32, hours, dummy32, dummy32, dummy32);
    VERIFY(hours < 256);
    return u8(hours);
}

float CLevel::GetGameDayTimeSec() {
    return (float(s64(GetGameTime() % (24 * 60 * 60 * 1000))) / 1000.f);
}

u32 CLevel::GetGameDayTimeMS() { 
    return (u32(s64(GetGameTime() % (24 * 60 * 60 * 1000)))); 
}

float CLevel::GetEnvironmentGameDayTimeSec() {
    return (float(s64(GetEnvironmentGameTime() % (24 * 60 * 60 * 1000))) / 1000.f);
}

void CLevel::GetGameDateTime(u32& year, u32& month, u32& day, u32& hours, u32& mins, u32& secs, u32& milisecs) 
{
    split_time(GetGameTime(), year, month, day, hours, mins, secs, milisecs);
}

float CLevel::GetGameTimeFactor() { return game->GetGameTimeFactor(); }
void CLevel::SetGameTimeFactor(const float fTimeFactor) { game->SetGameTimeFactor(fTimeFactor); }
void CLevel::SetGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor) {
    game->SetGameTimeFactor(GameTime, fTimeFactor);
}

void CLevel::SetEnvironmentGameTimeFactor(u64 const& GameTime, float const& fTimeFactor) 
{
    if (game) game->SetEnvironmentGameTimeFactor(GameTime, fTimeFactor);
}

bool CLevel::IsServer() const
{
    if (!Server || IsDemoPlayStarted()) return false;
    return true;
}

bool CLevel::IsClient() const
{
    if (IsDemoPlayStarted()) return true;
    if (Server) return false;
    return true;
}

void CLevel::OnAlifeSimulatorUnLoaded() 
{
    MapManager().ResetStorage();
    GameTaskManager().ResetStorage();
}

void CLevel::OnAlifeSimulatorLoaded() 
{
    MapManager().ResetStorage();
    GameTaskManager().ResetStorage();
}

void CLevel::OnSessionTerminate(LPCSTR reason) { MainMenu()->OnSessionTerminate(reason); }

u32 GameID() { return Game().Type(); }

CZoneList* CLevel::create_hud_zones_list() 
{
    hud_zones_list = xr_new<CZoneList>();
    hud_zones_list->clear();
    return hud_zones_list;
}

//-------------------------------------------------------------------------------------------------

BOOL CZoneList::feel_touch_contact(CObject* O) 
{
    bool res = m_TypesMap.find(O->cNameSect()) != m_TypesMap.end();
    if (CCustomZone* pZone = smart_cast<CCustomZone*>(O); pZone && !pZone->IsEnabled()) {
        res = false;
    }
    return res;
}

CZoneList::CZoneList() {}

CZoneList::~CZoneList() 
{
    clear();
    destroy();
}