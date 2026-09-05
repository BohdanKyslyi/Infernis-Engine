#include "stdafx.h"
#include "../xrEngine/gamemtllib.h"
#ifndef GMLibrary
#define GMLibrary() GMLib 
#endif
#include "Actor_Flags.h"
#include "hudmanager.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif 
#include "alife_space.h"
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "xrserver_objects_alife_monsters.h"
#include "CameraLook.h"
#include "CameraFirstEye.h"
#include "effectorfall.h"
#include "EffectorBobbing.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "SleepEffector.h"
#include "character_info.h"
#include "CustomOutfit.h"
#include "actorcondition.h"
#include "UIGameCustom.h"
#include "../xrphysics/matrix_utils.h"
#include "clsid_game.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "Grenade.h"
#include "Torch.h"

// breakpoints
#include "../xrEngine/xr_input.h"
//
#include "Actor.h"
#include "ActorAnimation.h"
#include "actor_anim_defs.h"
#include "HudItem.h"
#include "ai_sounds.h"
#include "ai_space.h"
#include "trade.h"
#include "inventory.h"
#include "level.h"
#include "GamePersistent.h"
#include "game_cl_base.h"
#include "game_cl_single.h"
#include "xrmessages.h"
#include "string_table.h"
#include "usablescriptobject.h"
#include "../xrEngine/cl_intersect.h"
#include "alife_registry_wrappers.h"
#include "xrRender/Kinematics.h"
#include "artefact.h"
#include "CharacterPhysicsSupport.h"
#include "material_manager.h"
#include "../xrphysics/IColisiondamageInfo.h"
#include "ui/UIMainIngameWnd.h"
#include "map_manager.h"
#include "GameTaskManager.h"
#include "actor_memory.h"
#include "Script_Game_Object.h"
#include "Game_Object_Space.h"
#include "script_callback_ex.h"
#include "InventoryBox.h"
#include "location_manager.h"
#include "player_hud.h"
#include "ItemUseController.h"
#include "ai/monsters/basemonster/base_monster.h"

#include "xrRender/UIRender.h"

#include "ai_object_location.h"
#include "ui/uiMotionIcon.h"
#include "ui/UIActorMenu.h"
#include "ActorHelmet.h"
#include "UI/UIDragDropReferenceList.h"
#include <algorithm> 

#include "../xrPhysics/PhysicsShell.h"
#include "../xrPhysics/PHElement.h"

constexpr u32 patch_frames = 50;
constexpr float respawn_delay = 1.f;
constexpr float respawn_auto = 7.f;

static float IReceived = 0.0f;
static float ICoincidenced = 0.0f;
extern float cammera_into_collision_shift;

string32 ACTOR_DEFS::g_quick_use_slots[4] = { NULL, NULL, NULL, NULL };

static Fbox bbStandBox;
static Fbox bbCrouchBox;
static Fvector vFootCenter;
static Fvector vFootExt;

Flags32 psActorFlags = { AF_GODMODE_RT | AF_AUTOPICKUP | AF_RUN_BACKWARD | AF_IMPORTANT_SAVE };
int psActorSleepTime = 1;

CActor::CActor() : CEntityAlive(), current_ik_cam_shift(0) {
    game_news_registry = xr_new<CGameNewsRegistryWrapper>();
    m_item_use = xr_new<CItemUseController>(this);
    
    cameras[eacFirstEye] = xr_new<CCameraFirstEye>(this);
    cameras[eacFirstEye]->Load("actor_firsteye_cam");

    psActorFlags.set(AF_PSP, strstr(Core.Params, "-psp") != nullptr);

    if (psActorFlags.test(AF_PSP)) {
        cameras[eacLookAt] = xr_new<CCameraLook2>(this);
        cameras[eacLookAt]->Load("actor_look_cam_psp");
    } else {
        cameras[eacLookAt] = xr_new<CCameraLook>(this);
        cameras[eacLookAt]->Load("actor_look_cam");
    }
    cameras[eacFreeLook] = xr_new<CCameraLook>(this);
    cameras[eacFreeLook]->Load("actor_free_cam");
    cameras[eacFixedLookAt] = xr_new<CCameraFixedLook>(this);
    cameras[eacFixedLookAt]->Load("actor_look_cam");

    cam_active = eacFirstEye;
    fPrevCamPos = 0.0f;
    vPrevCamDir.set(0.f, 0.f, 1.f);
    fCurAVelocity = 0.0f;
    
    pCamBobbing = nullptr;

    r_torso.yaw = 0.0f;
    r_torso.pitch = 0.0f;
    r_torso.roll = 0.0f;
    r_torso_tgt_roll = 0.0f;
    r_model_yaw = 0.0f;
    r_model_yaw_delta = 0.0f;
    r_model_yaw_dest = 0.0f;

    b_DropActivated = FALSE;
    f_DropPower = 0.f;

    m_fRunFactor = 2.f;
    m_fCrouchFactor = 0.2f;
    m_fClimbFactor = 1.f;
    m_fCamHeightFactor = 0.87f;

    m_fFallTime = s_fFallTime;
    m_bAnimTorsoPlayed = FALSE;

    m_pPhysicsShell = nullptr;

    m_fFeelGrenadeRadius = 10.0f;
    m_fFeelGrenadeTime = 1.0f;

    m_holder = nullptr;
    m_holderID = u16(-1);

#ifdef DEBUG
    Device.seqRender.Add(this, REG_PRIORITY_LOW);
#endif

    inventory().SetBeltUseful(true);

    m_pPersonWeLookingAt = nullptr;
    m_pVehicleWeLookingAt = nullptr;
    m_pObjectWeLookingAt = nullptr;
    m_bPickupMode = false;

    pStatGraph = nullptr;

    m_pActorEffector = nullptr;

    SetZoomAimingMode(false);

    m_sDefaultObjAction = nullptr;

    m_fSprintFactor = 4.f;

    m_pUsableObject = nullptr;

    m_anims = xr_new<SActorMotions>();
    
    m_entity_condition = nullptr;
    m_iLastHitterID = u16(-1);
    m_iLastHittingWeaponID = u16(-1);
    m_statistic_manager = nullptr;
    
    m_memory = xr_new<CActorMemory>(this);
    m_bOutBorder = false;
    m_hit_probability = 1.f;
    m_feel_touch_characters = 0;
    
    m_dwILastUpdateTime = 0;

    m_location_manager = xr_new<CLocationManager>(this);
    m_block_sprint_counter = 0;

    m_disabled_hitmarks = false;
    m_inventory_disabled = false;
}

CActor::~CActor() {
    xr_delete(m_item_use);
    xr_delete(m_location_manager);
    xr_delete(m_memory);
    xr_delete(game_news_registry);
#ifdef DEBUG
    Device.seqRender.Remove(this);
#endif

    for (auto*& cam : cameras) {
        xr_delete(cam);
    }

    m_HeavyBreathSnd.destroy();
    m_BloodSnd.destroy();
    m_DangerSnd.destroy();

    xr_delete(m_pActorEffector);
    xr_delete(m_pPhysics_support);
    xr_delete(m_anims);
}

void CActor::reinit() {
    character_physics_support()->movement()->CreateCharacter();
    character_physics_support()->movement()->SetPhysicsRefObject(this);
    CEntityAlive::reinit();
    CInventoryOwner::reinit();

    character_physics_support()->in_Init();
    material().reinit();

    m_pUsableObject = nullptr;
    memory().reinit();

    set_input_external_handler(nullptr);
    m_time_lock_accel = 0;
}

void CActor::reload(LPCSTR section) {
    CEntityAlive::reload(section);
    CInventoryOwner::reload(section);
    material().reload(section);
    CStepManager::reload(section);
    memory().reload(section);
    m_location_manager->reload(section);
}

void set_box(LPCSTR section, CPHMovementControl& mc, u32 box_num) {
    Fbox bb;
    Fvector vBOX_center, vBOX_size;
    string64 buff, buff1;
    
    strconcat(sizeof(buff), buff, "ph_box", itoa(box_num, buff1, 10), "_center");
    vBOX_center = pSettings->r_fvector3(section, buff);
    
    strconcat(sizeof(buff), buff, "ph_box", itoa(box_num, buff1, 10), "_size");
    vBOX_size = pSettings->r_fvector3(section, buff);
    
    vBOX_size.y += cammera_into_collision_shift / 2.f;
    bb.set(vBOX_center, vBOX_center);
    bb.grow(vBOX_size);
    mc.SetBox(box_num, bb);
}

void CActor::Load(LPCSTR section) {
    inherited::Load(section);
    material().Load(section);
    CInventoryOwner::Load(section);
    m_location_manager->Load(section);

    if (GameID() == eGameIDSingle)
        OnDifficultyChanged();
        
    if (ISpatial* self = smart_cast<ISpatial*>(this); self) {
        self->spatial.type |= STYPE_VISIBLEFORAI;
        self->spatial.type &= ~STYPE_REACTTOSOUND;
    }

    float cs_min = pSettings->r_float(section, "ph_crash_speed_min");
    float cs_max = pSettings->r_float(section, "ph_crash_speed_max");
    float mass = pSettings->r_float(section, "ph_mass");
    character_physics_support()->movement()->SetCrashSpeeds(cs_min, cs_max);
    character_physics_support()->movement()->SetMass(mass);
    
    if (pSettings->line_exist(section, "stalker_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtStalker, pSettings->r_float(section, "stalker_restrictor_radius"));
    if (pSettings->line_exist(section, "stalker_small_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtStalkerSmall, pSettings->r_float(section, "stalker_small_restrictor_radius"));
    if (pSettings->line_exist(section, "medium_monster_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtMonsterMedium, pSettings->r_float(section, "medium_monster_restrictor_radius"));
            
    character_physics_support()->movement()->Load(section);

    set_box(section, *character_physics_support()->movement(), 2);
    set_box(section, *character_physics_support()->movement(), 1);
    set_box(section, *character_physics_support()->movement(), 0);

    m_fWalkAccel = pSettings->r_float(section, "walk_accel");
    m_fJumpSpeed = pSettings->r_float(section, "jump_speed");
    m_fRunFactor = pSettings->r_float(section, "run_coef");
    m_fRunBackFactor = pSettings->r_float(section, "run_back_coef");
    m_fWalkBackFactor = pSettings->r_float(section, "walk_back_coef");
    m_fCrouchFactor = pSettings->r_float(section, "crouch_coef");
    m_fClimbFactor = pSettings->r_float(section, "climb_coef");
    m_fSprintFactor = pSettings->r_float(section, "sprint_koef");

    m_fWalk_StrafeFactor = READ_IF_EXISTS(pSettings, r_float, section, "walk_strafe_coef", 1.0f);
    m_fRun_StrafeFactor = READ_IF_EXISTS(pSettings, r_float, section, "run_strafe_coef", 1.0f);

    m_fCamHeightFactor = pSettings->r_float(section, "camera_height_factor");
    character_physics_support()->movement()->SetJumpUpVelocity(m_fJumpSpeed);
    
    float AirControlParam = pSettings->r_float(section, "air_control_param");
    character_physics_support()->movement()->SetAirControlParam(AirControlParam);

    m_fPickupInfoRadius = pSettings->r_float(section, "pickup_info_radius");

    m_fFeelGrenadeRadius = pSettings->r_float(section, "feel_grenade_radius");
    m_fFeelGrenadeTime = pSettings->r_float(section, "feel_grenade_time");
    m_fFeelGrenadeTime *= 1000.0f;

    character_physics_support()->in_Load(section);

    LPCSTR hit_snd_sect = pSettings->r_string(section, "hit_sounds");
    for (int hit_type = 0; hit_type < static_cast<int>(ALife::eHitTypeMax); ++hit_type) {
        LPCSTR hit_name = ALife::g_cafHitType2String(static_cast<ALife::EHitType>(hit_type));
        LPCSTR hit_snds = READ_IF_EXISTS(pSettings, r_string, hit_snd_sect, hit_name, "");
        int cnt = _GetItemCount(hit_snds);
        string128 tmp;
        VERIFY(cnt != 0);
        for (int i = 0; i < cnt; ++i) {
            sndHit[hit_type].emplace_back();
            sndHit[hit_type].back().create(_GetItem(hit_snds, i, tmp), st_Effect, sg_SourceType);
        }
    }
    
    char buf[256];
    ::Sound->create(sndDie[0], strconcat(sizeof(buf), buf, *cName(), "\\die0"), st_Effect, SOUND_TYPE_MONSTER_DYING);
    ::Sound->create(sndDie[1], strconcat(sizeof(buf), buf, *cName(), "\\die1"), st_Effect, SOUND_TYPE_MONSTER_DYING);
    ::Sound->create(sndDie[2], strconcat(sizeof(buf), buf, *cName(), "\\die2"), st_Effect, SOUND_TYPE_MONSTER_DYING);
    ::Sound->create(sndDie[3], strconcat(sizeof(buf), buf, *cName(), "\\die3"), st_Effect, SOUND_TYPE_MONSTER_DYING);

    m_HeavyBreathSnd.create(pSettings->r_string(section, "heavy_breath_snd"), st_Effect, SOUND_TYPE_MONSTER_INJURING);
    m_BloodSnd.create(pSettings->r_string(section, "heavy_blood_snd"), st_Effect, SOUND_TYPE_MONSTER_INJURING);
    m_DangerSnd.create(pSettings->r_string(section, "heavy_danger_snd"), st_Effect, SOUND_TYPE_MONSTER_INJURING);

    if (psActorFlags.test(AF_PSP))
        cam_Set(eacLookAt);
    else
        cam_Set(eacFirstEye);

    shedule.t_min = shedule.t_max = 1;

    m_fDispBase = pSettings->r_float(section, "disp_base");
    m_fDispBase = deg2rad(m_fDispBase);

    m_fDispAim = pSettings->r_float(section, "disp_aim");
    m_fDispAim = deg2rad(m_fDispAim);

    m_fDispVelFactor = pSettings->r_float(section, "disp_vel_factor");
    m_fDispAccelFactor = pSettings->r_float(section, "disp_accel_factor");
    m_fDispCrouchFactor = pSettings->r_float(section, "disp_crouch_factor");
    m_fDispCrouchNoAccelFactor = pSettings->r_float(section, "disp_crouch_no_acc_factor");

    LPCSTR default_outfit = READ_IF_EXISTS(pSettings, r_string, section, "default_outfit", nullptr);
    SetDefaultVisualOutfit(default_outfit);

    invincibility_fire_shield_1st = READ_IF_EXISTS(pSettings, r_string, section, "Invincibility_Shield_1st", nullptr);
    invincibility_fire_shield_3rd = READ_IF_EXISTS(pSettings, r_string, section, "Invincibility_Shield_3rd", nullptr);
    
    m_AutoPickUp_AABB = READ_IF_EXISTS(pSettings, r_fvector3, section, "AutoPickUp_AABB", Fvector().set(0.02f, 0.02f, 0.02f));
    m_AutoPickUp_AABB_Offset = READ_IF_EXISTS(pSettings, r_fvector3, section, "AutoPickUp_AABB_offs", Fvector().set(0.0f, 0.0f, 0.0f));

    m_sCharacterUseAction = "character_use";
    m_sDeadCharacterUseAction = "dead_character_use";
    m_sDeadCharacterUseOrDragAction = "dead_character_use_or_drag";
    m_sDeadCharacterDontUseAction = "dead_character_dont_use";
    m_sCarCharacterUseAction = "car_character_use";
    m_sInventoryItemUseAction = "inventory_item_use";
    m_sInventoryBoxUseAction = "inventory_box_use";
    
    m_sHeadShotParticle = READ_IF_EXISTS(pSettings, r_string, section, "HeadShotParticle", nullptr);
}

void CActor::PHHit(SHit& H) { m_pPhysics_support->in_Hit(H, false); }

void CActor::Hit(SHit* pHDS) {
    bool b_initiated = pHDS->aim_bullet; 

    pHDS->aim_bullet = false;

    SHit& HDS = *pHDS;
    if (HDS.hit_type < ALife::eHitTypeBurn || HDS.hit_type >= ALife::eHitTypeMax) {
        string256 err;
        xr_sprintf(err, "Unknown/unregistered hit type [%d]", HDS.hit_type);
        R_ASSERT2(0, err);
    }
#ifdef DEBUG
    if (ph_dbg_draw_mask.test(phDbgCharacterControl)) {
        DBG_OpenCashedDraw();
        Fvector to;
        to.add(Position(), Fvector().mul(HDS.dir, HDS.phys_impulse()));
        DBG_DrawLine(Position(), to, D3DCOLOR_XRGB(124, 124, 0));
        DBG_ClosedCashedDraw(500);
    }
#endif // DEBUG

    bool bPlaySound = true;
    if (!g_Alive())
        bPlaySound = false;

    if (!IsGameTypeSingle()) {
        game_PlayerState* ps = Game().GetPlayerByGameID(ID());
        if (ps && ps->testFlag(GAME_PLAYER_FLAG_INVINCIBLE)) {
            bPlaySound = false;
            if (Device.dwFrame != last_hit_frame && HDS.bone() != BI_NONE) {
                Fmatrix pos;

                CParticlesPlayer::MakeXFORM(this, HDS.bone(), HDS.dir, HDS.p_in_bone_space, pos);

                CParticlesObject* ps_obj = nullptr;

                if (eacFirstEye == cam_active && this == Level().CurrentEntity())
                    ps_obj = CParticlesObject::Create(invincibility_fire_shield_1st, TRUE);
                else
                    ps_obj = CParticlesObject::Create(invincibility_fire_shield_3rd, TRUE);

                ps_obj->UpdateParent(pos, Fvector().set(0.f, 0.f, 0.f));
                GamePersistent().ps_needtoplay.push_back(ps_obj);
            };
        };

        last_hit_frame = Device.dwFrame;
    };

    if (!sndHit[HDS.hit_type].empty() && conditions().PlayHitSound(pHDS)) {
        ref_sound& S = sndHit[HDS.hit_type][Random.randI(sndHit[HDS.hit_type].size())];
        
        bool b_snd_hit_playing = std::find_if(sndHit[HDS.hit_type].begin(), sndHit[HDS.hit_type].end(), 
            [](ref_sound& s) { return s._feedback() != nullptr; }) != sndHit[HDS.hit_type].end();

        if (ALife::eHitTypeExplosion == HDS.hit_type) {
            if (this == Level().CurrentControlEntity()) {
                S.set_volume(10.0f);
                if (!m_sndShockEffector) {
                    m_sndShockEffector = xr_new<SndShockEffector>();
                    m_sndShockEffector->Start(this, float(S.get_length_sec() * 1000.0f), HDS.damage());
                }
            } else
                bPlaySound = false;
        }
        if (bPlaySound && !b_snd_hit_playing) {
            Fvector point = Position();
            point.y += CameraHeight();
            S.play_at_pos(this, point);
        }
    }

    m_hit_slowmo = conditions().HitSlowmo(pHDS);

    if ((Level().CurrentViewEntity() == this) && (HDS.hit_type == ALife::eHitTypeFireWound)) {
        CObject* pLastHitter = Level().Objects.net_Find(m_iLastHitterID);
        CObject* pLastHittingWeapon = Level().Objects.net_Find(m_iLastHittingWeaponID);
        HitSector(pLastHitter, pLastHittingWeapon);
    }

    if ((mstate_real & mcSprint) && Level().CurrentControlEntity() == this && conditions().DisableSprint(pHDS)) {
        bool const is_special_burn_hit_2_self = (pHDS->who == this) && (pHDS->boneID == BI_NONE) &&
                                                ((pHDS->hit_type == ALife::eHitTypeBurn) ||
                                                 (pHDS->hit_type == ALife::eHitTypeLightBurn));
        if (!is_special_burn_hit_2_self) {
            mstate_wishful &= ~mcSprint;
        }
    }
    
    if (!m_disabled_hitmarks) {
        bool b_fireWound = (pHDS->hit_type == ALife::eHitTypeFireWound || pHDS->hit_type == ALife::eHitTypeWound_2);
        b_initiated = b_initiated && (pHDS->hit_type == ALife::eHitTypeStrike);

        if (b_fireWound || b_initiated)
            HitMark(HDS.damage(), HDS.dir, HDS.who, HDS.bone(), HDS.p_in_bone_space, HDS.impulse, HDS.hit_type);
    }

    if (IsGameTypeSingle()) {
        float hit_power = HitArtefactsOnBelt(HDS.damage(), HDS.hit_type);

        if (GodMode()) {
            HDS.power = 0.0f;
            inherited::Hit(&HDS);
            return;
        } else {
            HDS.power = hit_power;
            HDS.add_wound = true;
            inherited::Hit(&HDS);
        }
    } else {
        m_bWasBackStabbed = false;
        if (HDS.hit_type == ALife::eHitTypeWound_2 && Check_for_BackStab_Bone(HDS.bone())) {
            Fmatrix mInvXForm;
            mInvXForm.invert(XFORM());
            Fvector vLocalDir;
            mInvXForm.transform_dir(vLocalDir, HDS.dir);
            vLocalDir.invert();

            Fvector a = { 0.f, 0.f, 1.f };
            float res = a.dotproduct(vLocalDir);
            if (res < -0.707f) {
                game_PlayerState* ps = Game().GetPlayerByGameID(ID());

                if (!ps || !ps->testFlag(GAME_PLAYER_FLAG_INVINCIBLE))
                    m_bWasBackStabbed = true;
            }
        };

        float hit_power = 0.0f;

        if (m_bWasBackStabbed)
            hit_power = (HDS.damage() == 0.0f) ? 0.0f : 100000.0f;
        else
            hit_power = HitArtefactsOnBelt(HDS.damage(), HDS.hit_type);

        HDS.power = hit_power;
        HDS.add_wound = true;
        inherited::Hit(&HDS);

        if (OnServer() && !g_Alive() && HDS.hit_type == ALife::eHitTypeExplosion) {
            game_PlayerState* ps = Game().GetPlayerByGameID(ID());
            Game().m_WeaponUsageStatistic->OnExplosionKill(ps, HDS);
        }
    }
}

void CActor::HitMark(float P, Fvector dir, CObject* who_object, s16 element,
                     Fvector position_in_bone_space, float impulse, ALife::EHitType hit_type_) {
    if (g_Alive() && Local() && (Level().CurrentEntity() == this)) {
        HUD().HitMarked(0, P, dir);

        CEffectorCam* ce = Cameras().GetCamEffector((ECamEffectorType)effFireHit);
        if (ce)
            return;

        int id = -1;
        Fvector cam_pos, cam_dir, cam_norm;
        cam_Active()->Get(cam_pos, cam_dir, cam_norm);
        cam_dir.normalize_safe();
        dir.normalize_safe();

        float ang_diff = angle_difference(cam_dir.getH(), dir.getH());
        Fvector cp;
        cp.crossproduct(cam_dir, dir);
        bool bUp = (cp.y > 0.0f);

        Fvector cross;
        cross.crossproduct(cam_dir, dir);
        VERIFY(ang_diff >= 0.0f && ang_diff <= PI);

        constexpr float _s1 = PI_DIV_8;
        constexpr float _s2 = _s1 + PI_DIV_4;
        constexpr float _s3 = _s2 + PI_DIV_4;
        constexpr float _s4 = _s3 + PI_DIV_4;

        if (ang_diff <= _s1) {
            id = 2;
        } else if (ang_diff > _s1 && ang_diff <= _s2) {
            id = (bUp) ? 5 : 7;
        } else if (ang_diff > _s2 && ang_diff <= _s3) {
            id = (bUp) ? 3 : 1;
        } else if (ang_diff > _s3 && ang_diff <= _s4) {
            id = (bUp) ? 4 : 6;
        } else if (ang_diff > _s4) {
            id = 0;
        } else {
            VERIFY(0);
        }

        string64 sect_name;
        xr_sprintf(sect_name, "effector_fire_hit_%d", id);
        AddEffector(this, effFireHit, sect_name, P * 0.001f);
    } 
}

void CActor::HitSignal(float perc, Fvector& vLocalDir, CObject* who, s16 element) {
    if (g_Alive()) {
        Fvector D;
        XFORM().transform_dir(D, vLocalDir);

        float yaw, pitch;
        D.getHP(yaw, pitch);
        IRenderVisual* pV = Visual();
        IKinematicsAnimated* tpKinematics = smart_cast<IKinematicsAnimated*>(pV);
        IKinematics* pK = smart_cast<IKinematics*>(pV);
        VERIFY(tpKinematics);
        
        MotionID motion_ID = m_anims->m_normal.m_damage[iFloor(
            pK->LL_GetBoneInstance(element).get_param(1) +
            (angle_difference(r_model_yaw + r_model_yaw_delta, yaw) <= PI_DIV_2 ? 0 : 1))];
            
        float power_factor = std::clamp(perc / 100.f, 0.f, 1.f);
        VERIFY(motion_ID.valid());
        tpKinematics->PlayFX(motion_ID, power_factor);
    }
}

void start_tutorial(LPCSTR name);

void CActor::Die(CObject* who) {
#ifdef DEBUG
    Msg("--- Actor [%s] dies !", this->Name());
#endif // #ifdef DEBUG
    inherited::Die(who);

    if (OnServer()) {
        for (u16 I = inventory().FirstSlot(); I <= inventory().LastSlot(); ++I) {
            PIItem item_in_slot = inventory().ItemFromSlot(I);
            if (I == inventory().GetActiveSlot()) {
                if (item_in_slot) {
                    if (IsGameTypeSingle()) {
                        CGrenade* grenade = smart_cast<CGrenade*>(item_in_slot);
                        if (grenade)
                            grenade->DropGrenade();
                        else
                            item_in_slot->SetDropManual(TRUE);
                    }
                };
                continue;
            } else {
                CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(item_in_slot);
                if (pOutfit)
                    continue;
            };
            if (item_in_slot)
                inventory().Ruck(item_in_slot);
        };

        while (!inventory().m_belt.empty())
            inventory().Ruck(inventory().m_belt.front());

        if (!IsGameTypeSingle()) {
            for (auto* item : inventory().m_ruck) {
                if (item->object().CLS_ID == CLSID_OBJECT_PLAYERS_BAG) {
                    item->SetDropManual(TRUE);
                }
            }
        }
    };

    ::Sound->play_at_pos(sndDie[Random.randI(SND_DIE_COUNT)], this, Position());

    m_HeavyBreathSnd.stop();
    m_BloodSnd.stop();
    m_DangerSnd.stop();

    if (IsGameTypeSingle()) {
        cam_Set(eacFreeLook);
        CurrentGameUI()->HideShownDialogs();
        start_tutorial("game_over");
    } else {
        cam_Set(eacFixedLookAt);
    }

    mstate_wishful &= ~mcAnyMove;
    mstate_real &= ~mcAnyMove;

    xr_delete(m_sndShockEffector);
}

void CActor::SwitchOutBorder(bool new_border_state) {
    if (new_border_state) {
        callback(GameObject::eExitLevelBorder)(lua_game_object());
    } else {
        callback(GameObject::eEnterLevelBorder)(lua_game_object());
    }
    m_bOutBorder = new_border_state;
}

void CActor::g_Physics(Fvector& _accel, float jump, float dt) {
    Fvector accel;
    accel.set(_accel);
    m_hit_slowmo = std::max(0.0f, m_hit_slowmo - dt);

    accel.mul(1.f - m_hit_slowmo);

    if (g_Alive()) {
        if (mstate_real & mcClimb && !cameras[eacFirstEye]->bClampYaw)
            accel.set(0.f, 0.f, 0.f);
        character_physics_support()->movement()->Calculate(accel, cameras[cam_active]->vDirection,
                                                           0, jump, dt, false);
        bool new_border_state = character_physics_support()->movement()->isOutBorder();
        if (m_bOutBorder != new_border_state && Level().CurrentControlEntity() == this) {
            SwitchOutBorder(new_border_state);
        }
#ifdef DEBUG
        if (!psActorFlags.test(AF_NO_CLIP))
            character_physics_support()->movement()->GetPosition(Position());
#else  // DEBUG
        character_physics_support()->movement()->GetPosition(Position());
#endif // DEBUG
        character_physics_support()->movement()->bSleep = false;
    }

    if (Local() && g_Alive()) {
        if (character_physics_support()->movement()->gcontact_Was)
            Cameras().AddCamEffector(
                xr_new<CEffectorFall>(character_physics_support()->movement()->gcontact_Power));

        if (!fis_zero(character_physics_support()->movement()->gcontact_HealthLost)) {
            VERIFY(character_physics_support());
            VERIFY(character_physics_support()->movement());
            ICollisionDamageInfo* di =
                character_physics_support()->movement()->CollisionDamageInfo();
            VERIFY(di);
            bool b_hit_initiated = di->GetAndResetInitiated();
            Fvector hdir;
            di->HitDir(hdir);
            SetHitInfo(this, nullptr, 0, Fvector().set(0.f, 0.f, 0.f), hdir);
            
            if (Level().CurrentControlEntity() == this) {

                SHit HDS = SHit(character_physics_support()->movement()->gcontact_HealthLost,
                                hdir, di->DamageInitiator(),
                                character_physics_support()->movement()->ContactBone(),
                                di->HitPos(), 0.f, di->HitType(), 0.0f, b_hit_initiated);

                NET_Packet l_P;
                HDS.GenHeader(GE_HIT, ID());
                HDS.whoID = di->DamageInitiator()->ID();
                HDS.weaponID = di->DamageInitiator()->ID();
                HDS.Write_Packet(l_P);

                u_EventSend(l_P);
            }
        }
    }
}

float g_fov = 55.0f;

float CActor::currentFOV() {
    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2))
        return g_fov;

    CWeapon* pWeapon = smart_cast<CWeapon*>(inventory().ActiveItem());

    if (eacFirstEye == cam_active && pWeapon && pWeapon->IsZoomed() &&
        (!pWeapon->ZoomTexture() || (!pWeapon->IsRotatingToZoom() && pWeapon->ZoomTexture()))) {
        return pWeapon->GetZoomFactor() * (0.75f);
    } else {
        return g_fov;
    }
}

void CActor::UpdateCL() {
    if (m_item_use)
        m_item_use->Update(Device.fTimeDelta);

    if (g_Alive() && Level().CurrentViewEntity() == this) {
        if (CurrentGameUI() && nullptr == CurrentGameUI()->TopInputReceiver()) {
            int dik = get_action_dik(kUSE, 0);
            if (dik && pInput->iGetAsyncKeyState(dik))
                m_bPickupMode = true;

            dik = get_action_dik(kUSE, 1);
            if (dik && pInput->iGetAsyncKeyState(dik))
                m_bPickupMode = true;
        }
    }

    UpdateInventoryOwner(Device.dwTimeDelta);
	
    if (m_bReverseGravity) {
        m_fReverseGravityProgress += Device.fTimeDelta * 0.4f; 
        if (m_fReverseGravityProgress > 1.0f) m_fReverseGravityProgress = 1.0f;
        
        if (m_pPhysics_support && m_pPhysics_support->movement()) {
            Fvector vel;
            m_pPhysics_support->movement()->GetCharacterVelocity(vel);
            float ease = (1.0f - cosf(m_fReverseGravityProgress * PI)) / 2.0f;
            vel.y = 1.8f * ease; 
            m_pPhysics_support->movement()->SetVelocity(vel);
        }
    } else if (m_fReverseGravityProgress > 0.0f) {
        m_fReverseGravityProgress -= Device.fTimeDelta * 0.5f; 
        if (m_fReverseGravityProgress < 0.0f) m_fReverseGravityProgress = 0.0f;
        
        if (m_pPhysics_support && m_pPhysics_support->movement()) {
            Fvector vel;
            m_pPhysics_support->movement()->GetCharacterVelocity(vel);
            if (vel.y < -4.0f) {
                vel.y = -4.0f * m_fReverseGravityProgress; 
                m_pPhysics_support->movement()->SetVelocity(vel);
            }
        }
    }
	
	UpdateOrbitAnomaly();

    if (m_feel_touch_characters > 0) {
        for (auto* obj : feel_touch) {
            CPhysicsShellHolder* sh = smart_cast<CPhysicsShellHolder*>(obj);
            if (sh && sh->character_physics_support()) {
                sh->character_physics_support()->movement()->UpdateObjectBox(
                    character_physics_support()->movement()->PHCharacter());
            }
        }
    }
    if (m_holder)
        m_holder->UpdateEx(currentFOV());

    m_snd_noise -= 0.3f * Device.fTimeDelta;

    inherited::UpdateCL();
    m_pPhysics_support->in_UpdateCL();

    if (g_Alive())
        PickupModeUpdate();

    PickupModeUpdate_COD();

    SetZoomAimingMode(false);
    CWeapon* pWeapon = smart_cast<CWeapon*>(inventory().ActiveItem());

    cam_Update(float(Device.dwTimeDelta) / 1000.0f, currentFOV());

    if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID()) {
        psHUD_Flags.set(HUD_CROSSHAIR_RT2, true);
        psHUD_Flags.set(HUD_DRAW_RT, true);
    }
    if (pWeapon) {
        if (pWeapon->IsZoomed()) {
            float full_fire_disp = pWeapon->GetFireDispersion(true);

            CEffectorZoomInertion* S =
                smart_cast<CEffectorZoomInertion*>(Cameras().GetCamEffector(eCEZoom));
            if (S)
                S->SetParams(full_fire_disp);

            SetZoomAimingMode(true);
        }

        if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID()) {
            float fire_disp_full = pWeapon->GetFireDispersion(true, true);
            m_fdisp_controller.SetDispertion(fire_disp_full);

            fire_disp_full = m_fdisp_controller.GetCurrentDispertion();

            HUD().SetCrosshairDisp(fire_disp_full, 0.02f);
            HUD().ShowCrosshair(pWeapon->use_crosshair());
#ifdef DEBUG
            HUD().SetFirstBulletCrosshairDisp(pWeapon->GetFirstBulletDisp());
#endif

            BOOL B = !((mstate_real & mcLookout) && !IsGameTypeSingle());

            psHUD_Flags.set(HUD_WEAPON_RT, B);

            B = B && pWeapon->show_crosshair();

            psHUD_Flags.set(HUD_CROSSHAIR_RT2, B);

            psHUD_Flags.set(HUD_DRAW_RT, pWeapon->show_indicators());
        }

    } else {
        if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID()) {
            HUD().SetCrosshairDisp(0.f);
            HUD().ShowCrosshair(false);
        }
    }

    UpdateDefferedMessages();

    if (g_Alive())
        CStepManager::update(this == Level().CurrentViewEntity());

    spatial.type |= STYPE_REACTTOSOUND;

    if (m_sndShockEffector) {
        if (this == Level().CurrentViewEntity()) {
            m_sndShockEffector->Update();

            if (!m_sndShockEffector->InWork() || !g_Alive())
                xr_delete(m_sndShockEffector);
        } else
            xr_delete(m_sndShockEffector);
    }
    Fmatrix trans;
    if (cam_Active() == cam_FirstEye()) {
        Cameras().hud_camera_Matrix(trans);
    } else
        Cameras().camera_Matrix(trans);

    if (IsFocused())
        g_player_hud->update(trans);

    m_bPickupMode = false;
}

float NET_Jump = 0;
void CActor::set_state_box(u32 mstate) {
    if (mstate & mcCrouch) {
        if (isActorAccelerated(mstate_real, IsZoomAimingMode()))
            character_physics_support()->movement()->ActivateBox(1, true);
        else
            character_physics_support()->movement()->ActivateBox(2, true);
    } else {
        character_physics_support()->movement()->ActivateBox(0, true);
    }
}

void CActor::shedule_Update(u32 DT) {
    setSVU(OnServer());

    if (IsFocused() && !(m_item_use && m_item_use->IsActive())) {
        BOOL bHudView = HUDview();
        if (bHudView) {
            if (CInventoryItem* pInvItem = inventory().ActiveItem()) {
                if (CHudItem* pHudItem = smart_cast<CHudItem*>(pInvItem)) {
                    if (pHudItem->IsHidden()) {
                        g_player_hud->detach_item(pHudItem);
                    } else {
                        g_player_hud->attach_item(pHudItem);
                    }
                }
            } else {
                g_player_hud->detach_item_idx(0);
            }
        } else {
            g_player_hud->detach_all_items();
        }
    }

    if (m_holder || !getEnabled() || !Ready()) {
        m_sDefaultObjAction = nullptr;
        inherited::shedule_Update(DT);
        return;
    }

    u32 clamped_DT = std::clamp(DT, 0u, 100u);
    float dt = float(clamped_DT) / 1000.f;

    if (Level().CurrentControlEntity() == this && !Level().IsDemoPlay()) {
        g_cl_CheckControls(mstate_wishful, NET_SavedAccel, NET_Jump, dt);
        
        g_cl_Orientate(mstate_real, dt);
        g_Orientate(mstate_real, dt);

        g_Physics(NET_SavedAccel, NET_Jump, dt);

        g_cl_ValidateMState(dt, mstate_wishful);
        g_SetAnimation(mstate_real);

        Fvector C;
        Center(C);
        float R = Radius();
        feel_touch_update(C, R);
        Feel_Grenade_Update(m_fFeelGrenadeRadius);

        if (b_DropActivated) {
            f_DropPower += dt * 0.1f;
            f_DropPower = std::clamp(f_DropPower, 0.f, 1.f);
        } else {
            f_DropPower = 0.f;
        }
        
        if (!Level().IsDemoPlay()) {
            mstate_wishful &= ~mcAccel;
            mstate_wishful &= ~mcLStrafe;
            mstate_wishful &= ~mcRStrafe;
            mstate_wishful &= ~mcLLookout;
            mstate_wishful &= ~mcRLookout;
            mstate_wishful &= ~mcFwd;
            mstate_wishful &= ~mcBack;
            if (!psActorFlags.test(AF_CROUCH_TOGGLE))
                mstate_wishful &= ~mcCrouch;
        }
    } else {
        make_Interpolation();

        if (!NET.empty()) {
            g_sv_Orientate(mstate_real, dt);
            g_Orientate(mstate_real, dt);
            g_Physics(NET_SavedAccel, NET_Jump, dt);
            if (!m_bInInterpolation)
                g_cl_ValidateMState(dt, mstate_wishful);
            g_SetAnimation(mstate_real);

            set_state_box(NET_Last.mstate);
        }
        mstate_old = mstate_real;
    }

    NET_Jump = 0.0f;

    inherited::shedule_Update(DT);

    if (!pCamBobbing) {
        pCamBobbing = xr_new<CEffectorBobbing>();
        Cameras().AddCamEffector(pCamBobbing);
    }
    pCamBobbing->SetState(mstate_real, conditions().IsLimping(), IsZoomAimingMode());

    if (this == Level().CurrentControlEntity()) {
        if (conditions().IsLimping() && g_Alive() && !psActorFlags.test(AF_GODMODE_RT)) {
            if (!m_HeavyBreathSnd._feedback()) {
                m_HeavyBreathSnd.play_at_pos(this, Fvector().set(0, ACTOR_HEIGHT, 0), sm_Looped | sm_2D);
            } else {
                m_HeavyBreathSnd.set_position(Fvector().set(0, ACTOR_HEIGHT, 0));
            }
        } else if (m_HeavyBreathSnd._feedback()) {
            m_HeavyBreathSnd.stop();
        }

        float bs = conditions().BleedingSpeed();
        if (bs > 0.6f) {
            Fvector snd_pos{0, ACTOR_HEIGHT, 0};
            if (!m_BloodSnd._feedback())
                m_BloodSnd.play_at_pos(this, snd_pos, sm_Looped | sm_2D);
            else
                m_BloodSnd.set_position(snd_pos);

            m_BloodSnd.set_volume(bs + 0.25f);
        } else {
            if (m_BloodSnd._feedback()) m_BloodSnd.stop();
        }

        if (!g_Alive() && m_BloodSnd._feedback())
            m_BloodSnd.stop();

        bs = conditions().GetZoneDanger();
        if (bs > 0.1f) {
            Fvector snd_pos{0, ACTOR_HEIGHT, 0};
            if (!m_DangerSnd._feedback())
                m_DangerSnd.play_at_pos(this, snd_pos, sm_Looped | sm_2D);
            else
                m_DangerSnd.set_position(snd_pos);

            m_DangerSnd.set_volume(bs + 0.25f);
        } else {
            if (m_DangerSnd._feedback()) m_DangerSnd.stop();
        }

        if (!g_Alive() && m_DangerSnd._feedback())
            m_DangerSnd.stop();
    }

    if (!character_physics_support()->IsRemoved())
        setVisible(!HUDview());

    collide::rq_result& RQ = HUD().GetCurrentRayQuery();

    if (!input_external_handler_installed() && RQ.O && RQ.O->getVisible() && RQ.range < 2.0f) {
        m_pObjectWeLookingAt = smart_cast<CGameObject*>(RQ.O);

        if (CGameObject* game_object = m_pObjectWeLookingAt) {
            m_pPersonWeLookingAt  = game_object->cast_inventory_owner();
            m_pVehicleWeLookingAt = game_object->cast_holder_custom();
            CEntityAlive* pEntityAlive = game_object->cast_entity_alive();
            
            m_pUsableObject = smart_cast<CUsableScriptObject*>(game_object);
            m_pInvBoxWeLookingAt = smart_cast<CInventoryBox*>(game_object);

            if (GameID() == eGameIDSingle) {
                if (m_pUsableObject && m_pUsableObject->tip_text()) {
                    m_sDefaultObjAction = CStringTable().translate(m_pUsableObject->tip_text());
                } else {
                    if (m_pPersonWeLookingAt && pEntityAlive && pEntityAlive->g_Alive() && m_pPersonWeLookingAt->IsTalkEnabled()) {
                        m_sDefaultObjAction = m_sCharacterUseAction;
                    } else if (pEntityAlive && !pEntityAlive->g_Alive()) {
                        if (m_pPersonWeLookingAt && m_pPersonWeLookingAt->deadbody_closed_status()) {
                            m_sDefaultObjAction = m_sDeadCharacterDontUseAction;
                        } else {
                            bool b_allow_drag = !!pSettings->line_exist("ph_capture_visuals", pEntityAlive->cNameVisual());
                            if (b_allow_drag) {
                                m_sDefaultObjAction = m_sDeadCharacterUseOrDragAction;
                            } else if (m_pPersonWeLookingAt) {
                                m_sDefaultObjAction = m_sDeadCharacterUseAction;
                            }
                        }
                    } else if (m_pVehicleWeLookingAt) {
                        m_sDefaultObjAction = m_sCarCharacterUseAction;
                    } else if (m_pObjectWeLookingAt && m_pObjectWeLookingAt->cast_inventory_item() && m_pObjectWeLookingAt->cast_inventory_item()->CanTake()) {
                        m_sDefaultObjAction = m_sInventoryItemUseAction;
                    } else {
                        m_sDefaultObjAction = nullptr;
                    }
                }
            }
        }
    } else {
        m_pPersonWeLookingAt = nullptr;
        m_sDefaultObjAction = nullptr;
        m_pUsableObject = nullptr;
        m_pObjectWeLookingAt = nullptr;
        m_pVehicleWeLookingAt = nullptr;
        m_pInvBoxWeLookingAt = nullptr;
    }

    UpdateArtefactsOnBeltAndOutfit();
    m_pPhysics_support->in_shedule_Update(DT);
    Check_for_AutoPickUp();
};

void CActor::renderable_Render() {
    VERIFY(xr::valid(XFORM()));
    inherited::renderable_Render();
    if (1 /*!HUDview()*/) {
        CInventoryOwner::renderable_Render();
    }
    VERIFY(xr::valid(XFORM()));
}

BOOL CActor::renderable_ShadowGenerate() {
    if (m_holder) return FALSE;
    return inherited::renderable_ShadowGenerate();
}

void CActor::g_PerformDrop() {
    b_DropActivated = FALSE;

    if (PIItem pItem = inventory().ActiveItem()) {
        if (pItem->IsQuestItem()) return;

        u16 s = inventory().GetActiveSlot();
        if (inventory().SlotIsPersistent(s)) return;

        pItem->SetDropManual(TRUE);
    }
}

bool CActor::use_default_throw_force() {
    return g_Alive();
}

float CActor::missile_throw_force() { return 0.f; }

#ifdef DEBUG
extern BOOL g_ShowAnimationInfo;
#endif 

void CActor::OnHUDDraw(CCustomHUD*) {
    R_ASSERT(IsFocused());
    if (!((mstate_real & mcLookout) && !IsGameTypeSingle()))
        g_player_hud->render_hud();

#if 0 
	if (Level().CurrentControlEntity() == this && g_ShowAnimationInfo)
	{
		string128 buf;
		UI().Font().pFontStat->SetColor	(0xffffffff);
		UI().Font().pFontStat->OutSet		(170,530);
		UI().Font().pFontStat->OutNext	("Position:      [%3.2f, %3.2f, %3.2f]",VPUSH(Position()));
		UI().Font().pFontStat->OutNext	("Velocity:      [%3.2f, %3.2f, %3.2f]",VPUSH(m_PhysicMovementControl->GetVelocity()));
		UI().Font().pFontStat->OutNext	("Vel Magnitude: [%3.2f]",m_PhysicMovementControl->GetVelocityMagnitude());
		UI().Font().pFontStat->OutNext	("Vel Actual:    [%3.2f]",m_PhysicMovementControl->GetVelocityActual());
		switch (m_PhysicMovementControl->Environment())
		{
		case CPHMovementControl::peOnGround:	xr_strcpy(buf,"ground");			break;
		case CPHMovementControl::peInAir:		xr_strcpy(buf,"air");				break;
		case CPHMovementControl::peAtWall:		xr_strcpy(buf,"wall");				break;
		}
		UI().Font().pFontStat->OutNext	(buf);

		if (IReceived != 0)
		{
			float Size = 0;
			Size = UI().Font().pFontStat->GetSize();
			UI().Font().pFontStat->SetSize(Size*2);
			UI().Font().pFontStat->SetColor	(0xffff0000);
			UI().Font().pFontStat->OutNext ("Input :		[%3.2f]", ICoincidenced/IReceived * 100.0f);
			UI().Font().pFontStat->SetSize(Size);
		};
	};
#endif
}

void CActor::RenderIndicator(Fvector dpos, float r1, float r2, const ui_shader& IndShader) {
    if (!g_Alive()) return;

    UIRender->StartPrimitive(4, IUIRender::ptTriStrip, IUIRender::pttLIT);

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(u16(m_head));
    Fmatrix M;
    smart_cast<IKinematics*>(Visual())->CalculateBones();
    M.mul(XFORM(), BI.mTransform);

    Fvector pos = M.c;
    pos.add(dpos);
    const Fvector& T = Device.vCameraTop;
    const Fvector& R = Device.vCameraRight;
    Fvector Vr, Vt;
    Vr.x = R.x * r1;
    Vr.y = R.y * r1;
    Vr.z = R.z * r1;
    Vt.x = T.x * r2;
    Vt.y = T.y * r2;
    Vt.z = T.z * r2;

    Fvector a, b, c, d;
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.invert(a);
    d.invert(b);

    UIRender->PushPoint(d.x + pos.x, d.y + pos.y, d.z + pos.z, 0xffffffff, 0.f, 1.f);
    UIRender->PushPoint(a.x + pos.x, a.y + pos.y, a.z + pos.z, 0xffffffff, 0.f, 0.f);
    UIRender->PushPoint(c.x + pos.x, c.y + pos.y, c.z + pos.z, 0xffffffff, 1.f, 1.f);
    UIRender->PushPoint(b.x + pos.x, b.y + pos.y, b.z + pos.z, 0xffffffff, 1.f, 0.f);
    
    UIRender->CacheSetXformWorld(Fidentity);
    UIRender->SetShader(*IndShader);
    UIRender->FlushPrimitive();
}

static constexpr float mid_size = 0.097f;
static constexpr float fontsize = 15.0f;
static constexpr float upsize = 0.33f;

void CActor::RenderText(LPCSTR Text, Fvector dpos, float* pdup, u32 color) {
    if (!g_Alive()) return;

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(u16(m_head));
    Fmatrix M;
    smart_cast<IKinematics*>(Visual())->CalculateBones();
    M.mul(XFORM(), BI.mTransform);
    
    Fvector v0, v1;
    v0.set(M.c);
    v1.set(M.c);
    Fvector T = Device.vCameraTop;
    v1.add(T);

    Fvector v0r, v1r;
    Device.mFullTransform.transform(v0r, v0);
    Device.mFullTransform.transform(v1r, v1);
    float size = v1r.distance_to(v0r);
    CGameFont* pFont = UI().Font().pFontArial14;
    if (!pFont) return;
    
    float delta_up = (size < mid_size) ? upsize : (upsize * (mid_size / size));
    dpos.y += delta_up;
    
    if (size > mid_size) size = mid_size;
    
    M.c.y += dpos.y;

    Fvector4 v_res;
    Device.mFullTransform.transform(v_res, M.c);

    if (v_res.z < 0.f || v_res.w < 0.f) return;
    if (v_res.x < -1.f || v_res.x > 1.f || v_res.y < -1.f || v_res.y > 1.f) return;

    float x = (1.f + v_res.x) / 2.f * (Device.dwWidth);
    float y = (1.f - v_res.y) / 2.f * (Device.dwHeight);

    pFont->SetAligment(CGameFont::alCenter);
    pFont->SetColor(color);
    pFont->Out(x, y, Text);
    
    *pdup = delta_up;
}

void CActor::SetPhPosition(const Fmatrix& transform) {
    if (!m_pPhysicsShell) {
        character_physics_support()->movement()->SetPosition(transform.c);
    }
}

void CActor::ForceTransform(const Fmatrix& m) {
    character_physics_support()->ForceTransform(m);
    constexpr float block_damage_time_seconds = 2.f;
    if (!IsGameTypeSingle())
        character_physics_support()->movement()->BlockDamageSet(
            u64(block_damage_time_seconds / fixed_step));
}

extern ENGINE_API float psHUD_FOV;

float CActor::Radius() const {
    float R = inherited::Radius();
    if (CWeapon* W = smart_cast<CWeapon*>(inventory().ActiveItem()))
        R += W->Radius();
    return R;
}

bool CActor::use_bolts() const {
    if (!IsGameTypeSingle())
        return false;
    return CInventoryOwner::use_bolts();
}

int g_iCorpseRemove = 1;

bool CActor::NeedToDestroyObject() const {
    if (IsGameTypeSingle()) {
        return false;
    } else {
        if (g_Alive())
            return false;
        if (g_iCorpseRemove == -1)
            return false;
        if (g_iCorpseRemove == 0 && m_bAllowDeathRemove)
            return true;
        if (TimePassedAfterDeath() > m_dwBodyRemoveTime && m_bAllowDeathRemove)
            return true;
        else
            return false;
    }
}

ALife::_TIME_ID CActor::TimePassedAfterDeath() const {
    if (!g_Alive())
        return Level().timeServer() - GetLevelDeathTime();
    else
        return 0;
}

void CActor::OnItemTake(CInventoryItem* inventory_item) {
    CInventoryOwner::OnItemTake(inventory_item);
    if (OnClient())
        return;
}

void CActor::OnItemDrop(CInventoryItem* inventory_item, bool just_before_destroy) {
    CInventoryOwner::OnItemDrop(inventory_item, just_before_destroy);

    if (CCustomOutfit* outfit = smart_cast<CCustomOutfit*>(inventory_item)) {
        if (inventory_item->m_ItemCurrPlace.type == eItemPlaceSlot) {
            outfit->ApplySkinModel(this, false, false);
        }
    }

    if (CWeapon* weapon = smart_cast<CWeapon*>(inventory_item)) {
        if (inventory_item->m_ItemCurrPlace.type == eItemPlaceSlot) {
            weapon->OnZoomOut();
            if (weapon->GetRememberActorNVisnStatus())
                weapon->EnableActorNVisnAfterZoom();
        }
    }

    if (!just_before_destroy && inventory_item->BaseSlot() == GRENADE_SLOT &&
        nullptr == inventory().ItemFromSlot(GRENADE_SLOT)) {
        PIItem grenade = inventory().SameSlot(GRENADE_SLOT, inventory_item, true);

        if (grenade)
            inventory().Slot(GRENADE_SLOT, grenade, true, true);
    }
}

void CActor::OnItemDropUpdate() {
    CInventoryOwner::OnItemDropUpdate();

    for (auto* item : inventory().m_all) {
        if (!item->IsInvalid() && !attached(item)) {
            attach(item);
        }
    }
}

void CActor::OnItemRuck(CInventoryItem* inventory_item, const SInvItemPlace& previous_place) {
    CInventoryOwner::OnItemRuck(inventory_item, previous_place);
}

void CActor::OnItemBelt(CInventoryItem* inventory_item, const SInvItemPlace& previous_place) {
    CInventoryOwner::OnItemBelt(inventory_item, previous_place);
}

constexpr float ARTEFACTS_UPDATE_TIME = 0.100f;

void CActor::UpdateArtefactsOnBeltAndOutfit() {
    static float update_time = 0.0f;
    float f_update_time = 0.0f;

    if (update_time < ARTEFACTS_UPDATE_TIME) {
        update_time += conditions().fdelta_time();
        return;
    } else {
        f_update_time = update_time;
        update_time = 0.0f;
    }

    for (auto* item : inventory().m_belt) {
        if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
            conditions().ChangeBleeding(artefact->m_fBleedingRestoreSpeed * f_update_time);
            conditions().ChangeHealth(artefact->m_fHealthRestoreSpeed * f_update_time);
            conditions().ChangePower(artefact->m_fPowerRestoreSpeed * f_update_time);
            conditions().ChangeSatiety(artefact->m_fSatietyRestoreSpeed * f_update_time);
            
            if (artefact->m_fRadiationRestoreSpeed > 0.0f) {
                float val = artefact->m_fRadiationRestoreSpeed - conditions().GetBoostRadiationImmunity();
                val = std::max(0.0f, val); 
                conditions().ChangeRadiation(val * f_update_time);
            } else {
                conditions().ChangeRadiation(artefact->m_fRadiationRestoreSpeed * f_update_time);
            }
        }
    }
    
    if (CCustomOutfit* outfit = GetOutfit()) {
        conditions().ChangeBleeding(outfit->m_fBleedingRestoreSpeed * f_update_time);
        conditions().ChangeHealth(outfit->m_fHealthRestoreSpeed * f_update_time);
        conditions().ChangePower(outfit->m_fPowerRestoreSpeed * f_update_time);
        conditions().ChangeSatiety(outfit->m_fSatietyRestoreSpeed * f_update_time);
        conditions().ChangeRadiation(outfit->m_fRadiationRestoreSpeed * f_update_time);
    } else {
        if (CHelmet* pHelmet = smart_cast<CHelmet*>(inventory().ItemFromSlot(HELMET_SLOT)); !pHelmet) {
            if (CTorch* pTorch = smart_cast<CTorch*>(inventory().ItemFromSlot(TORCH_SLOT)); pTorch && pTorch->GetNightVisionStatus()) {
                pTorch->SwitchNightVision(false);
            }
        }
    }
}

float CActor::HitArtefactsOnBelt(float hit_power, ALife::EHitType hit_type) {
    for (auto* item : inventory().m_belt) {
        if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
            hit_power -= artefact->m_ArtefactHitImmunities.AffectHit(1.0f, hit_type);
        }
    }
    return std::max(hit_power, 0.0f); 
}

float CActor::GetProtection_ArtefactsOnBelt(ALife::EHitType hit_type) {
    float sum = 0.0f;
    for (auto* item : inventory().m_belt) {
        if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
            sum += artefact->m_ArtefactHitImmunities.AffectHit(1.0f, hit_type);
        }
    }
    return sum;
}

void CActor::SetZoomRndSeed(s32 Seed) {
    if (0 != Seed)
        m_ZoomRndSeed = Seed;
    else
        m_ZoomRndSeed = s32(Level().timeServer_Async());
}

void CActor::SetShotRndSeed(s32 Seed) {
    if (0 != Seed)
        m_ShotRndSeed = Seed;
    else
        m_ShotRndSeed = s32(Level().timeServer_Async());
}

void CActor::spawn_supplies() {
    inherited::spawn_supplies();
    CInventoryOwner::spawn_supplies();
}

void CActor::AnimTorsoPlayCallBack(CBlend* B) {
    CActor* actor = static_cast<CActor*>(B->CallbackParam);
    actor->m_bAnimTorsoPlayed = FALSE;
}

CPHDestroyable* CActor::ph_destroyable() {
    return smart_cast<CPHDestroyable*>(character_physics_support());
}

CEntityConditionSimple* CActor::create_entity_condition(CEntityConditionSimple* ec) {
    if (!ec)
        m_entity_condition = xr_new<CActorCondition>(this);
    else
        m_entity_condition = smart_cast<CActorCondition*>(ec);

    return (inherited::create_entity_condition(m_entity_condition));
}

DLL_Pure* CActor::_construct() {
    m_pPhysics_support = xr_new<CCharacterPhysicsSupport>(CCharacterPhysicsSupport::etActor, this);
    CEntityAlive::_construct();
    CInventoryOwner::_construct();
    CStepManager::_construct();

    return (this);
}

bool CActor::use_center_to_aim() const { 
    return (mstate_real & mcCrouch) != 0; 
}

bool CActor::can_attach(const CInventoryItem* inventory_item) const {
    const CAttachableItem* item = smart_cast<const CAttachableItem*>(inventory_item);
    if (!item || !item->can_be_attached())
        return false;

    if (std::find(m_attach_item_sections.begin(), m_attach_item_sections.end(), inventory_item->object().cNameSect()) == m_attach_item_sections.end())
        return false;

    if (attached(inventory_item->object().cNameSect()))
        return false;

    return true;
}

void CActor::OnDifficultyChanged() {
    VERIFY(g_SingleGameDifficulty >= egdNovice && g_SingleGameDifficulty <= egdMaster);
    LPCSTR diff_name = get_token_name(difficulty_type_token, g_SingleGameDifficulty);
    string128 tmp;
    
    strconcat(sizeof(tmp), tmp, "actor_immunities_", diff_name);
    conditions().LoadImmunities(tmp, pSettings);
    
    strconcat(sizeof(tmp), tmp, "hit_probability_", diff_name);
    m_hit_probability = pSettings->r_float(*cNameSect(), tmp);
    
    strconcat(sizeof(tmp), tmp, "actor_thd_", diff_name);
    conditions().LoadTwoHitsDeathParams(tmp);
}

CVisualMemoryManager* CActor::visual_memory() const { 
    return &memory().visual(); 
}

float CActor::GetMass() {
    return g_Alive() ? character_physics_support()->movement()->GetMass()
                     : (m_pPhysicsShell ? m_pPhysicsShell->getMass() : 0.0f);
}

bool CActor::is_on_ground() {
    return (character_physics_support()->movement()->Environment() != CPHMovementControl::peInAir);
}

bool CActor::is_ai_obstacle() const {
    return false; 
}

float CActor::GetRestoreSpeed(ALife::EConditionRestoreType const& type) {
    float res = 0.0f;
    
    switch (type) {
    case ALife::eHealthRestoreSpeed: {
        res = conditions().change_v().m_fV_HealthRestore;
        res += conditions().V_SatietyHealth() * ((conditions().GetSatiety() > 0.0f) ? 1.0f : -1.0f);

        for (auto* item : inventory().m_belt) {
            if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
                res += artefact->m_fHealthRestoreSpeed;
            }
        }
        if (CCustomOutfit* outfit = GetOutfit()) {
            res += outfit->m_fHealthRestoreSpeed;
        }
        break;
    }
    case ALife::eRadiationRestoreSpeed: {
        for (auto* item : inventory().m_belt) {
            if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
                res += artefact->m_fRadiationRestoreSpeed;
            }
        }
        if (CCustomOutfit* outfit = GetOutfit()) {
            res += outfit->m_fRadiationRestoreSpeed;
        }
        break;
    }
    case ALife::eSatietyRestoreSpeed: {
        res = conditions().V_Satiety();

        for (auto* item : inventory().m_belt) {
            if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
                res += artefact->m_fSatietyRestoreSpeed;
            }
        }
        if (CCustomOutfit* outfit = GetOutfit()) {
            res += outfit->m_fSatietyRestoreSpeed;
        }
        break;
    }
    case ALife::ePowerRestoreSpeed: {
        res = conditions().GetSatietyPower();

        for (auto* item : inventory().m_belt) {
            if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
                res += artefact->m_fPowerRestoreSpeed;
            }
        }
        if (CCustomOutfit* outfit = GetOutfit()) {
            res += outfit->m_fPowerRestoreSpeed;
            VERIFY(outfit->m_fPowerLoss != 0.0f);
            res /= outfit->m_fPowerLoss;
        } else {
            res /= 0.5f;
        }
        break;
    }
    case ALife::eBleedingRestoreSpeed: {
        res = conditions().change_v().m_fV_WoundIncarnation;

        for (auto* item : inventory().m_belt) {
            if (CArtefact* artefact = smart_cast<CArtefact*>(item)) {
                res += artefact->m_fBleedingRestoreSpeed;
            }
        }
        if (CCustomOutfit* outfit = GetOutfit()) {
            res += outfit->m_fBleedingRestoreSpeed;
        }
        break;
    }
    } // switch

    return res;
}

void CActor::On_SetEntity() {
    if (CCustomOutfit* pOutfit = GetOutfit()) {
        pOutfit->ApplySkinModel(this, true, true);
    } else {
        g_player_hud->load_default();
    }
}

bool CActor::unlimited_ammo() { 
    return psActorFlags.test(AF_UNLIMITEDAMMO) != 0; 
}

void CActor::SetReverseGravity(bool state) {
    m_bReverseGravity = state;
}

inline float SmootherStep(float edge0, float edge1, float x) {
    x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

void CActor::StartOrbitAnomaly(Fvector center, float radius) {
    m_AnomalyState = 1;
    m_AnomalyCenter = center;
    m_AnomalyRadius = radius;
    m_OrbitObjects.clear();

    for (int i = 1; i < 65534; ++i) {
        CPhysicsShellHolder* obj = smart_cast<CPhysicsShellHolder*>(Level().Objects.net_Find(i));
        
        if (obj && obj->ID() != this->ID() && obj->m_pPhysicsShell) {
            if (obj->Position().distance_to(center) <= radius) {
                float mass = obj->m_pPhysicsShell->getMass();
                if (mass > 0.1f && mass < 500.0f) {
                    SOrbitObject orb;
                    orb.id = obj->ID();
                    orb.target_radius = ::Random.randF(2.0f, radius);
                    orb.base_speed = ::Random.randF(1.5f, 3.5f);
                    orb.phase = ::Random.randF(0.0f, PI_MUL_2);
                    
                    orb.height_offset = ::Random.randF(1.5f, 4.0f); 
                    orb.spin_axis.set(::Random.randF(-1.f, 1.f), ::Random.randF(-1.f, 1.f), ::Random.randF(-1.f, 1.f)).normalize();
                    
                    orb.start_pos = obj->Position();
                    orb.start_pos.y += 1.0f; 
                    
                    orb.time_captured = float(Device.dwTimeGlobal) / 1000.0f;
                    m_OrbitObjects.push_back(orb);
                    
                    Fvector dir = {0.0f, 1.0f, 0.0f};
                    obj->m_pPhysicsShell->applyImpulse(dir, mass * 3.5f); 
                }
            }
        }
    }
}

void CActor::SetOrbitAttack() {
    m_AnomalyState = 2;
    m_ActorCaptureTime = float(Device.dwTimeGlobal) / 1000.0f;
    for (auto& orb : m_OrbitObjects) {
        orb.time_captured = m_ActorCaptureTime; 
    }
}

void CActor::StopOrbitAnomaly() {
    m_AnomalyState = 0;
    m_OrbitObjects.clear();
}

void CActor::UpdateOrbitAnomaly() {
    if (m_AnomalyState == 0) return;

    float dt = Device.fTimeDelta;
    float current_time = float(Device.dwTimeGlobal) / 1000.0f; 
    Fvector attack_target = Position();
    attack_target.y += 1.2f; 

    for (auto it = m_OrbitObjects.begin(); it != m_OrbitObjects.end(); ) {
        CPhysicsShellHolder* obj = smart_cast<CPhysicsShellHolder*>(Level().Objects.net_Find(it->id));
        if (!obj || !obj->m_pPhysicsShell || !obj->m_pPhysicsShell->isActive()) {
            it = m_OrbitObjects.erase(it); continue;
        }

        CPhysicsShell* shell = obj->m_pPhysicsShell;
        CPHElement* elem = cast_PHElement(shell->get_ElementByStoreOrder(0));
        if (!elem || !elem->isActive()) {
            it = m_OrbitObjects.erase(it); continue;
        }

        elem->Enable(); 

        Fvector pos = obj->Position();
        float mass = elem->getMass(); 
        Fvector current_vel;
        elem->get_LinearVel(current_vel); 

        Fvector offset;
        offset.sub(pos, m_AnomalyCenter);
        float current_height = offset.y; 
        offset.y = 0.0f; 
        
        float dist = offset.magnitude();
        Fvector radial_dir;
        
        if (dist > 0.01f) {
            radial_dir = offset; 
            radial_dir.normalize(); 
        } else {
            radial_dir.set(1.0f, 0.0f, 0.0f);
            dist = 0.01f;
        }

        Fvector tangent_dir;
        tangent_dir.set(-radial_dir.z, 0.0f, radial_dir.x);

        float speed_multiplier = std::clamp(8.0f / dist, 1.0f, 12.0f);
        float target_speed = it->base_speed * speed_multiplier; 

        Fvector desired_vel = tangent_dir;
        desired_vel.mul(target_speed);

        float target_r = (m_AnomalyState == 2) ? (it->target_radius * 0.3f) : it->target_radius;
        Fvector radius_correction = radial_dir;
        radius_correction.mul((target_r - dist) * 4.0f);
        desired_vel.add(radius_correction);

        float wave = sinf(current_time * 2.0f + it->phase) * 0.3f;
        desired_vel.y = ((it->height_offset + wave) - current_height) * 4.0f;

        Fvector total_force;
        total_force.sub(desired_vel, current_vel);
        total_force.mul(mass * 80.0f); 
        total_force.y += mass * 9.81f;

        float age = current_time - it->time_captured;
        float ease = SmootherStep(0.0f, 2.5f, age);
        total_force.mul(ease);

        elem->applyForce(total_force.x, total_force.y, total_force.z);

        Fvector torque = it->spin_axis;
        torque.mul(mass * 4.0f * speed_multiplier); 
        elem->setTorque(torque);

        ++it;
    }

    if (m_AnomalyState == 2 && m_pPhysics_support && m_pPhysics_support->movement()) {
        float time_inside = current_time - m_ActorCaptureTime;
        float power = std::clamp(time_inside / 4.0f, 0.0f, 1.0f); 

        Fvector actor_pos = Position();
        Fvector offset;
        offset.sub(m_AnomalyCenter, actor_pos);
        float dist_to_center = offset.magnitude();

        if (dist_to_center < 0.7f && power > 0.9f) {
            SHit hit;
            hit.power = 10000.0f;
            hit.dir.set(0, 1, 0);                    
            hit.who = this;                          
            hit.hit_type = ALife::eHitTypeExplosion; 
            
            this->Hit(&hit);
            StopOrbitAnomaly();
            return;
        }

        if (dist_to_center >= m_AnomalyRadius && power > 0.8f) {
            Fvector throw_dir = offset; 
            throw_dir.invert(); 
            throw_dir.normalize_safe();
            throw_dir.y = 0.4f; 
            
            Fvector throw_vel = throw_dir;
            throw_vel.mul(25.0f); 
            
            m_pPhysics_support->movement()->SetVelocity(throw_vel);
            StopOrbitAnomaly();
            return;
        }

        Fvector current_actor_vel;
        m_pPhysics_support->movement()->GetCharacterVelocity(current_actor_vel);

        Fvector pull_dir = offset;
        pull_dir.y = 0.0f; 
        if (pull_dir.magnitude() > 0.01f) pull_dir.normalize();
        else pull_dir.set(1.0f, 0.0f, 0.0f);

        Fvector spin_dir;
        spin_dir.set(-pull_dir.z, 0.0f, pull_dir.x);

        float pull_speed = power * 6.5f; 
        float spin_power = std::max(0.0f, (power - 0.5f) * 2.0f);
        float spin_speed = spin_power * (18.0f / std::max(dist_to_center, 1.0f));

        Fvector target_vel = current_actor_vel;
        target_vel.lerp(target_vel, Fvector().set(0,0,0), power * dt * 2.0f); 

        target_vel.x += pull_dir.x * pull_speed * dt * 10.0f;
        target_vel.z += pull_dir.z * pull_speed * dt * 10.0f;
        target_vel.x += spin_dir.x * spin_speed * dt * 10.0f;
        target_vel.z += spin_dir.z * spin_speed * dt * 10.0f;

        float lift_speed = power * 3.5f;
        target_vel.y = lift_speed;

        m_pPhysics_support->movement()->SetVelocity(target_vel);
    }
}
