////////////////////////////////////////////////////////////////////////////
//	Module 		: level_script.cpp
//	Created 	: 28.06.2004
//  Modified 	: 28.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Level script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "level.h"
#include "actor.h"
#include "script_game_object.h"
#include "patrol_path_storage.h"
#include "xrServer.h"
#include "client_spawn_manager.h"
#include "../xrEngine/igame_persistent.h"
#include "../xrEngine/Environment.h"
#include "../xrEngine/Thunderbolt.h"
#include "game_cl_base.h"
#include "UIGameCustom.h"
#include "UI/UIDialogWnd.h"
#include "date_time.h"
#include "ai_space.h"
#include "level_graph.h"
#include "PHCommander.h"
#include "PHScriptCall.h"
#include "script_engine.h"
#include "game_cl_single.h"
#include "game_sv_single.h"
#include "map_manager.h"
#include "map_spot.h"
#include "map_location.h"
#include "physics_world_scripted.h"
#include "alife_simulator.h"
#include "alife_time_manager.h"
#include "UI/UIGameTutorial.h"
#include "string_table.h"
#include "ui/UIInventoryUtilities.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "GamePersistent.h"
#include "CameraEffector.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/CameraManager.h"
#include "../xrSound/Sound.h"
#include "inventory.h"
#include "HudItem.h"
#include "Weapon.h"
#include "xr_level_controller.h"
#include "ui\UICinematicBorders.h"
#include "UIGameCustom.h"

using namespace luabind;

LPCSTR command_line() { return (Core.Params); }
bool IsDynamicMusic() { return !!psActorFlags.test(AF_DYNAMIC_MUSIC); }

bool IsImportantSave() { return !!psActorFlags.test(AF_IMPORTANT_SAVE); }

#ifdef DEBUG
void check_object(CScriptGameObject* object) {
    try {
        Msg("check_object %s", object->Name());
    } catch (...) {
        object = object;
    }
}

CScriptGameObject* tpfGetActor() {
    static bool first_time = true;
    if (first_time)
        ai().script_engine().script_log(eLuaMessageTypeError, "Do not use level.actor function!");
    first_time = false;

    CActor* l_tpActor = smart_cast<CActor*>(Level().CurrentEntity());
    if (l_tpActor)
        return (smart_cast<CGameObject*>(l_tpActor)->lua_game_object());
    else
        return (0);
}

CScriptGameObject* get_object_by_name(LPCSTR caObjectName) {
    static bool first_time = true;
    if (first_time)
        ai().script_engine().script_log(eLuaMessageTypeError, "Do not use level.object function!");
    first_time = false;

    CGameObject* l_tpGameObject =
        smart_cast<CGameObject*>(Level().Objects.FindObjectByName(caObjectName));
    if (l_tpGameObject)
        return (l_tpGameObject->lua_game_object());
    else
        return (0);
}
#endif

CScriptGameObject* get_object_by_id(u16 id) {
    CGameObject* pGameObject = smart_cast<CGameObject*>(Level().Objects.net_Find(id));
    if (!pGameObject)
        return nullptr;

    return pGameObject->lua_game_object();
}

LPCSTR get_weather() { return g_pGamePersistent->Environment().GetWeather().c_str(); }

void set_weather(LPCSTR weather_name, bool forced) {
#ifdef INGAME_EDITOR
    if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
        g_pGamePersistent->Environment().SetWeather(weather_name, forced);
}

bool set_weather_fx(LPCSTR weather_name) {
#ifdef INGAME_EDITOR
    if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
        return (g_pGamePersistent->Environment().SetWeatherFX(weather_name));

#ifdef INGAME_EDITOR
    return (false);
#endif // #ifdef INGAME_EDITOR
}

bool start_weather_fx_from_time(LPCSTR weather_name, float time) {
#ifdef INGAME_EDITOR
    if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
        return (g_pGamePersistent->Environment().StartWeatherFXFromTime(weather_name, time));

#ifdef INGAME_EDITOR
    return (false);
#endif // #ifdef INGAME_EDITOR
}

bool is_wfx_playing() { return (g_pGamePersistent->Environment().IsWFXPlaying()); }

float get_wfx_time() { return (g_pGamePersistent->Environment().wfx_time); }

void stop_weather_fx() { g_pGamePersistent->Environment().StopWFX(); }

void set_time_factor(float time_factor) {
    if (!OnServer())
        return;

#ifdef INGAME_EDITOR
    if (Device.editor())
        return;
#endif // #ifdef INGAME_EDITOR

    Level().Server->game->SetGameTimeFactor(time_factor);
}

float get_time_factor() { return (Level().GetGameTimeFactor()); }

void set_game_difficulty(ESingleGameDifficulty dif) {
    g_SingleGameDifficulty = dif;
    game_cl_Single* game = smart_cast<game_cl_Single*>(Level().game);
    VERIFY(game);
    game->OnDifficultyChanged();
}
ESingleGameDifficulty get_game_difficulty() { return g_SingleGameDifficulty; }

u32 get_time_days() {
    u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
    split_time((g_pGameLevel && Level().game) ? Level().GetGameTime()
                                              : ai().alife().time_manager().game_time(),
               year, month, day, hours, mins, secs, milisecs);
    return day;
}

u32 get_time_hours() {
    u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
    split_time((g_pGameLevel && Level().game) ? Level().GetGameTime()
                                              : ai().alife().time_manager().game_time(),
               year, month, day, hours, mins, secs, milisecs);
    return hours;
}

u32 get_time_minutes() {
    u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
    split_time((g_pGameLevel && Level().game) ? Level().GetGameTime()
                                              : ai().alife().time_manager().game_time(),
               year, month, day, hours, mins, secs, milisecs);
    return mins;
}

void change_game_time(u32 days, u32 hours, u32 mins) {
    game_sv_Single* tpGame = smart_cast<game_sv_Single*>(Level().Server->game);
    if (tpGame && ai().get_alife()) {
        u32 value = days * 86400 + hours * 3600 + mins * 60;
        float fValue = static_cast<float>(value);
        value *= 1000; // msec
        g_pGamePersistent->Environment().ChangeGameTime(fValue);
        tpGame->alife().time_manager().change_game_time(value);
    }
}

float high_cover_in_direction(u32 level_vertex_id, const Fvector& direction) {
    float y, p;
    direction.getHP(y, p);
    return (ai().level_graph().high_cover_in_direction(y, level_vertex_id));
}

float low_cover_in_direction(u32 level_vertex_id, const Fvector& direction) {
    float y, p;
    direction.getHP(y, p);
    return (ai().level_graph().low_cover_in_direction(y, level_vertex_id));
}

float rain_factor() { return (g_pGamePersistent->Environment().CurrentEnv->rain_density); }

u32 vertex_in_direction(u32 level_vertex_id, Fvector direction, float max_distance) {
    direction.normalize_safe();
    direction.mul(max_distance);
    Fvector start_position = ai().level_graph().vertex_position(level_vertex_id);
    Fvector finish_position = Fvector(start_position).add(direction);
    u32 result = u32(-1);
    ai().level_graph().farthest_vertex_in_direction(level_vertex_id, start_position,
                                                    finish_position, result, 0);
    return (ai().level_graph().valid_vertex_id(result) ? result : level_vertex_id);
}

Fvector vertex_position(u32 level_vertex_id) {
    return (ai().level_graph().vertex_position(level_vertex_id));
}

void map_add_object_spot(u16 id, LPCSTR spot_type, LPCSTR text) {
    CMapLocation* ml = Level().MapManager().AddMapLocation(spot_type, id);
    if (xr_strlen(text)) {
        ml->SetHint(text);
    }
}

void map_add_object_spot_ser(u16 id, LPCSTR spot_type, LPCSTR text) {
    CMapLocation* ml = Level().MapManager().AddMapLocation(spot_type, id);
    if (xr_strlen(text))
        ml->SetHint(text);

    ml->SetSerializable(true);
}

void map_change_spot_hint(u16 id, LPCSTR spot_type, LPCSTR text) {
    CMapLocation* ml = Level().MapManager().GetMapLocation(spot_type, id);
    if (!ml)
        return;
    ml->SetHint(text);
}

void map_remove_object_spot(u16 id, LPCSTR spot_type) {
    Level().MapManager().RemoveMapLocation(spot_type, id);
}

u16 map_has_object_spot(u16 id, LPCSTR spot_type) {
    return Level().MapManager().HasMapLocation(spot_type, id);
}

bool patrol_path_exists(LPCSTR patrol_path) {
    return (!!ai().patrol_paths().path(patrol_path, true));
}

// TODO: [imdex] use string_view
LPCSTR get_name() { return Level().name().c_str(); }

void prefetch_sound(LPCSTR name) { Level().PrefetchSound(name); }

CClientSpawnManager& get_client_spawn_manager() { return (Level().client_spawn_manager()); }

void add_dialog_to_render(CUIDialogWnd* pDialog) { CurrentGameUI()->AddDialogToRender(pDialog); }

void remove_dialog_to_render(CUIDialogWnd* pDialog) {
    CurrentGameUI()->RemoveDialogToRender(pDialog);
}

void hide_indicators() {
    if (CurrentGameUI()) {
        CurrentGameUI()->HideShownDialogs();
        CurrentGameUI()->ShowGameIndicators(false);
        CurrentGameUI()->ShowCrosshair(false);
    }
    psActorFlags.set(AF_GODMODE_RT, TRUE);
}

void hide_indicators_safe() {
    if (CurrentGameUI()) {
        CurrentGameUI()->ShowGameIndicators(false);
        CurrentGameUI()->ShowCrosshair(false);

        CurrentGameUI()->OnExternalHideIndicators();
    }
    psActorFlags.set(AF_GODMODE_RT, TRUE);
}

// ==============================================================================
// CUSTOM CINEMATIC TOOLSET (DoF, FOV, Cinematic Camera, HUD Anims, Actor Fire, Sound)
// ==============================================================================

// --- Depth of Field (DoF) Control ---
void set_dof_params_script(float near_blur, float focus_dist, float far_blur) {
    if (g_pGamePersistent) {
        Fvector dof;
        dof.set(near_blur, focus_dist, far_blur);
        GamePersistent().SetEffectorDOF(dof); 
    }
}

void restore_dof_script() {
    if (g_pGamePersistent) GamePersistent().RestoreEffectorDOF();
}

void set_dof_on_object_script(u16 id, float near_offset, float far_offset) {
    CGameObject* obj = smart_cast<CGameObject*>(Level().Objects.net_Find(id));
    if (obj && g_pGamePersistent) {
        float dist = obj->Position().distance_to(Device.vCameraPosition);
        Fvector dof;
        dof.set(dist - near_offset, dist, dist + far_offset);
        GamePersistent().SetEffectorDOF(dof);
    }
}

// --- Standalone FOV Effector ---
class CScriptFovEffector : public CEffectorCam {
    float m_fStartFov;
    float m_fTargetFov;
    float m_fTransitionTime;
    float m_fCurrentTime;
public:
    CScriptFovEffector(float target_fov, float time) : CEffectorCam((ECamEffectorType)(effCustomEffectorStartID + 1), 100000.f) {
        m_fTargetFov = target_fov;
        m_fTransitionTime = time;
        m_fCurrentTime = 0.0f;
        m_fStartFov = Device.fFOV; 
    }

    virtual BOOL ProcessCam(SCamEffectorInfo& info) {
        if (m_fTransitionTime <= 0.001f) {
            info.fFov = m_fTargetFov; 
        } else {
            m_fCurrentTime += Device.fTimeDelta;
            float t = m_fCurrentTime / m_fTransitionTime;
            clamp(t, 0.0f, 1.0f);
            info.fFov = m_fStartFov + (m_fTargetFov - m_fStartFov) * t;
        }
        return TRUE; 
    }
};

void set_camera_fov_script(float fov, float time) {
    if (!Actor()) return;
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 1));
    CScriptFovEffector* eff = xr_new<CScriptFovEffector>(fov, time);
    Actor()->Cameras().AddCamEffector(eff);
}

void restore_camera_fov_script() {
    if (!Actor()) return;
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 1));
}

// --- Unified Cinematic Effector (Look + Dolly + FOV) ---
class CScriptCinematicEffector : public CEffectorCam {
    Fvector m_vStartPos;
    Fvector m_vStartDir;
    u16 m_target_id;
    Fvector m_target_pos;
    float m_fApproachDist;
    float m_fStartFov;
    float m_fTargetFov;
    bool  m_bChangeFov;
    float m_fTransitionTime;
    float m_fCurrentTime;
    bool m_bDynamic;

public:
    CScriptCinematicEffector(u16 target_id, Fvector target_pos, float approach_dist, float target_fov, float time) 
        : CEffectorCam((ECamEffectorType)(effCustomEffectorStartID + 2), 100000.f) 
    {
        m_target_id = target_id;
        m_target_pos = target_pos;
        m_fApproachDist = approach_dist;
        m_fStartFov = Device.fFOV;
        m_fTargetFov = target_fov;
        m_bChangeFov = (target_fov > 0.1f);
        m_fTransitionTime = time;
        m_fCurrentTime = 0.0f;
        m_vStartPos = Device.vCameraPosition;
        m_vStartDir = Device.vCameraDirection; 
        m_bDynamic = (target_id != 0xffff);
    }

    virtual BOOL ProcessCam(SCamEffectorInfo& info) {
        Fvector target_world = m_target_pos;

        if (m_bDynamic) {
            CGameObject* target_obj = smart_cast<CGameObject*>(Level().Objects.net_Find(m_target_id));
            if (target_obj) {
                // Smart focus: entity head/chest level vs geometric center for props
                if (smart_cast<CEntityAlive*>(target_obj)) {
                    target_world = target_obj->Position();
                    target_world.y += 1.2f; 
                } else {
                    target_obj->Center(target_world); 
                }
            }
        }

        m_fCurrentTime += Device.fTimeDelta;
        float t = (m_fTransitionTime > 0.001f) ? (m_fCurrentTime / m_fTransitionTime) : 1.0f;
        clamp(t, 0.0f, 1.0f);
        float smooth_t = t * t * (3.0f - 2.0f * t);

        // Rotation (Look at)
        Fvector target_dir;
        target_dir.sub(target_world, info.p).normalize_safe();
        if (m_fTransitionTime <= 0.001f) {
            info.d = target_dir; 
        } else {
            Fvector current_dir;
            current_dir.lerp(m_vStartDir, target_dir, smooth_t).normalize_safe();
            info.d = current_dir;
        }

        // Dolly (Approach distance)
        if (m_fApproachDist > 0.01f) {
            Fvector dir_to_target;
            dir_to_target.sub(target_world, m_vStartPos).normalize_safe();
            Fvector final_pos = dir_to_target;
            final_pos.mul(m_fApproachDist);
            final_pos.add(m_vStartPos);
            info.p.lerp(m_vStartPos, final_pos, smooth_t);
        }

        // Zoom (FOV change)
        if (m_bChangeFov) {
            info.fFov = m_fStartFov + (m_fTargetFov - m_fStartFov) * smooth_t;
        }

        info.n.set(0.f, 1.f, 0.f); // Lock camera roll
        return TRUE;
    }
};

void run_cinematic_camera_obj_script(u16 target_id, float time, float approach_dist, float target_fov) {
    if (!Actor()) return;
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 2));
    CScriptCinematicEffector* eff = xr_new<CScriptCinematicEffector>(target_id, Fvector().set(0,0,0), approach_dist, target_fov, time);
    Actor()->Cameras().AddCamEffector(eff);
}

void run_cinematic_camera_pos_script(Fvector pos, float time, float approach_dist, float target_fov) {
    if (!Actor()) return;
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 2));
    CScriptCinematicEffector* eff = xr_new<CScriptCinematicEffector>(0xffff, pos, approach_dist, target_fov, time);
    Actor()->Cameras().AddCamEffector(eff);
}

void restore_camera_look_script(bool bMaintainOrientation) {
    if (!Actor()) return;
    if (bMaintainOrientation) {
        float h, p;
        Device.vCameraDirection.getHP(h, p);
        Actor()->cam_Active()->yaw   = -h;
        Actor()->cam_Active()->pitch = -p;
    }
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 2));
}

// --- Cinematic Spline Flight Effector (Catmull-Rom) ---
static xr_vector<Fvector> g_flight_path; 

// Режими напрямку погляду камери
enum EFlightLookMode {
    flmForward  = 0, // Вперед по сплайну
    flmBackward = 1, // Назад
    flmObject   = 2, // На Story ID / Object ID
    flmCoords   = 3  // На статичні координати
};

class CScriptFlightEffector : public CEffectorCam {
    xr_vector<Fvector> m_points;
    float m_fEntryTime;
    float m_fFlightTime;
    float m_fExitTime;
    float m_fCurrentTime;
    
    Fvector m_vActorStartPos;
    Fvector m_vActorStartDir;

    Fvector m_vExitStartPos;
    Fvector m_vExitStartDir;
    bool m_bExitCaptured;
    bool m_bFirstUpdate;

    EFlightLookMode m_eLookMode;
    u16 m_target_id;
    Fvector m_target_pos;

public:
    CScriptFlightEffector(float entry_time, float flight_time, float exit_time, EFlightLookMode look_mode, u16 target_id, Fvector target_pos, Fvector start_pos, Fvector start_dir) 
        : CEffectorCam((ECamEffectorType)(effCustomEffectorStartID + 3), 100000.f) 
    {
        m_fEntryTime = entry_time;
        m_fFlightTime = flight_time;
        m_fExitTime = exit_time;
        m_fCurrentTime = 0.0f;
        m_bExitCaptured = false;
        m_bFirstUpdate = true; 
        
        m_vActorStartPos = start_pos;
        m_vActorStartDir = start_dir;

        m_eLookMode = look_mode;
        m_target_id = target_id;
        m_target_pos = target_pos;
        
        m_points = g_flight_path;
        g_flight_path.clear();
    }

    Fvector GetSplinePoint(float t) {
        int p0, p1, p2, p3;
        int max_p = m_points.size() - 1;
        float t_scaled = t * max_p;
        int segment = iFloor(t_scaled);
        float local_t = t_scaled - segment;
        
        p1 = segment;
        p2 = (p1 + 1 > max_p) ? max_p : p1 + 1;
        p0 = (p1 - 1 < 0) ? p1 : p1 - 1;
        p3 = (p2 + 1 > max_p) ? p2 : p2 + 1;
        
        float t2 = local_t * local_t;
        float t3 = t2 * local_t;
        Fvector res;
        res.x = 0.5f * ((2.0f * m_points[p1].x) + (-m_points[p0].x + m_points[p2].x) * local_t + (2.0f * m_points[p0].x - 5.0f * m_points[p1].x + 4.0f * m_points[p2].x - m_points[p3].x) * t2 + (-m_points[p0].x + 3.0f * m_points[p1].x - 3.0f * m_points[p2].x + m_points[p3].x) * t3);
        res.y = 0.5f * ((2.0f * m_points[p1].y) + (-m_points[p0].y + m_points[p2].y) * local_t + (2.0f * m_points[p0].y - 5.0f * m_points[p1].y + 4.0f * m_points[p2].y - m_points[p3].y) * t2 + (-m_points[p0].y + 3.0f * m_points[p1].y - 3.0f * m_points[p2].y + m_points[p3].y) * t3);
        res.z = 0.5f * ((2.0f * m_points[p1].z) + (-m_points[p0].z + m_points[p2].z) * local_t + (2.0f * m_points[p0].z - 5.0f * m_points[p1].z + 4.0f * m_points[p2].z - m_points[p3].z) * t2 + (-m_points[p0].z + 3.0f * m_points[p1].z - 3.0f * m_points[p2].z + m_points[p3].z) * t3);
        return res;
    }

    Fvector GetLookDirection(float t, const Fvector& current_pos) {
        Fvector dir;
        if (m_eLookMode == flmObject) {
            CGameObject* obj = smart_cast<CGameObject*>(Level().Objects.net_Find(m_target_id));
            if (obj) {
                Fvector look_point = obj->Position();
                if (smart_cast<CEntityAlive*>(obj)) look_point.y += 1.2f; 
                else obj->Center(look_point);
                dir.sub(look_point, current_pos).normalize_safe();
                if (dir.magnitude() > 0.0001f) return dir;
            }
        } else if (m_eLookMode == flmCoords) {
            dir.sub(m_target_pos, current_pos).normalize_safe();
            if (dir.magnitude() > 0.0001f) return dir;
        } else if (m_eLookMode == flmBackward) {
            float t_prev = t - 0.01f;
            if (t_prev < 0.0f) {
                float t_next = (t + 0.01f <= 1.0f) ? (t + 0.01f) : 1.0f;
                Fvector next_p = GetSplinePoint(t_next);
                dir.sub(current_pos, next_p).normalize_safe();
            } else {
                Fvector prev_p = GetSplinePoint(t_prev);
                dir.sub(prev_p, current_pos).normalize_safe();
            }
            if (dir.magnitude() > 0.0001f) return dir;
        }
        
        float t_next = t + 0.01f;
        if (t_next > 1.0f) {
            float t_prev = (t - 0.01f >= 0.0f) ? (t - 0.01f) : 0.0f;
            Fvector prev_p = GetSplinePoint(t_prev);
            dir.sub(current_pos, prev_p).normalize_safe();
        } else {
            Fvector next_p = GetSplinePoint(t_next);
            dir.sub(next_p, current_pos).normalize_safe();
        }

        if (dir.magnitude() < 0.0001f) {
            dir = m_vActorStartDir;
        }
        return dir;
    }

virtual BOOL ProcessCam(SCamEffectorInfo& info) {
        if (m_points.size() < 2) return FALSE;

        // Захоплюємо справжню камеру актора з усіма ефектами дихання/розгойдування, 
        // щоб на фініші злитися з нею ідеально, без мікро-стрибків.
        Fvector base_p = info.p;
        Fvector base_d = info.d;

        // АНТИ-СТАТТЕР СИСТЕМА
        float dt = Device.fTimeDelta;
        if (m_bFirstUpdate) {
            dt = 0.0f; 
            m_bFirstUpdate = false;
        } else if (dt > 0.05f) {
            dt = 0.05f; 
        }
        
        m_fCurrentTime += dt;

        // ==========================================
        // ФАЗА 1: ПЛАВНИЙ ВХІД
        // ==========================================
        if (m_fCurrentTime <= m_fEntryTime && m_fEntryTime > 0.001f) {
            float t = m_fCurrentTime / m_fEntryTime;
            clamp(t, 0.0f, 1.0f);
            float smooth_t = t * t * (3.0f - 2.0f * t);

            info.p.lerp(m_vActorStartPos, GetSplinePoint(0.0f), smooth_t);

            Fvector target_dir = GetLookDirection(0.0f, info.p);
            info.d.lerp(m_vActorStartDir, target_dir, smooth_t).normalize_safe();
            info.n.set(0.f, 1.f, 0.f);
            return TRUE;
        }

        // ==========================================
        // ФАЗА 2: ПОЛІТ ПО СПЛАЙНУ
        // ==========================================
        if (m_fCurrentTime <= m_fEntryTime + m_fFlightTime) {
            float flight_time_elapsed = m_fCurrentTime - m_fEntryTime;
            float t = (m_fFlightTime > 0.001f) ? (flight_time_elapsed / m_fFlightTime) : 1.0f;
            clamp(t, 0.0f, 1.0f);

            info.p = GetSplinePoint(t);
            info.d = GetLookDirection(t, info.p);
            info.n.set(0.f, 1.f, 0.f);
            return TRUE;
        }

        // ==========================================
        // ФАЗА 3: ПЛАВНИЙ ВИХІД ДО ТІЛА АКТОРA
        // ==========================================
        if (m_fCurrentTime <= m_fEntryTime + m_fFlightTime + m_fExitTime && m_fExitTime > 0.001f) {
            if (!m_bExitCaptured) {
                m_vExitStartPos = GetSplinePoint(1.0f); 
                m_vExitStartDir = GetLookDirection(1.0f, m_vExitStartPos); 
                m_bExitCaptured = true;
            }

            float exit_elapsed = m_fCurrentTime - (m_fEntryTime + m_fFlightTime);
            float t = exit_elapsed / m_fExitTime;
            clamp(t, 0.0f, 1.0f);
            float smooth_t = t * t * (3.0f - 2.0f * t);

            // М'яко зливаємося з реальною камерою (base_p), а не статичною точкою
            info.p.lerp(m_vExitStartPos, base_p, smooth_t);
            info.d.lerp(m_vExitStartDir, base_d, smooth_t).normalize_safe();
            info.n.set(0.f, 1.f, 0.f);
            return TRUE;
        }

        return FALSE; 
    }
};

// --- Оновлена обгортка для Lua ---
void flight_path_clear_script() { g_flight_path.clear(); }
void flight_path_add_point_script(Fvector pos) { g_flight_path.push_back(pos); }

void flight_start_script(float entry, float flight, float exit, int mode, u16 id, Fvector pos, Fvector start_pos, Fvector start_dir) {
    if (!Actor()) return;
    Actor()->Cameras().RemoveCamEffector((ECamEffectorType)(effCustomEffectorStartID + 3));
    Actor()->Cameras().AddCamEffector(xr_new<CScriptFlightEffector>(entry, flight, exit, (EFlightLookMode)mode, id, pos, start_pos, start_dir));
}
// --- HUD Animations & Weapon Fire ---
void force_play_hud_anim_script(LPCSTR anim_name) {
    if (!Actor()) return;
    CInventoryItem* active_item = Actor()->inventory().ActiveItem();
    if (!active_item) {
        Msg("! ERROR: force_play_hud_anim - No active item in Actor's hands!");
        return;
    }
    CHudItem* hud_item = smart_cast<CHudItem*>(active_item);
    if (!hud_item) return;
    hud_item->PlayHUDMotion(shared_str(anim_name), TRUE, hud_item, hud_item->GetState());
}

void actor_fire_start_script() {
    if (!Actor()) return;
    CWeapon* wpn = smart_cast<CWeapon*>(Actor()->inventory().ActiveItem());
    if (wpn) wpn->Action(kWPN_FIRE, CMD_START);
}

void actor_fire_stop_script() {
    if (!Actor()) return;
    CWeapon* wpn = smart_cast<CWeapon*>(Actor()->inventory().ActiveItem());
    if (wpn) wpn->Action(kWPN_FIRE, CMD_STOP);
}

// --- 3D Sound Pool ---
static xr_vector<ref_sound*> g_script_sounds;

void play_sound_3d_script(LPCSTR path, Fvector pos, float vol, float pitch) {
    for (auto it = g_script_sounds.begin(); it != g_script_sounds.end(); ) {
        if (!(*it)->_feedback()) {
            (*it)->destroy();
            xr_delete(*it);
            it = g_script_sounds.erase(it);
        } else {
            ++it;
        }
    }
    ref_sound* snd = xr_new<ref_sound>();
    snd->create(path, st_Effect, 0); 
    snd->play_at_pos(0, pos, 0);
    snd->set_volume(vol);
    snd->set_frequency(pitch); 
    g_script_sounds.push_back(snd);
}

void stop_all_custom_sounds_script() {
    for (auto it = g_script_sounds.begin(); it != g_script_sounds.end(); ++it) {
        (*it)->destroy();
        xr_delete(*it);
    }
    g_script_sounds.clear();
}

// === NOIR ENGINE: Cinematic EFX Control ===
void set_efx_override_script(float room, float room_hf, float decay_time, float decay_hf_ratio, float reflections_delay, float reverb_delay, float room_rolloff_factor, float diffusion, float reflections, float reverb, float air_absorption_hf) {
    if (Sound) {
        Sound->set_efx_override(true, room, room_hf, decay_time, decay_hf_ratio, reflections_delay, reverb_delay, room_rolloff_factor, diffusion, reflections, reverb, air_absorption_hf);
    }
}

void set_efx_preset_script(LPCSTR preset_name) {
    if (Sound) {
        Sound->set_efx_override(preset_name);
    }
}

void disable_efx_override_script() {
    if (Sound) {
        Sound->set_efx_override(false);
    }
}
// ==========================================

// Статичний вказівник, який гарантує, що об'єкт існує і ним не керує збирач сміття Lua
static CCinematicBorders* g_cinematic_borders = nullptr;

void show_cinematic_borders_script(int appear_type, u32 duration_ms) {
    if (!g_cinematic_borders) {
        g_cinematic_borders = xr_new<CCinematicBorders>();
    }
    
    if (CurrentGameUI()) {
        // Запобігаємо дублюванню у списку рендера
        CurrentGameUI()->RemoveDialogToRender(g_cinematic_borders);
        CurrentGameUI()->AddDialogToRender(g_cinematic_borders);
    }
    
    g_cinematic_borders->Show(appear_type, duration_ms);
}

void hide_cinematic_borders_script(int disappear_type, u32 duration_ms) {
    if (g_cinematic_borders) {
        g_cinematic_borders->Hide(disappear_type, duration_ms);
    }
}

void show_indicators() {
    if (CurrentGameUI()) {
        CurrentGameUI()->ShowGameIndicators(true);
        CurrentGameUI()->ShowCrosshair(true);
    }
    psActorFlags.set(AF_GODMODE_RT, FALSE);
}

void show_weapon(bool b) { psHUD_Flags.set(HUD_WEAPON_RT2, b); }

bool is_level_present() { return (!!g_pGameLevel); }

void add_call(const luabind::functor<bool>& condition, const luabind::functor<void>& action) {
    luabind::functor<bool> _condition = condition;
    luabind::functor<void> _action = action;
    CPHScriptCondition* c = xr_new<CPHScriptCondition>(_condition);
    CPHScriptAction* a = xr_new<CPHScriptAction>(_action);
    Level().ph_commander_scripts().add_call(c, a);
}

void remove_call(const luabind::functor<bool>& condition, const luabind::functor<void>& action) {
    CPHScriptCondition c(condition);
    CPHScriptAction a(action);
    Level().ph_commander_scripts().remove_call(&c, &a);
}

void add_call(const luabind::object& lua_object, LPCSTR condition, LPCSTR action) {
    luabind::functor<bool> _condition = object_cast<luabind::functor<bool>>(lua_object[condition]);
    luabind::functor<void> _action = object_cast<luabind::functor<void>>(lua_object[action]);
    CPHScriptObjectConditionN* c = xr_new<CPHScriptObjectConditionN>(lua_object, _condition);
    CPHScriptObjectActionN* a = xr_new<CPHScriptObjectActionN>(lua_object, _action);
    Level().ph_commander_scripts().add_call_unique(c, c, a, a);
}

void remove_call(const luabind::object& lua_object, LPCSTR condition, LPCSTR action) {
    CPHScriptObjectCondition c(lua_object, condition);
    CPHScriptObjectAction a(lua_object, action);
    Level().ph_commander_scripts().remove_call(&c, &a);
}

void add_call(const luabind::object& lua_object, const luabind::functor<bool>& condition,
              const luabind::functor<void>& action) {

    CPHScriptObjectConditionN* c = xr_new<CPHScriptObjectConditionN>(lua_object, condition);
    CPHScriptObjectActionN* a = xr_new<CPHScriptObjectActionN>(lua_object, action);
    Level().ph_commander_scripts().add_call(c, a);
}

void remove_call(const luabind::object& lua_object, const luabind::functor<bool>& condition,
                 const luabind::functor<void>& action) {
    CPHScriptObjectConditionN c(lua_object, condition);
    CPHScriptObjectActionN a(lua_object, action);
    Level().ph_commander_scripts().remove_call(&c, &a);
}

void remove_calls_for_object(const luabind::object& lua_object) {
    CPHSriptReqObjComparer c(lua_object);
    Level().ph_commander_scripts().remove_calls(&c);
}

cphysics_world_scripted* physics_world_scripted() {
    return get_script_wrapper<cphysics_world_scripted>(*physics_world());
}
CEnvironment* environment() { return (g_pGamePersistent->pEnvironment); }

CEnvDescriptor* current_environment(CEnvironment* self) { return (self->CurrentEnv); }
extern bool g_bDisableAllInput;
void disable_input() {
    g_bDisableAllInput = true;
#ifdef DEBUG
    Msg("input disabled");
#endif // #ifdef DEBUG
}
void enable_input() {
    g_bDisableAllInput = false;
#ifdef DEBUG
    Msg("input enabled");
#endif // #ifdef DEBUG
}

void spawn_lightning_at_pos(LPCSTR id, Fvector pos) {
    if (g_pGamePersistent) {
        CEffect_Thunderbolt* tb = g_pGamePersistent->Environment().eff_Thunderbolt;
        if (tb) {
            tb->ForceStrike(id, pos);
        }
    }
}
// ----------------------------------------

void spawn_phantom(const Fvector& position) {
    Level().spawn_item("m_phantom", position, u32(-1), u16(-1), false);
}

Fbox get_bounding_volume() { return Level().ObjectSpace.GetBoundingVolume(); }

void iterate_sounds(LPCSTR prefix, u32 max_count, const CScriptCallbackEx<void>& callback) {
    for (int j = 0, N = _GetItemCount(prefix); j < N; ++j) {
        string_path fn, s;
        LPSTR S = (LPSTR)&s;
        _GetItem(prefix, j, s);
        if (FS.exist(fn, "$game_sounds$", S, ".ogg"))
            callback(prefix);

        for (u32 i = 0; i < max_count; ++i) {
            string_path name;
            xr_sprintf(name, "%s%d", S, i);
            if (FS.exist(fn, "$game_sounds$", name, ".ogg"))
                callback(name);
        }
    }
}

void iterate_sounds1(LPCSTR prefix, u32 max_count, luabind::functor<void> functor) {
    CScriptCallbackEx<void> temp;
    temp.set(functor);
    iterate_sounds(prefix, max_count, temp);
}

void iterate_sounds2(LPCSTR prefix, u32 max_count, luabind::object object,
                     luabind::functor<void> functor) {
    CScriptCallbackEx<void> temp;
    temp.set(functor, object);
    iterate_sounds(prefix, max_count, temp);
}

#include "actoreffector.h"
float add_cam_effector(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func) {
    CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
    e->SetType((ECamEffectorType)id);
    e->SetCyclic(cyclic);
    e->Start(fn);
    Actor()->Cameras().AddCamEffector(e);
    return e->GetAnimatorLength();
}

float add_cam_effector2(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func, float cam_fov) {
    CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
    e->m_bAbsolutePositioning = true;
    e->m_fov = cam_fov;
    e->SetType((ECamEffectorType)id);
    e->SetCyclic(cyclic);
    e->Start(fn);
    Actor()->Cameras().AddCamEffector(e);
    return e->GetAnimatorLength();
}

void remove_cam_effector(int id) { Actor()->Cameras().RemoveCamEffector((ECamEffectorType)id); }

float get_snd_volume() { return psSoundVFactor; }

void set_snd_volume(float v) {
    psSoundVFactor = v;
    clamp(psSoundVFactor, 0.0f, 1.0f);
}
#include "actor_statistic_mgr.h"
void add_actor_points(LPCSTR sect, LPCSTR detail_key, int cnt, int pts) {
    return Actor()->StatisticMgr().AddPoints(sect, detail_key, cnt, pts);
}

void add_actor_points_str(LPCSTR sect, LPCSTR detail_key, LPCSTR str_value) {
    return Actor()->StatisticMgr().AddPoints(sect, detail_key, str_value);
}

int get_actor_points(LPCSTR sect) { return Actor()->StatisticMgr().GetSectionPoints(sect); }

#include "ActorEffector.h"
void add_complex_effector(LPCSTR section, int id) { AddEffector(Actor(), id, section); }

void remove_complex_effector(int id) { RemoveEffector(Actor(), id); }

#include "postprocessanimator.h"
void add_pp_effector(LPCSTR fn, int id, bool cyclic) {
    CPostprocessAnimator* pp = xr_new<CPostprocessAnimator>(id, cyclic);
    pp->Load(fn);
    Actor()->Cameras().AddPPEffector(pp);
}

void remove_pp_effector(int id) {
    CPostprocessAnimator* pp =
        smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

    if (pp)
        pp->Stop(1.0f);
}

void set_pp_effector_factor(int id, float f, float f_sp) {
    CPostprocessAnimator* pp =
        smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

    if (pp)
        pp->SetDesiredFactor(f, f_sp);
}

void set_pp_effector_factor2(int id, float f) {
    CPostprocessAnimator* pp =
        smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

    if (pp)
        pp->SetCurrentFactor(f);
}

#include "relation_registry.h"

int g_community_goodwill(LPCSTR _community, int _entity_id) {
    CHARACTER_COMMUNITY c;
    c.set(_community);

    return RELATION_REGISTRY().GetCommunityGoodwill(c.index(), u16(_entity_id));
}

void g_set_community_goodwill(LPCSTR _community, int _entity_id, int val) {
    CHARACTER_COMMUNITY c;
    c.set(_community);
    RELATION_REGISTRY().SetCommunityGoodwill(c.index(), u16(_entity_id), val);
}

void g_change_community_goodwill(LPCSTR _community, int _entity_id, int val) {
    CHARACTER_COMMUNITY c;
    c.set(_community);
    RELATION_REGISTRY().ChangeCommunityGoodwill(c.index(), u16(_entity_id), val);
}

int g_get_community_relation(LPCSTR comm_from, LPCSTR comm_to) {
    CHARACTER_COMMUNITY community_from;
    community_from.set(comm_from);
    CHARACTER_COMMUNITY community_to;
    community_to.set(comm_to);

    return RELATION_REGISTRY().GetCommunityRelation(community_from.index(), community_to.index());
}

void g_set_community_relation(LPCSTR comm_from, LPCSTR comm_to, int value) {
    CHARACTER_COMMUNITY community_from;
    community_from.set(comm_from);
    CHARACTER_COMMUNITY community_to;
    community_to.set(comm_to);

    RELATION_REGISTRY().SetCommunityRelation(community_from.index(), community_to.index(), value);
}

int g_get_general_goodwill_between(u16 from, u16 to) {
    CHARACTER_GOODWILL presonal_goodwill = RELATION_REGISTRY().GetGoodwill(from, to);
    VERIFY(presonal_goodwill != NO_GOODWILL);

    CSE_ALifeTraderAbstract* from_obj =
        smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(from));
    CSE_ALifeTraderAbstract* to_obj =
        smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(to));

    if (!from_obj || !to_obj) {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
                                        "RELATION_REGISTRY::get_general_goodwill_between  : cannot "
                                        "convert obj to CSE_ALifeTraderAbstract!");
        return (0);
    }
    CHARACTER_GOODWILL community_to_obj_goodwill =
        RELATION_REGISTRY().GetCommunityGoodwill(from_obj->Community(), to);
    CHARACTER_GOODWILL community_to_community_goodwill =
        RELATION_REGISTRY().GetCommunityRelation(from_obj->Community(), to_obj->Community());

    return presonal_goodwill + community_to_obj_goodwill + community_to_community_goodwill;
}

u32 vertex_id(Fvector position) { return (ai().level_graph().vertex_id(position)); }

u32 render_get_dx_level() { return ::Render->get_dx_level(); }

CUISequencer* g_tutorial = NULL;
CUISequencer* g_tutorial2 = NULL;

void start_tutorial(LPCSTR name) {
    if (g_tutorial) {
        VERIFY(!g_tutorial2);
        g_tutorial2 = g_tutorial;
    };

    g_tutorial = xr_new<CUISequencer>();
    g_tutorial->Start(name);
    if (g_tutorial2)
        g_tutorial->m_pStoredInputReceiver = g_tutorial2->m_pStoredInputReceiver;
}

void stop_tutorial() {
    if (g_tutorial)
        g_tutorial->Stop();
}

LPCSTR translate_string(LPCSTR str) { return *CStringTable().translate(str); }

bool has_active_tutotial() { return (g_tutorial != NULL); }

#pragma optimize("s", on)
void CLevel::script_register(lua_State* L) {
    class_<CEnvDescriptor>("CEnvDescriptor")
        .def_readonly("fog_density", &CEnvDescriptor::fog_density)
        .def_readonly("far_plane", &CEnvDescriptor::far_plane),

        class_<CEnvironment>("CEnvironment").def("current", current_environment);

    module(L, "level")[
        // obsolete\deprecated
        def("object_by_id", get_object_by_id),
#ifdef DEBUG
        def("debug_object", get_object_by_name), def("debug_actor", tpfGetActor),
        def("check_object", check_object),
#endif

        def("get_weather", get_weather), def("set_weather", set_weather),
        def("set_weather_fx", set_weather_fx),
        def("start_weather_fx_from_time", start_weather_fx_from_time),
        def("is_wfx_playing", is_wfx_playing), def("get_wfx_time", get_wfx_time),
        def("stop_weather_fx", stop_weather_fx),

        def("environment", environment),

        def("set_time_factor", set_time_factor), def("get_time_factor", get_time_factor),

        def("set_game_difficulty", set_game_difficulty),
        def("get_game_difficulty", get_game_difficulty),

        def("get_time_days", get_time_days), def("get_time_hours", get_time_hours),
        def("get_time_minutes", get_time_minutes), def("change_game_time", change_game_time),

        def("high_cover_in_direction", high_cover_in_direction),
        def("low_cover_in_direction", low_cover_in_direction),
        def("vertex_in_direction", vertex_in_direction), def("rain_factor", rain_factor),
        def("patrol_path_exists", patrol_path_exists), def("vertex_position", vertex_position),
        def("name", get_name), def("prefetch_sound", prefetch_sound),

        def("client_spawn_manager", get_client_spawn_manager),

        def("map_add_object_spot_ser", map_add_object_spot_ser),
        def("map_add_object_spot", map_add_object_spot),
        //-		def("map_add_object_spot_complex", map_add_object_spot_complex),
        def("map_remove_object_spot", map_remove_object_spot),
        def("map_has_object_spot", map_has_object_spot),
        def("map_change_spot_hint", map_change_spot_hint),

        def("add_dialog_to_render", add_dialog_to_render),
        def("remove_dialog_to_render", remove_dialog_to_render),
        def("hide_indicators", hide_indicators), def("hide_indicators_safe", hide_indicators_safe),

        def("show_indicators", show_indicators), def("show_weapon", show_weapon),
		
		// Cinematic Toolset Bindings
        def("set_dof_script", &set_dof_params_script),
        def("restore_dof_script", &restore_dof_script),
        def("set_dof_obj_script", &set_dof_on_object_script),
        
        def("set_fov_script", &set_camera_fov_script),
        def("restore_fov_script", &restore_camera_fov_script),
        
        def("run_cinematic_camera_obj", &run_cinematic_camera_obj_script),
        def("run_cinematic_camera_pos", &run_cinematic_camera_pos_script),
        def("restore_camera_look", &restore_camera_look_script),
		
		
		def("flight_clear", &flight_path_clear_script),
        def("flight_add_point", &flight_path_add_point_script),
        def("flight_start", &flight_start_script),
        
        def("play_hud_anim", &force_play_hud_anim_script),
        def("actor_fire_start", &actor_fire_start_script),
        def("actor_fire_stop", &actor_fire_stop_script),
        
        def("play_sound_3d", &play_sound_3d_script),
        def("stop_custom_sounds", &stop_all_custom_sounds_script),
		
		// === NOIR ENGINE: Cinematic EFX Control ===
        def("set_efx_override", &set_efx_override_script),
        def("set_efx_preset", &set_efx_preset_script),
        def("disable_efx_override", &disable_efx_override_script),
		
		def("show_cinematic_borders", &show_cinematic_borders_script),
        def("hide_cinematic_borders", &hide_cinematic_borders_script),
        // ==========================================
		
        def("add_call",
            ((void (*)(const luabind::functor<bool>&, const luabind::functor<void>&)) & add_call)),
        def("add_call", ((void (*)(const luabind::object&, const luabind::functor<bool>&,
                                   const luabind::functor<void>&)) &
                         add_call)),
        def("add_call", ((void (*)(const luabind::object&, LPCSTR, LPCSTR)) & add_call)),
        def("remove_call",
            ((void (*)(const luabind::functor<bool>&, const luabind::functor<void>&)) &
             remove_call)),
        def("remove_call", ((void (*)(const luabind::object&, const luabind::functor<bool>&,
                                      const luabind::functor<void>&)) &
                            remove_call)),
        def("remove_call", ((void (*)(const luabind::object&, LPCSTR, LPCSTR)) & remove_call)),
        def("remove_calls_for_object", remove_calls_for_object), def("present", is_level_present),
        def("disable_input", disable_input), def("enable_input", enable_input),
        
        def("spawn_lightning", &spawn_lightning_at_pos),
        // --------------------------------------------------

        def("spawn_phantom", spawn_phantom),

        def("get_bounding_volume", get_bounding_volume),

        def("iterate_sounds", &iterate_sounds1), def("iterate_sounds", &iterate_sounds2),
        def("physics_world", &physics_world_scripted), def("get_snd_volume", &get_snd_volume),
        def("set_snd_volume", &set_snd_volume), def("add_cam_effector", &add_cam_effector),
        def("add_cam_effector2", &add_cam_effector2),
        def("remove_cam_effector", &remove_cam_effector), def("add_pp_effector", &add_pp_effector),
        def("set_pp_effector_factor", &set_pp_effector_factor),
        def("set_pp_effector_factor", &set_pp_effector_factor2),
        def("remove_pp_effector", &remove_pp_effector),

        def("add_complex_effector", &add_complex_effector),
        def("remove_complex_effector", &remove_complex_effector),

        def("vertex_id", &vertex_id),

        def("game_id", &GameID)],

        module(L, "actor_stats")[def("add_points", &add_actor_points),
                                 def("add_points_str", &add_actor_points_str),
                                 def("get_points", &get_actor_points)];

    module(
        L)[def("command_line", &command_line), def("IsGameTypeSingle", &IsGameTypeSingle),
           def("IsDynamicMusic", &IsDynamicMusic), def("render_get_dx_level", &render_get_dx_level),
           def("IsImportantSave", &IsImportantSave)];

    module(
        L,
        "relation_registry")[def("community_goodwill", &g_community_goodwill),
                             def("set_community_goodwill", &g_set_community_goodwill),
                             def("change_community_goodwill", &g_change_community_goodwill),

                             def("community_relation", &g_get_community_relation),
                             def("set_community_relation", &g_set_community_relation),
                             def("get_general_goodwill_between", &g_get_general_goodwill_between)];
    module(L, "game")
        [class_<xrTime>("CTime")
             .enum_("date_format")[value("DateToDay", int(InventoryUtilities::edpDateToDay)),
                                   value("DateToMonth", int(InventoryUtilities::edpDateToMonth)),
                                   value("DateToYear", int(InventoryUtilities::edpDateToYear))]
             .enum_(
                 "time_format")[value("TimeToHours", int(InventoryUtilities::etpTimeToHours)),
                                value("TimeToMinutes", int(InventoryUtilities::etpTimeToMinutes)),
                                value("TimeToSeconds", int(InventoryUtilities::etpTimeToSeconds)),
                                value("TimeToMilisecs", int(InventoryUtilities::etpTimeToMilisecs))]
             .def(constructor<>())
             .def(constructor<const xrTime&>())
             .def(const_self < xrTime())
             .def(const_self <= xrTime())
             .def(const_self > xrTime())
             .def(const_self >= xrTime())
             .def(const_self == xrTime())
             .def(self + xrTime())
             .def(self - xrTime())

             .def("diffSec", &xrTime::diffSec_script)
             .def("add", &xrTime::add_script)
             .def("sub", &xrTime::sub_script)

             .def("setHMS", &xrTime::setHMS)
             .def("setHMSms", &xrTime::setHMSms)
             .def("set", &xrTime::set)
             .def("get", &xrTime::get,
                  out_value<2>() + out_value<3>() + out_value<4>() + out_value<5>() +
                      out_value<6>() + out_value<7>() + out_value<8>())
             .def("dateToString", &xrTime::dateToString)
             .def("timeToString", &xrTime::timeToString),
         // declarations
         def("time", get_time), def("get_game_time", get_time_struct),
         //		def("get_surge_time",	Game::get_surge_time),
         //		def("get_object_by_name",Game::get_object_by_name),

         def("start_tutorial", &start_tutorial), def("stop_tutorial", &stop_tutorial),
         def("has_active_tutorial", &has_active_tutotial),
         def("translate_string", &translate_string)

    ];
}