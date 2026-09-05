#pragma once

#include "../xrEngine/igame_level.h"
#include "../xrEngine/IGame_Persistent.h"
#include "../xrNetServer/net_client.h"
#include "script_export_space.h"
#include "../xrEngine/StatGraph.h"
#include "xrMessages.h"
#include "alife_space.h"
#include "../xrcore/xrDebug.h"
#include "xrServer.h"
#include "GlobalFeelTouch.hpp"

#include "Level_network_map_sync.h"

// Forward declarations
class CHUDManager;
class CParticlesObject;
class xrServer;
class game_cl_GameState;
class NET_Queue_Event;
class CSE_Abstract;
class CSpaceRestrictionManager;
class CSeniorityHierarchyHolder;
class CClientSpawnManager;
class CGameObject;
class CAutosaveManager;
class CPHCommander;
class CLevelDebug;
class CLevelSoundManager;
class CGameTaskManager;
class CZoneList;
class message_filter;
class demoplay_control;
class demo_info;
class CFogOfWarMngr;
class CBulletManager;
class CMapManager;

#ifdef DEBUG
class CDebugRenderer;
#endif

extern float g_fov;

// C++17 inline constexpr for compile-time constants
inline constexpr int maxRP = 64;
inline constexpr int maxTeams = 32;

class CLevel : public IGame_Level, public IPureClient {
#include "Level_network_Demo.h"

private:
    void ClearAllObjects();

#ifdef DEBUG
    bool m_bSynchronization = false;
    bool m_bEnvPaused = false;
#endif

protected:
    using inherited = IGame_Level;

    // Default member initializers (C++11/17)
    CLevelSoundManager* m_level_sound_manager = nullptr;
    CSpaceRestrictionManager* m_space_restriction_manager = nullptr;
    CSeniorityHierarchyHolder* m_seniority_hierarchy_holder = nullptr;
    CClientSpawnManager* m_client_spawn_manager = nullptr;
    CAutosaveManager* m_autosave_manager = nullptr;

#ifdef DEBUG
    CDebugRenderer* m_debug_renderer = nullptr;
#endif

    CPHCommander* m_ph_commander = nullptr;
    CPHCommander* m_ph_commander_scripts = nullptr;
    CPHCommander* m_ph_commander_physics_worldstep = nullptr;

    // Local events
    EVENT eChangeRP;
    EVENT eDemoPlay;
    EVENT eChangeTrack;
    EVENT eEnvironment;
    EVENT eEntitySpawn;

    CStatGraph* pStatGraphS = nullptr;
    u32 m_dwSPC = 0; 
    u32 m_dwSPS = 0; 
    CStatGraph* pStatGraphR = nullptr;
    u32 m_dwRPC = 0; 
    u32 m_dwRPS = 0; 

public:
#ifdef DEBUG
    CLevelDebug* m_level_debug = nullptr;
#endif

    ////////////// network ////////////////////////
    [[nodiscard]] u32 GetInterpolationSteps() const;
    void SetInterpolationSteps(u32 InterpSteps);
    [[nodiscard]] static bool InterpolationDisabled();
    void ReculcInterpolationSteps() const;
    
    [[nodiscard]] u32 GetNumCrSteps() const { return m_dwNumSteps; }
    void SetNumCrSteps(u32 NumSteps);
    static void PhisStepsCallback(u32 Time0, u32 Time1);
    
    [[nodiscard]] bool In_NetCorrectionPrediction() const { return m_bIn_CrPr; }

    // Explicit override specifiers for safety
    virtual void OnMessage(void* data, u32 size) override;
    virtual void OnInvalidHost() override;
    virtual void OnSessionFull() override;
    virtual void OnConnectRejected() override;

private:
    BOOL m_bNeed_CrPr = FALSE;
    u32 m_dwNumSteps = 0;
    bool m_bIn_CrPr = false;

    using OBJECTS_LIST = xr_vector<CGameObject*>;

    OBJECTS_LIST pObjects4CrPr;
    OBJECTS_LIST pActors4CrPr;

    CObject* pCurrentControlEntity = nullptr;
    xrServer::EConnect m_connect_server_err = xrServer::ErrNoError;

public:
    void AddObject_To_Objects4CrPr(CGameObject* pObj);
    void AddActor_To_Actors4CrPr(CGameObject* pActor);
    void RemoveObject_From_4CrPr(CGameObject* pObj);

    [[nodiscard]] CObject* CurrentControlEntity() const { return pCurrentControlEntity; }
    void SetControlEntity(CObject* O) { pCurrentControlEntity = O; }

private:
    void make_NetCorrectionPrediction();

    u32 m_dwDeltaUpdate = 0;
    u32 m_dwLastNetUpdateTime = 0;
    void UpdateDeltaUpd(u32 LastTime);
    void BlockCheatLoad();

    BOOL Connect2Server(LPCSTR options);
    shared_str m_client_digest; 

public:
    [[nodiscard]] shared_str get_cdkey_digest() const { return m_client_digest; }

private:
    bool m_bConnectResultReceived = false;
    bool m_bConnectResult = false;
    xr_string m_sConnectResult;

public:
    void OnConnectResult(NET_Packet* P);

public:
    using POVec = xr_vector<CParticlesObject*>;
    POVec m_StaticParticles;

    game_cl_GameState* game = nullptr;
    BOOL m_bGameConfigStarted = FALSE;
    BOOL game_configured = FALSE;
    NET_Queue_Event* game_events = nullptr;
    xr_deque<CSE_Abstract*> game_spawn_queue;
    xrServer* Server = nullptr;
    GlobalFeelTouch m_feel_deny;

    CZoneList* hud_zones_list = nullptr;
    CZoneList* create_hud_zones_list();

private:
    using SoundRegistryMap = xr_map<shared_str, ref_sound>;
    SoundRegistryMap sound_registry;

public:
    void PrefetchSound(LPCSTR name);

protected:
    BOOL net_start_result_total = FALSE;
    BOOL connected_to_server = FALSE;
    BOOL deny_m_spawn = FALSE; 
    BOOL sended_request_connection_data = FALSE;

    void MakeReconnect();

    LevelMapSyncData map_data;
    bool synchronize_map_data();
    bool synchronize_client();

    bool xr_stdcall net_start1();
    bool xr_stdcall net_start2();
    bool xr_stdcall net_start3();
    bool xr_stdcall net_start4();
    bool xr_stdcall net_start5();
    bool xr_stdcall net_start6();

    bool xr_stdcall net_start_client1();
    bool xr_stdcall net_start_client2();
    bool xr_stdcall net_start_client3();
    bool xr_stdcall net_start_client4();
    bool xr_stdcall net_start_client5();
    bool xr_stdcall net_start_client6();

    void net_OnChangeSelfName(NET_Packet* P);
    void CalculateLevelCrc32();

public:
    [[nodiscard]] bool IsChecksumsEqual(u32 check_sum) const;

    xr_vector<ref_sound*> static_Sounds;

    shared_str m_caServerOptions;
    shared_str m_caClientOptions;

    // Core IGame_Level Overrides
    virtual BOOL net_Start(LPCSTR op_server, LPCSTR op_client) override;
    virtual void net_Load(LPCSTR name) override;
    virtual void net_Save(LPCSTR name) override;
    virtual void net_Stop() override;
    virtual BOOL net_Start_client(LPCSTR name);
    virtual void net_Update() override;

    virtual BOOL Load_GameSpecific_Before() override;
    virtual BOOL Load_GameSpecific_After() override;
    virtual void Load_GameSpecific_CFORM(CDB::TRI* T, u32 count) override;

    virtual void OnEvent(EVENT E, u64 P1, u64 P2) override;
    virtual void OnFrame() override;
    virtual void OnRender() override;

    virtual shared_str OpenDemoFile(LPCSTR demo_file_name) override;
    virtual void net_StartPlayDemo() override;

    void cl_Process_Event(u16 dest, u16 type, NET_Packet& P);
    void cl_Process_Spawn(NET_Packet& P);
    void ProcessGameEvents();
    void ProcessGameSpawns();
    void ProcessCompressedUpdate(NET_Packet& P, u8 const compression_type);

    // Input Interface Overrides
    virtual void IR_OnKeyboardPress(int btn) override;
    virtual void IR_OnKeyboardRelease(int btn) override;
    virtual void IR_OnKeyboardHold(int btn) override;
    virtual void IR_OnMousePress(int btn) override;
    virtual void IR_OnMouseRelease(int btn) override;
    virtual void IR_OnMouseHold(int btn) override;
    virtual void IR_OnMouseMove(int x, int y) override;
    virtual void IR_OnMouseStop(int x, int y) override;
    virtual void IR_OnMouseWheel(int direction) override;
    virtual void IR_OnActivate() override;

    int get_RPID(LPCSTR name);

    void InitializeClientGame(NET_Packet& P);
    void ClientReceive();
    void ClientSend();
    void ClientSave();
    u32 Objects_net_Save(NET_Packet* _Packet, u32 start, u32 count);
    virtual void Send(NET_Packet& P, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0) override;

    void g_cl_Spawn(LPCSTR name, u8 rp, u16 flags, Fvector pos);
    void g_sv_Spawn(CSE_Abstract* E);

    void SLS_Load(LPCSTR name);
    void SLS_Default();

    IC CSpaceRestrictionManager& space_restriction_manager();
    IC CSeniorityHierarchyHolder& seniority_holder();
    IC CClientSpawnManager& client_spawn_manager();
    IC CAutosaveManager& autosave_manager();
#ifdef DEBUG
    IC CDebugRenderer& debug_renderer();
#endif
    void __stdcall script_gc();

    IC CPHCommander& ph_commander();
    IC CPHCommander& ph_commander_scripts();
    IC CPHCommander& ph_commander_physics_worldstep();

    CLevel();
    virtual ~CLevel() override;

    [[nodiscard]] virtual const std::string& name() const override;
    [[nodiscard]] shared_str version() const { return map_data.m_map_version.c_str(); }

    virtual void GetLevelInfo(CServerInfo* si) override;

    [[nodiscard]] ALife::_TIME_ID GetStartGameTime();
    [[nodiscard]] ALife::_TIME_ID GetGameTime();
    [[nodiscard]] ALife::_TIME_ID GetEnvironmentGameTime();
    
    void GetGameDateTime(u32& year, u32& month, u32& day, u32& hours, u32& mins, u32& secs, u32& milisecs);

    [[nodiscard]] float GetGameTimeFactor();
    void SetGameTimeFactor(const float fTimeFactor);
    void SetGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor);
    virtual void SetEnvironmentGameTimeFactor(u64 const& GameTime, float const& fTimeFactor) override;

    [[nodiscard]] u8 GetDayTime();
    [[nodiscard]] u32 GetGameDayTimeMS();
    [[nodiscard]] float GetGameDayTimeSec();
    [[nodiscard]] float GetEnvironmentGameDayTimeSec();

protected:
    CMapManager* m_map_manager = nullptr;
    CGameTaskManager* m_game_task_manager = nullptr;
    CBulletManager* m_pBulletManager = nullptr;

public:
    IC CMapManager& MapManager() const { return *m_map_manager; }
    IC CGameTaskManager& GameTaskManager() const { return *m_game_task_manager; }
    IC CBulletManager& BulletManager() { return *m_pBulletManager; }

    void OnAlifeSimulatorLoaded();
    void OnAlifeSimulatorUnLoaded();

    [[nodiscard]] bool IsServer() const;
    [[nodiscard]] bool IsClient() const;
    
    CSE_Abstract* spawn_item(LPCSTR section, const Fvector& position, u32 level_vertex_id, u16 parent_id, bool return_item = false);

protected:
    u32 m_dwCL_PingDeltaSend = 0;
    u32 m_dwCL_PingLastSendTime = 0;
    u32 m_dwRealPing = 0;

public:
    [[nodiscard]] virtual u32 GetRealPing() { return m_dwRealPing; }
    void remove_objects();
    virtual void OnSessionTerminate(LPCSTR reason) override;

    u8* m_lzo_working_memory = nullptr;
    u8* m_lzo_working_buffer = nullptr;

    void init_compression();
    void deinit_compression();

    DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CLevel)
#undef script_type_list
#define script_type_list save_type_list(CLevel)

// -------------------------------------------------------------------------------------------------
// Global Accessors
// -------------------------------------------------------------------------------------------------

[[nodiscard]] IC CLevel& Level() { return *((CLevel*)g_pGameLevel); }
[[nodiscard]] IC game_cl_GameState& Game() { return *Level().game; }
[[nodiscard]] u32 GameID();

#ifdef DEBUG
[[nodiscard]] IC CLevelDebug& DBG() { return *((CLevelDebug*)Level().m_level_debug); }
#endif

IC CSpaceRestrictionManager& CLevel::space_restriction_manager() { VERIFY(m_space_restriction_manager); return (*m_space_restriction_manager); }
IC CSeniorityHierarchyHolder& CLevel::seniority_holder() { VERIFY(m_seniority_hierarchy_holder); return (*m_seniority_hierarchy_holder); }
IC CClientSpawnManager& CLevel::client_spawn_manager() { VERIFY(m_client_spawn_manager); return (*m_client_spawn_manager); }
IC CAutosaveManager& CLevel::autosave_manager() { VERIFY(m_autosave_manager); return (*m_autosave_manager); }

#ifdef DEBUG
IC CDebugRenderer& CLevel::debug_renderer() { VERIFY(m_debug_renderer); return (*m_debug_renderer); }
#endif

IC CPHCommander& CLevel::ph_commander() { VERIFY(m_ph_commander); return *m_ph_commander; }
IC CPHCommander& CLevel::ph_commander_scripts() { VERIFY(m_ph_commander_scripts); return *m_ph_commander_scripts; }
IC CPHCommander& CLevel::ph_commander_physics_worldstep() { VERIFY(m_ph_commander_physics_worldstep); return *m_ph_commander_physics_worldstep; }

[[nodiscard]] IC bool OnServer() { return Level().IsServer(); }
[[nodiscard]] IC bool OnClient() { return Level().IsClient(); }
[[nodiscard]] IC bool IsGameTypeSingle() { return (g_pGamePersistent->GameType() == eGameIDSingle); }

extern BOOL g_bDebugEvents;