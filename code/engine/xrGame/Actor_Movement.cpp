#include "stdafx.h"

#include "actor.h"
#include "inventory.h"
#include "weapon.h"
#include "../xrEngine/CameraBase.h"
#include "xrMessages.h"

#include "level.h"
#include "UIGameCustom.h"
#include "string_table.h"
#include "actorcondition.h"
#include "game_cl_base.h"
#include "WeaponMagazined.h"
#include "CharacterPhysicsSupport.h"
#include "actoreffector.h"
#include "static_cast_checked.hpp"
#include "player_hud.h"

#ifdef DEBUG
#include "phdebug.h"
#endif

constexpr float s_fLandingTime1 = 0.1f; 
constexpr float s_fLandingTime2 = 0.3f; 
constexpr float s_fJumpTime = 0.3f;
constexpr float s_fJumpGroundTime = 0.1f; 
extern const float s_fFallTime = 0.2f;

IC static void generate_orthonormal_basis1(const Fvector& dir, Fvector& updir, Fvector& right) {
    right.crossproduct(dir, updir); 
    right.normalize();
    updir.crossproduct(right, dir);
}

void CActor::g_cl_ValidateMState(float dt, u32 mstate_wf) {
    // Lookout
    if (mstate_wf & mcLookout)
        mstate_real |= mstate_wf & mcLookout;
    else
        mstate_real &= ~mcLookout;

    if (mstate_real & (mcJump | mcFall | mcLanding | mcLanding2))
        mstate_real &= ~mcLookout;

    if (mstate_real & (mcLanding | mcLanding2)) {
        m_fLandingTime -= dt;
        if (m_fLandingTime <= 0.f) {
            mstate_real &= ~(mcLanding | mcLanding2);
            mstate_real &= ~(mcFall | mcJump);
        }
    }
    
    if (character_physics_support()->movement()->gcontact_Was) {
        if (mstate_real & mcFall) {
            if (character_physics_support()->movement()->GetContactSpeed() > 4.f) {
                if (fis_zero(character_physics_support()->movement()->gcontact_HealthLost)) {
                    m_fLandingTime = s_fLandingTime1;
                    mstate_real |= mcLanding;
                } else {
                    m_fLandingTime = s_fLandingTime2;
                    mstate_real |= mcLanding2;
                }
            }
        }
        m_bJumpKeyPressed = TRUE;
        m_fJumpTime = s_fJumpTime;
        mstate_real &= ~(mcFall | mcJump);
    }
    if ((mstate_wf & mcJump) == 0)
        m_bJumpKeyPressed = FALSE;

    if (((character_physics_support()->movement()->GetVelocityActual() < 0.2f) &&
         (!(mstate_real & (mcFall | mcJump)))) ||
        character_physics_support()->movement()->bSleep) {
        mstate_real &= ~mcAnyMove;
    }
    if (character_physics_support()->movement()->Environment() == CPHMovementControl::peOnGround ||
        character_physics_support()->movement()->Environment() == CPHMovementControl::peAtWall) {
        if (((s_fJumpTime - m_fJumpTime) > s_fJumpGroundTime) && (mstate_real & mcJump)) {
            mstate_real &= ~mcJump;
            m_fJumpTime = s_fJumpTime;
        }
    }
    if (character_physics_support()->movement()->Environment() == CPHMovementControl::peAtWall) {
        if (!(mstate_real & mcClimb)) {
			Msg("~ DEBUG [LADDER]: Актор отримав статус mcClimb!");
            mstate_real |= mcClimb;
            mstate_real &= ~mcSprint;
            cam_SetLadder();
        }
    } else {
        if (mstate_real & mcClimb) {
			Msg("~ DEBUG [LADDER]: Актор ВТРАТИВ статус mcClimb (відірвався від драбини)");
            cam_UnsetLadder();
        }
        mstate_real &= ~mcClimb;
    }

    if (mstate_wf != mstate_real) {
        if ((mstate_real & mcCrouch) && ((0 == (mstate_wf & mcCrouch)) || mstate_real & mcClimb)) {
            if (character_physics_support()->movement()->ActivateBoxDynamic(0)) {
                mstate_real &= ~mcCrouch;
            }
        }
    }

    if (!CanAccelerate() && isActorAccelerated(mstate_real, IsZoomAimingMode())) {
        mstate_real ^= mcAccel;
    }

    if (this == Level().CurrentControlEntity()) {
        bool bOnClimbNow = !!(mstate_real & mcClimb);
        bool bOnClimbOld = !!(mstate_old & mcClimb);

        if (bOnClimbNow != bOnClimbOld) {
            SetWeaponHideState(INV_STATE_LADDER, bOnClimbNow);
        }
    }
}

void CActor::g_cl_CheckControls(u32 mstate_wf, Fvector& vControlAccel, float& Jump, float dt) {
    float cam_eff_factor = 0.0f;
    mstate_old = mstate_real;
    vControlAccel.set(0, 0, 0);

    if (!(mstate_real & mcFall) &&
        (character_physics_support()->movement()->Environment() == CPHMovementControl::peInAir)) {
        m_fFallTime -= dt;
        if (m_fFallTime <= 0.f) {
            m_fFallTime = s_fFallTime;
            mstate_real |= mcFall;
            mstate_real &= ~mcJump;
        }
    }

    if (!CanMove()) {
        if (mstate_wf & mcAnyMove) {
            StopAnyMove();
            mstate_wf &= ~mcAnyMove;
            mstate_wf &= ~mcJump;
        }
    }
    
    if (mstate_wf & mcFwd) vControlAccel.z += 1;
    if (mstate_wf & mcBack) vControlAccel.z += -1;
    if (mstate_wf & mcLStrafe) vControlAccel.x += -1;
    if (mstate_wf & mcRStrafe) vControlAccel.x += 1;

    CPHMovementControl::EEnvironment curr_env = character_physics_support()->movement()->Environment();
    
    if (curr_env == CPHMovementControl::peOnGround || curr_env == CPHMovementControl::peAtWall) {
        if ((0 == (mstate_real & mcCrouch)) && (mstate_wf & mcCrouch)) {
            if (mstate_real & mcClimb) {
                mstate_wf &= ~mcCrouch;
            } else {
                character_physics_support()->movement()->EnableCharacter();
                bool Crouched = false;
                if (isActorAccelerated(mstate_wf, IsZoomAimingMode()))
                    Crouched = character_physics_support()->movement()->ActivateBoxDynamic(1);
                else
                    Crouched = character_physics_support()->movement()->ActivateBoxDynamic(2);

                if (Crouched)
                    mstate_real |= mcCrouch;
            }
        }
        
        m_fJumpTime -= dt;

        if (CanJump() && (mstate_wf & mcJump)) {
            mstate_real |= mcJump;
            m_bJumpKeyPressed = TRUE;
            Jump = m_fJumpSpeed;
            m_fJumpTime = s_fJumpTime;

            if (!GodMode())
                conditions().ConditionJump(inventory().TotalWeight() / MaxCarryWeight());
        }

        u32 move = mcAnyMove | mcAccel;

        if (mstate_real & mcCrouch) {
            if (!isActorAccelerated(mstate_real, IsZoomAimingMode()) &&
                isActorAccelerated(mstate_wf, IsZoomAimingMode())) {
                character_physics_support()->movement()->EnableCharacter();
                if (!character_physics_support()->movement()->ActivateBoxDynamic(1))
                    move &= ~mcAccel;
            }

            if (isActorAccelerated(mstate_real, IsZoomAimingMode()) &&
                !isActorAccelerated(mstate_wf, IsZoomAimingMode())) {
                character_physics_support()->movement()->EnableCharacter();
                if (character_physics_support()->movement()->ActivateBoxDynamic(2))
                    mstate_real &= ~mcAccel;
            }
        }

        if ((mstate_wf & mcSprint) && !CanSprint())
            mstate_wf &= ~mcSprint;

        mstate_real &= (~move);
        mstate_real |= (mstate_wf & move);

        if (mstate_wf & mcSprint)
            mstate_real |= mcSprint;
        else
            mstate_real &= ~mcSprint;
            
        if (!(mstate_real & (mcFwd | mcLStrafe | mcRStrafe)) ||
            mstate_real & (mcCrouch | mcClimb) ||
            !isActorAccelerated(mstate_wf, IsZoomAimingMode())) {
            mstate_real &= ~mcSprint;
            mstate_wishful &= ~mcSprint;
        }

        if (mstate_real & mcAnyMove) {
            BOOL bAccelerated = isActorAccelerated(mstate_real, IsZoomAimingMode()) && CanAccelerate();

            if (xr::abs(vControlAccel.z) < EPS)
                mstate_real &= ~(mcFwd + mcBack);
            if (xr::abs(vControlAccel.x) < EPS)
                mstate_real &= ~(mcLStrafe + mcRStrafe);

            float scale = vControlAccel.magnitude();
            if (scale > EPS) {
                scale = m_fWalkAccel / scale;
                if (bAccelerated) {
                    if (mstate_real & mcBack) scale *= m_fRunBackFactor;
                    else scale *= m_fRunFactor;
                } else if (mstate_real & mcBack) {
                    scale *= m_fWalkBackFactor;
                }

                if (mstate_real & mcCrouch) scale *= m_fCrouchFactor;
                if (mstate_real & mcClimb) scale *= m_fClimbFactor;
                if (mstate_real & mcSprint) scale *= m_fSprintFactor;

                if (mstate_real & (mcLStrafe | mcRStrafe) && !(mstate_real & mcCrouch)) {
                    if (bAccelerated) scale *= m_fRun_StrafeFactor;
                    else scale *= m_fWalk_StrafeFactor;
                }

                vControlAccel.mul(scale);
                cam_eff_factor = scale;
            } 
        }     
    }         

    if (IsGameTypeSingle() && cam_eff_factor > EPS) {
        LPCSTR state_anm = nullptr;

        if (mstate_real & mcSprint && !(mstate_old & mcSprint))
            state_anm = "sprint";
        else if (mstate_real & mcLStrafe && !(mstate_old & mcLStrafe))
            state_anm = "strafe_left";
        else if (mstate_real & mcRStrafe && !(mstate_old & mcRStrafe))
            state_anm = "strafe_right";
        else if (mstate_real & mcFwd && !(mstate_old & mcFwd))
            state_anm = "move_fwd";
        else if (mstate_real & mcBack && !(mstate_old & mcBack))
            state_anm = "move_back";

        if (state_anm) { 
            CActor* control_entity = static_cast_checked<CActor*>(Level().CurrentControlEntity());
            R_ASSERT2(control_entity, "current control entity is NULL");
            CEffectorCam* ec = control_entity->Cameras().GetCamEffector(eCEActorMoving);
            if (!ec) {
                string_path eff_name;
                xr_sprintf(eff_name, sizeof(eff_name), "%s.anm", state_anm);
                string_path ce_path;
                string_path anm_name;
                strconcat(sizeof(anm_name), anm_name, "camera_effects\\actor_move\\", eff_name);
                
                if (FS.exist(ce_path, "$game_anims$", anm_name)) {
                    CAnimatorCamLerpEffectorConst* e = xr_new<CAnimatorCamLerpEffectorConst>();
                    float max_scale = 70.0f;
                    float factor = cam_eff_factor / max_scale;
                    e->SetFactor(factor);
                    e->SetType(eCEActorMoving);
                    e->SetHudAffect(false);
                    e->SetCyclic(false);
                    e->Start(anm_name);
                    control_entity->Cameras().AddCamEffector(e);
                }
            }
        }
    }
    
    Fmatrix mOrient;
    mOrient.rotateY(-r_model_yaw);
    mOrient.transform_dir(vControlAccel);
}

constexpr float ACTOR_LLOOKOUT_ANGLE = PI_DIV_4;
constexpr float ACTOR_RLOOKOUT_ANGLE = PI_DIV_4;

void CActor::g_Orientate(u32 mstate_rl, float dt) {
    static float fwd_l_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "fwd_l_strafe_yaw"));
    static float back_l_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "back_l_strafe_yaw"));
    static float fwd_r_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "fwd_r_strafe_yaw"));
    static float back_r_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "back_r_strafe_yaw"));
    static float l_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "l_strafe_yaw"));
    static float r_strafe_yaw = deg2rad(pSettings->r_float("actor_animation", "r_strafe_yaw"));

    if (!g_Alive()) return;
    
    float calc_yaw = 0;
    if (mstate_real & mcClimb) {
        if (g_LadderOrient()) return;
    }
    
    switch (mstate_rl & mcAnyMove) {
    case mcFwd + mcLStrafe: calc_yaw = +fwd_l_strafe_yaw; break;
    case mcBack + mcRStrafe: calc_yaw = +back_r_strafe_yaw; break;
    case mcFwd + mcRStrafe: calc_yaw = -fwd_r_strafe_yaw; break;
    case mcBack + mcLStrafe: calc_yaw = -back_l_strafe_yaw; break;
    case mcLStrafe: calc_yaw = +l_strafe_yaw; break;
    case mcRStrafe: calc_yaw = -r_strafe_yaw; break;
    }

    angle_lerp(r_model_yaw_delta, calc_yaw, PI_MUL_4, dt);

    Fmatrix mXFORM;
    mXFORM.rotateY(-(r_model_yaw + r_model_yaw_delta));
    mXFORM.c.set(Position());
    XFORM().set(mXFORM);
    VERIFY(xr::valid(XFORM()));

    float tgt_roll = 0.f;
    if (mstate_rl & mcLookout) {
        tgt_roll = (mstate_rl & mcLLookout) ? -ACTOR_LLOOKOUT_ANGLE : ACTOR_RLOOKOUT_ANGLE;

        if ((mstate_rl & mcLLookout) && (mstate_rl & mcRLookout))
            tgt_roll = 0.0f;
    }
    
    if (!fsimilar(tgt_roll, r_torso_tgt_roll, EPS)) {
        angle_lerp(r_torso_tgt_roll, tgt_roll, PI_MUL_2, dt);
        r_torso_tgt_roll = angle_normalize_signed(r_torso_tgt_roll);
    }
}

bool CActor::g_LadderOrient() {
    Fvector leader_norm;
    character_physics_support()->movement()->GroundNormal(leader_norm);
    if (xr::abs(leader_norm.y) > M_SQRT1_2) return false;
    
    float mag = leader_norm.magnitude();
    if (mag < EPS_L) return false;
    
    leader_norm.div(mag);
    leader_norm.invert();
    Fmatrix M;
    M.set(Fidentity);
    M.k.set(leader_norm);
    M.j.set(0.f, 1.f, 0.f);
    generate_orthonormal_basis1(M.k, M.j, M.i);
    M.i.invert();
    
    Fvector position;
    position.set(Position());
    VERIFY2(xr::valid(M), "Invalid matrix in g_LadderOrient");
    XFORM().set(M);
    VERIFY2(xr::valid(position), "Invalid position in g_LadderOrient");
    Position().set(position);
    VERIFY(xr::valid(XFORM()));
    return true;
}

void CActor::g_cl_Orientate(u32 mstate_rl, float dt) {
    if (eacFreeLook != cam_active) {
        r_torso.yaw = cam_Active()->GetWorldYaw();
        r_torso.pitch = cam_Active()->GetWorldPitch();
    } else {
        r_torso.yaw = cam_FirstEye()->GetWorldYaw();
        r_torso.pitch = cam_FirstEye()->GetWorldPitch();
    }

    unaffected_r_torso.yaw = r_torso.yaw;
    unaffected_r_torso.pitch = r_torso.pitch;
    unaffected_r_torso.roll = r_torso.roll;

    CWeaponMagazined* pWM = smart_cast<CWeaponMagazined*>(inventory().GetActiveSlot() != NO_ACTIVE_SLOT
                                          ? inventory().ItemFromSlot(inventory().GetActiveSlot())
                                          : nullptr);
                                          
    if (pWM && pWM->GetCurrentFireMode() == 1 && eacFirstEye != cam_active) {
        Fvector dangle = weapon_recoil_last_delta();
        r_torso.yaw = unaffected_r_torso.yaw + dangle.y;
        r_torso.pitch = unaffected_r_torso.pitch + dangle.x;
    }

    if (mstate_rl & mcAnyMove) {
        r_model_yaw = angle_normalize(r_torso.yaw);
        mstate_real &= ~mcTurn;
    } else {
        float ty = angle_normalize(r_torso.yaw);
        if (xr::abs(r_model_yaw - ty) > PI_DIV_4) {
            r_model_yaw_dest = ty;
            mstate_real |= mcTurn;
        }
        if (xr::abs(r_model_yaw - r_model_yaw_dest) < EPS_L) {
            mstate_real &= ~mcTurn;
        }
        if (mstate_rl & mcTurn) {
            angle_lerp(r_model_yaw, r_model_yaw_dest, PI_MUL_2, dt);
        }
    }
}

void CActor::g_sv_Orientate(u32 /**mstate_rl/**/, float /**dt/**/) {
    r_model_yaw = NET_Last.o_model;

    r_torso.yaw = unaffected_r_torso.yaw;
    r_torso.pitch = unaffected_r_torso.pitch;
    r_torso.roll = unaffected_r_torso.roll;

    CWeaponMagazined* pWM = smart_cast<CWeaponMagazined*>(inventory().GetActiveSlot() != NO_ACTIVE_SLOT
                                          ? inventory().ItemFromSlot(inventory().GetActiveSlot())
                                          : nullptr);
                                          
    if (pWM && pWM->GetCurrentFireMode() == 1) {
        Fvector dangle = weapon_recoil_last_delta();
        r_torso.yaw += dangle.y;
        r_torso.pitch += dangle.x;
        r_torso.roll += dangle.z;
    }
}

bool isActorAccelerated(u32 mstate, bool ZoomMode) {
    bool res = false;
    if (mstate & mcAccel) res = false;
    else res = true;

    if (mstate & (mcCrouch | mcClimb | mcJump | mcLanding | mcLanding2))
        return res;
    if (mstate & mcLookout || ZoomMode)
        return false;
    return res;
}

bool CActor::CanAccelerate() {
    bool can_accel = !conditions().IsLimping() &&
                     !character_physics_support()->movement()->PHCapture() &&
                     (m_time_lock_accel < Device.dwTimeGlobal);
    return can_accel;
}

bool CActor::CanRun() {
    bool can_run = !IsZoomAimingMode() && !(mstate_real & mcLookout);
    return can_run;
}

bool CActor::CanSprint() {
    bool can_Sprint = CanAccelerate() && !conditions().IsCantSprint() && Game().PlayerCanSprint(this) &&
                      CanRun() && !(mstate_real & mcLStrafe || mstate_real & mcRStrafe) && InventoryAllowSprint();

    return can_Sprint && (m_block_sprint_counter <= 0);
}

bool CActor::CanJump() {
    bool can_Jump = !character_physics_support()->movement()->PHCapture() &&
                    ((mstate_real & mcJump) == 0) && (m_fJumpTime <= 0.f) && !m_bJumpKeyPressed &&
                    !IsZoomAimingMode();
    return can_Jump;
}

bool CActor::CanMove() {
    if (conditions().IsCantWalk()) {
        if (mstate_wishful & mcAnyMove) {
            CurrentGameUI()->AddCustomStatic("cant_walk", true);
        }
        return false;
    } else if (conditions().IsCantWalkWeight()) {
        if (mstate_wishful & mcAnyMove) {
            CurrentGameUI()->AddCustomStatic("cant_walk_weight", true);
        }
        return false;
    }

    if (IsTalking()) return false;
    else return true;
}

void CActor::StopAnyMove() {
    mstate_wishful &= ~mcAnyMove;
    mstate_real &= ~mcAnyMove;

    if (this == Level().CurrentViewEntity()) {
        g_player_hud->OnMovementChanged((EMoveCommand)0);
    }
}

bool CActor::is_jump() { return ((mstate_real & (mcJump | mcFall | mcLanding | mcLanding2)) != 0); }

#include "CustomOutfit.h"
#include "UserBackpack.h"
#include "NoirInventorySlots.h"
float CActor::MaxCarryWeight() const {
    float res = inventory().GetMaxWeight();
    res += get_additional_weight();
    return res;
}

float CActor::MaxWalkWeight() const {
    float max_w = CActor::conditions().MaxWalkWeight();
    max_w += get_additional_weight();
    return max_w;
}

#include "artefact.h"
float CActor::get_additional_weight() const {
    float res = 0.0f;
    CCustomOutfit* outfit = GetOutfit();
    if (outfit) {
        res += outfit->m_additional_weight;
    }
 
    if (NoirInventorySlots::BackpackEnabled())
    {
        CBackpack* backpack = smart_cast<CBackpack*>(inventory().ItemFromSlot(BACKPACK_SLOT));
        if (backpack) {
            res += backpack->m_additional_weight;
        }
    }

    for (TIItemContainer::const_iterator it = inventory().m_belt.begin();
         inventory().m_belt.end() != it; ++it) {
        CArtefact* artefact = smart_cast<CArtefact*>(*it);
        if (artefact)
            res += artefact->AdditionalInventoryWeight();
    }

    return res;
}