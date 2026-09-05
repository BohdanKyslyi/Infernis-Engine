#include "stdafx.h"
#include "spectator.h"
#include "effectorfall.h"
#include "CameraLook.h"
#include "spectator_camera_first_eye.h"
#include "actor.h"
#include "xrServer_Objects.h"
#include "game_cl_base.h"
#include "level.h"
#include "xr_level_controller.h"
#include "seniority_hierarchy_holder.h"
#include "team_hierarchy_holder.h"
#include "squad_hierarchy_holder.h"
#include "group_hierarchy_holder.h"
#include "../xrEngine/CameraManager.h"
#include "Inventory.h"
#include "huditem.h"
#include "string_table.h"
#include "map_manager.h"
#include <algorithm>

constexpr float START_ACCEL = 16.0f;
static float Accel_mul = START_ACCEL;

CSpectator::CSpectator() : CGameObject() 
{
    m_timer.Start();

    cameras[eacFirstEye] = xr_new<CCameraFirstEye>(this);
    cameras[eacFirstEye]->Load("actor_firsteye_cam");

    cameras[eacLookAt] = xr_new<CCameraLook>(this);
    cameras[eacLookAt]->Load("actor_look_cam");

    cameras[eacFreeLook] = xr_new<CCameraLook>(this);
    cameras[eacFreeLook]->Load("actor_free_cam");

    cameras[eacFreeFly] = xr_new<CSpectrCameraFirstEye>(m_fTimeDelta, this, 0);
    cameras[eacFreeFly]->Load("actor_firsteye_cam");

    cameras[eacFixedLookAt] = xr_new<CCameraFixedLook>(this);
    cameras[eacFixedLookAt]->Load("actor_look_cam");
}

CSpectator::~CSpectator() 
{
    for (auto*& cam : cameras) {
        xr_delete(cam);
    }
}

void CSpectator::UpdateCL() 
{
    inherited::UpdateCL();

    float fPreviousFrameTime = m_timer.GetElapsed_sec();
    m_timer.Start();
    
    m_fTimeDelta = 0.3f * m_fTimeDelta + 0.7f * fPreviousFrameTime;
    m_fTimeDelta = std::clamp(m_fTimeDelta, EPS_S, 0.1f); 

    if (Device.Paused()) {
#ifdef DEBUG
        dbg_update_cl = 0;
#endif
        if (m_pActorToLookAt) {
#ifdef DEBUG
            m_pActorToLookAt->dbg_update_cl = 0;
            m_pActorToLookAt->dbg_update_shedule = 0;
            Game().dbg_update_shedule = 0;
#endif
            Device.dwTimeDelta = 0;
            m_pActorToLookAt->UpdateCL();
            m_pActorToLookAt->shedule_Update(0);
            Game().shedule_Update(0);
#ifdef DEBUG
            m_pActorToLookAt->dbg_update_cl = 0;
            m_pActorToLookAt->dbg_update_shedule = 0;
            Game().dbg_update_shedule = 0;
#endif
        }
    }

    if (GameID() != eGameIDSingle) {
        if (Game().local_player && ((Game().local_player->GameID == ID()) || Level().IsDemoPlay())) {
            if (cam_active != eacFreeFly) {
                if (m_pActorToLookAt && !m_pActorToLookAt->g_Alive())
                    cam_Set(eacFreeLook);
                
                if (!m_pActorToLookAt) {
                    SelectNextPlayerToLook(false);
                    if (m_pActorToLookAt) cam_Set(m_last_camera);
                }
            }
            if (Level().CurrentViewEntity() == this) {
                cam_Update(m_pActorToLookAt);
            }
            return;
        }
    }

    if (g_pGameLevel->CurrentViewEntity() == this) {
        if (eacFreeFly != cam_active) {
            int idx = 0;
            if (game_PlayerState* P = Game().local_player; P && (P->team >= 0) && (P->team < (int)Level().seniority_holder().teams().size())) {
                const CTeamHierarchyHolder& T = Level().seniority_holder().team(P->team);
                for (u32 i = 0; i < T.squads().size(); ++i) {
                    const CSquadHierarchyHolder& S = T.squad(i);
                    for (u32 j = 0; j < S.groups().size(); ++j) {
                        const CGroupHierarchyHolder& G = S.group(j);
                        for (u32 k = 0; k < G.members().size(); ++k) {
                            if (CActor* A = smart_cast<CActor*>(G.members()[k]); A) {
                                if (idx == look_idx) {
                                    cam_Update(A);
                                    return;
                                }
                                ++idx;
                            }
                        }
                    }
                }
            }
            look_idx = 0;
            if (0 == idx) cam_Set(eacFreeFly);
        }
        cam_Update(0);
    }
}

void CSpectator::shedule_Update(u32 DT) 
{
    inherited::shedule_Update(DT);
    if (!Ready()) return;
}

void CSpectator::IR_OnKeyboardPress(int cmd) 
{
    if (Remote()) return;

    switch (cmd) {
    case kACCEL:
        Accel_mul = START_ACCEL * 2;
        break;
    case kCAM_1:
        if (cam_active == eacFreeFly && SelectNextPlayerToLook(false)) cam_Set(eacFirstEye);
        break;
    case kCAM_2:
        if (cam_active == eacFreeFly && SelectNextPlayerToLook(false)) cam_Set(eacLookAt);
        break;
    case kCAM_3:
        if (cam_active == eacFreeFly && SelectNextPlayerToLook(false)) cam_Set(eacFreeLook);
        break;
    case kWPN_FIRE:
        if ((cam_active != eacFreeFly) || (!m_pActorToLookAt)) {
            ++look_idx;
            SelectNextPlayerToLook(true);
            if (cam_active == eacFirstEye && m_pActorToLookAt)
                FirstEye_ToPlayer(m_pActorToLookAt);
        }
        break;
    case kWPN_ZOOM:
        break;
    }
}

void CSpectator::IR_OnKeyboardRelease(int cmd) 
{
    if (cmd == kACCEL) Accel_mul = START_ACCEL;
}

void CSpectator::IR_OnKeyboardHold(int cmd) 
{
    if (Remote()) return;

    if ((cam_active == eacFreeFly) || (cam_active == eacFreeLook)) {
        CCameraBase* C = cameras[cam_active];
        Fvector vmove = { 0, 0, 0 };
        
        switch (cmd) {
        case kUP:
        case kDOWN:
        case kCAM_ZOOM_IN:
        case kCAM_ZOOM_OUT:
            cameras[cam_active]->Move(cmd);
            break;
        case kLEFT:
        case kRIGHT:
            if (eacFreeLook != cam_active) cameras[cam_active]->Move(cmd);
            break;
        case kFWD:
            vmove.mad(C->vDirection, m_fTimeDelta * Accel_mul);
            break;
        case kBACK:
            vmove.mad(C->vDirection, -m_fTimeDelta * Accel_mul);
            break;
        case kR_STRAFE: {
            Fvector right;
            right.crossproduct(C->vNormal, C->vDirection);
            vmove.mad(right, m_fTimeDelta * Accel_mul);
            break;
        }
        case kL_STRAFE: {
            Fvector right;
            right.crossproduct(C->vNormal, C->vDirection);
            vmove.mad(right, -m_fTimeDelta * Accel_mul);
            break;
        }
        }
        
        game_PlayerState* PS = Game().local_player;
        if (cam_active != eacFreeFly || (PS && PS->testFlag(GAME_PLAYER_FLAG_SPECTATOR))) {
            XFORM().c.add(vmove);
        }
    }
}

void CSpectator::IR_OnMouseMove(int dx, int dy) 
{
    if (Remote()) return;
    
    CCameraBase* C = cameras[cam_active];
    float scale = (C->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.f;
    
    if (dx) {
        float d = float(dx) * scale;
        cameras[cam_active]->Move((d < 0) ? kLEFT : kRIGHT, xr::abs(d));
    }
    if (dy) {
        float d = ((psMouseInvert.test(1)) ? -1.f : 1.f) * float(dy) * scale * 3.f / 4.f;
        cameras[cam_active]->Move((d > 0) ? kUP : kDOWN, xr::abs(d));
    }
}

void CSpectator::FirstEye_ToPlayer(CObject* pObject) 
{
    CObject* pCurViewEntity = Level().CurrentEntity();
    CActor* pOldActor = nullptr;

    if (pCurViewEntity) {
        if (pOldActor = smart_cast<CActor*>(pCurViewEntity); pOldActor) {
            pOldActor->inventory().Items_SetCurrentEntityHud(false);
        }
        if (smart_cast<CSpectator*>(pCurViewEntity)) {
            Engine.Sheduler.Unregister(pCurViewEntity);
            Engine.Sheduler.Register(pCurViewEntity, TRUE);
        }
    }

    if (pObject) {
        Level().SetEntity(pObject);
        Engine.Sheduler.Unregister(pObject);
        Engine.Sheduler.Register(pObject, TRUE);

        if (CActor* pActor = smart_cast<CActor*>(pObject); pActor) {
            pActor->inventory().Items_SetCurrentEntityHud(true);
        }
    }

    if (Device.Paused() && pOldActor) {
#ifdef DEBUG
        pOldActor->dbg_update_cl = 0;
        pOldActor->dbg_update_shedule = 0;
#endif
        Device.dwTimeDelta = 0;
        pOldActor->UpdateCL();
        pOldActor->shedule_Update(0);
#ifdef DEBUG
        pOldActor->dbg_update_cl = 0;
        pOldActor->dbg_update_shedule = 0;
#endif
    }
}

void CSpectator::cam_Set(EActorCameras style) 
{
    CCameraBase* old_cam = cameras[cam_active];
    
    if (style == eacFirstEye) FirstEye_ToPlayer(m_pActorToLookAt);
    if (cam_active == eacFirstEye) FirstEye_ToPlayer(this);
    
    cam_active = style;
    old_cam->OnDeactivate();
    cameras[cam_active]->OnActivate(old_cam);
}

void CSpectator::cam_Update(CActor* A) 
{
    if (A) {
        const Fmatrix& M = A->XFORM();
        CCameraBase* pACam = A->cam_Active();
        CCameraBase* cam = cameras[cam_active];
        
        switch (cam_active) {
        case eacFirstEye: {
            Fvector P, D, N;
            pACam->Get(P, D, N);
            cam->Set(P, D, N);
            break;
        }
        case eacLookAt: {
            float y, p, r;
            M.getHPB(y, p, r);
            cam->Set(pACam->yaw, pACam->pitch, -r);
            [[fallthrough]]; 
        }
        case eacFreeLook: {
            cam->SetParent(A);
            Fmatrix tmp;
            tmp.identity();

            Fvector point{0.f, 1.6f, 0.f}, point1{0.f, 1.6f, 0.f}, dangle;
            M.transform_tiny(point);
            tmp.translate_over(point);
            tmp.transform_tiny(point1);
            if (!A->g_Alive()) point.set(point1);
            
            cam->Update(point, dangle);
            break;
        }
        default:
            break;
        }
        
        Fvector P, D, N;
        cam->Get(P, D, N);
        cameras[eacFreeFly]->Set(P, D, N);
        cameras[eacFreeFly]->Set(cam->yaw, cam->pitch, 0);
        P.y -= 1.6f;
        XFORM().translate_over(P);
        
        if (Device.Paused()) {
            Device.fTimeDelta = m_fTimeDelta; 
            g_pGameLevel->Cameras().UpdateFromCamera(cam);
            Device.fTimeDelta = 0.0f; 
        } else {
            g_pGameLevel->Cameras().UpdateFromCamera(cam);
        }
    } else {
        CCameraBase* cam = (cam_active == eacFixedLookAt) ? cameras[eacFixedLookAt] : cameras[eacFreeFly];

        Fvector point{0.f, 1.6f, 0.f}, dangle{0, 0, 0};
        XFORM().transform_tiny(point);

        cam->Update(point, dangle);
        
        if (Device.Paused()) {
            Device.fTimeDelta = m_fTimeDelta;
            g_pGameLevel->Cameras().UpdateFromCamera(cam);
            Device.fTimeDelta = 0.0f; 
        } else {
            g_pGameLevel->Cameras().UpdateFromCamera(cam);
        }
    }
}

BOOL CSpectator::net_Spawn(CSE_Abstract* DC) 
{
    if (!inherited::net_Spawn(DC)) return FALSE;

    CSE_Abstract* E = smart_cast<CSE_Abstract*>(DC);
    if (!E) return FALSE;

    float tmp_roll = 0.f;
    cam_active = eacFreeFly;
    look_idx = 0;

    cameras[cam_active]->Set(-E->o_Angle.y, -E->o_Angle.x, tmp_roll);
    cameras[cam_active]->vPosition.set(E->o_Position);

    if (OnServer()) {
        E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
    }
    return TRUE;
}

void CSpectator::net_Destroy() 
{
    inherited::net_Destroy();
    Level().MapManager().OnObjectDestroyNotify(ID());
}

bool CSpectator::SelectNextPlayerToLook(bool const search_next) 
{
    if (GameID() == eGameIDSingle) return false;

    if (!Game().local_player) return false;
    m_pActorToLookAt = nullptr;

    u16 PPCount = 0;
    CActor* PossiblePlayers[32];
    int last_player_idx = -1;

    for (const auto& [id, ps] : Game().players) {
        if (!ps || ps->testFlag(GAME_PLAYER_FLAG_VERY_VERY_DEAD))
            continue;

        if (CObject* pObject = Level().Objects.net_Find(ps->GameID); pObject) {
            if (CActor* A = smart_cast<CActor*>(pObject); A) {
                if (m_last_player_name.size() && (m_last_player_name == ps->getName())) {
                    last_player_idx = PPCount;
                }
                PossiblePlayers[PPCount++] = A;
            }
        }
    }

    if (!search_next) {
        if (last_player_idx != -1) {
            m_pActorToLookAt = PossiblePlayers[last_player_idx];
            return true;
        }
        return false;
    }

    if (PPCount > 0) {
        look_idx %= PPCount;
        m_pActorToLookAt = PossiblePlayers[look_idx];
        if (game_PlayerState* tmp_state = Game().GetPlayerByGameID(m_pActorToLookAt->ID()); tmp_state) {
            m_last_player_name = tmp_state->getName();
        }
        return true;
    }
    return false;
}

void CSpectator::net_Relcase(CObject* O) 
{
    if (O != m_pActorToLookAt) return;

    if (m_pActorToLookAt != Level().CurrentEntity()) {
        m_pActorToLookAt = nullptr;
        return;
    }

    m_pActorToLookAt = nullptr;
    if (cam_active != eacFreeFly) {
        SelectNextPlayerToLook(false);
        if (m_pActorToLookAt == O) {
            m_pActorToLookAt = nullptr;
        }
    }
    if (!m_pActorToLookAt) cam_Set(eacFreeFly);
}

void CSpectator::GetSpectatorString(string1024& pStr) const 
{
    if (!pStr || GameID() == eGameIDSingle) return;

    xr_string SpectatorMsg;
    CStringTable st;
    
    SpectatorMsg = *st.translate("mp_spectator");
    SpectatorMsg += " ";

    switch (cam_active) {
    case eacFreeFly:
        SpectatorMsg += *st.translate("mp_free_fly");
        break;
    case eacFirstEye:
        SpectatorMsg += *st.translate("mp_first_eye");
        SpectatorMsg += " ";
        SpectatorMsg += m_pActorToLookAt ? m_pActorToLookAt->Name() : "";
        break;
    case eacFreeLook:
        SpectatorMsg += *st.translate("mp_free_look");
        SpectatorMsg += " ";
        SpectatorMsg += m_pActorToLookAt ? m_pActorToLookAt->Name() : "";
        break;
    case eacLookAt:
        SpectatorMsg += *st.translate("mp_look_at");
        SpectatorMsg += " ";
        SpectatorMsg += m_pActorToLookAt ? m_pActorToLookAt->Name() : "";
        break;
    default:
        break;
    }
    
    xr_strcpy(pStr, SpectatorMsg.c_str());
}

void CSpectator::On_SetEntity() 
{
    prev_cam_inert_value = psCamInert;
    psCamInert = cam_inert_value;
}

void CSpectator::On_LostEntity() 
{ 
    psCamInert = prev_cam_inert_value; 
}