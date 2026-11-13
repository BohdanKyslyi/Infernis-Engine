#include "stdafx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "Weapon.h"
#include "ItemUseController.h"
#include "ssfx_script_bridge.h" // Screen Space Shaders
#include "ParticlesObject.h"
#include "entity_alive.h"
#include "inventory_item_impl.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "actor.h"
#include "actoreffector.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "../xrphysics/mathutils.h"
#include "object_broker.h"
#include "player_hud.h"
#include "gamepersistent.h"
#include "effectorFall.h"
#include "debug_renderer.h"
#include "static_cast_checked.hpp"
#include "clsid_game.h"
#include "weaponBinocularsVision.h"
#include "ui/UIWindow.h"
#include "ui/UIXmlInit.h"
#include "ui_base.h"
#include "Torch.h"
#include "Entity.h"
#include "level_bullet_manager.h"
#include "../xrEngine/gamemtllib.h"

#define WEAPON_REMOVE_TIME 60000
#define ROTATION_TIME 0.25f

BOOL b_toggle_weapon_aim = FALSE;
extern CUIXml* pWpnScopeXml;

static constexpr LPCSTR scope_zoom_sounds[] = {
    "snd_scope_zoom_minus", "snd_scope_zoom_plus", "snd_scope_zoom_gyro"
};

static constexpr LPCSTR scope_brightness_sounds[] = {
    "snd_scope_brightness_minus", "snd_scope_brightness_plus"
};

CWeapon::CWeapon() {
    SetState(eHidden);
    SetNextState(eHidden);
    m_sub_state = eSubstateReloadBegin;
    m_bTriStateReload = false;
    SetDefaults();

    m_Offset.identity();
    m_StrapOffset.identity();

    m_iAmmoCurrentTotal = 0;
    m_BriefInfo_CalcFrame = 0;

    iAmmoElapsed = -1;
    iMagazineSize = -1;
    m_ammoType = 0;

    eHandDependence = hdNone;

    m_zoom_params.m_fCurrentZoomFactor = g_fov;
    m_zoom_params.m_fZoomRotationFactor = 0.f;
    m_zoom_params.m_pVision = NULL;
    m_zoom_params.m_pNight_vision = NULL;
    m_bAlternativeAimActive = false;
    m_bAlternativeAimOwnsZoom = false;
    m_fAlternativeAimFactor = 0.f;
    m_fZoomFactorBeforeAlternativeAim = g_fov;

    m_pCurrentAmmo = NULL;

    m_pFlameParticles2 = NULL;
    m_sFlameParticles2 = NULL;

    m_fCurrentCartirdgeDisp = 1.f;

    m_strap_bone0 = 0;
    m_strap_bone1 = 0;
    m_StrapOffset.identity();
    m_strapped_mode = false;
    m_can_be_strapped = false;
    m_ef_main_weapon_type = u32(-1);
    m_ef_weapon_type = u32(-1);
    m_UIScope = NULL;
    m_set_next_ammoType_on_reload = undefined_ammo_type;
    m_crosshair_inertion = 0.f;
    m_activation_speed_is_overriden = false;
    m_cur_scope = NULL;
    m_bRememberActorNVisnStatus = false;
}

CWeapon::~CWeapon() {
    SetTacticalTorchEffects(false);
    StopLaserDot();
    xr_delete(m_UIScope);
    delete_data(m_scopes);
}

void CWeapon::Hit(SHit* pHDS) { inherited::Hit(pHDS); }

void CWeapon::UpdateXForm() {
    if (Device.dwFrame == dwXF_Frame)
        return;

    dwXF_Frame = Device.dwFrame;

    if (!H_Parent())
        return;

    // Get access to entity and its visual
    CEntityAlive* E = smart_cast<CEntityAlive*>(H_Parent());

    if (!E) {
        if (!IsGameTypeSingle())
            UpdatePosition(H_Parent()->XFORM());

        return;
    }

    const CInventoryOwner* parent = smart_cast<const CInventoryOwner*>(E);
    if (!parent || parent->use_simplified_visual())
        return;

    if (parent->attached(this))
        return;

    IKinematics* V = smart_cast<IKinematics*>(E->Visual());
    VERIFY(V);

    // Get matrices
    int boneL = -1, boneR = -1, boneR2 = -1;

    // this ugly case is possible in case of a CustomMonster, not a Stalker, nor an Actor
    E->g_WeaponBones(boneL, boneR, boneR2);

    const int bone_count = V->LL_BoneCount();
    if (boneR < 0 || boneR >= bone_count)
        return;

    if (HandDependence() == hd1Hand || GetState() == eReload || !E->g_Alive())
        boneL = boneR2;

    V->CalculateBones();
    const Fmatrix& right_hand = V->LL_GetTransform(u16(boneR));
    Fmatrix grip = right_hand;
    if (boneL >= 0 && boneL < bone_count) {
        Fvector forward, right, up;
        forward.sub(V->LL_GetTransform(u16(boneL)).c, right_hand.c);
        if (forward.square_magnitude() > EPS_S) {
            forward.normalize();
            right.crossproduct(right_hand.j, forward);
            if (right.square_magnitude() > EPS_S) {
                right.normalize();
                up.crossproduct(forward, right);
                grip.set(right, up, forward, right_hand.c);
            }
        }
    }

    grip.mulA_43(E->XFORM());
    UpdatePosition(grip);
}

void CWeapon::UpdateFireDependencies_internal() {
    if (Device.dwFrame != dwFP_Frame) {
        dwFP_Frame = Device.dwFrame;

        UpdateXForm();

        if (GetHUDmode()) {
            HudItemData()->setup_firedeps(m_current_firedeps);
            VERIFY(xr::valid(m_current_firedeps.m_FireParticlesXForm));
        } else {
            // 3rd person or no parent
            Fmatrix& parent = XFORM();
            Fvector& fp = vLoadedFirePoint;
            Fvector& fp2 = vLoadedFirePoint2;
            Fvector& sp = vLoadedShellPoint;

            parent.transform_tiny(m_current_firedeps.vLastFP, fp);
            parent.transform_tiny(m_current_firedeps.vLastFP2, fp2);
            parent.transform_tiny(m_current_firedeps.vLastSP, sp);

            m_current_firedeps.vLastFD.set(0.f, 0.f, 1.f);
            parent.transform_dir(m_current_firedeps.vLastFD);

            m_current_firedeps.m_FireParticlesXForm.set(parent);
            VERIFY(xr::valid(m_current_firedeps.m_FireParticlesXForm));
        }
    }
}

void CWeapon::ForceUpdateFireParticles() {
    if (!GetHUDmode()) { // update particlesXFORM real bullet direction

        if (!H_Parent())
            return;

        Fvector p, d;
        smart_cast<CEntity*>(H_Parent())->g_fireParams(this, p, d);

        Fmatrix _pxf;
        _pxf.k = d;
        _pxf.i.crossproduct(Fvector().set(0.0f, 1.0f, 0.0f), _pxf.k);
        _pxf.j.crossproduct(_pxf.k, _pxf.i);
        _pxf.c = XFORM().c;

        m_current_firedeps.m_FireParticlesXForm.set(_pxf);
    }
}

void CWeapon::LoadHudFeatureFlags() {
    m_gunslinger_hud_movement =
        READ_IF_EXISTS(pSettings, r_bool, HudSection(), "gunslinger_hud_movement", false);
    m_advanced_ammo_bones =
        READ_IF_EXISTS(pSettings, r_bool, HudSection(), "use_advanced_ammo_bones", false);
}

void CWeapon::RefreshUpgradeHud() {
    if (m_hud_upgrade_sections.empty())
        return;

    shared_str target = m_base_upgrade_hud;
    const u16 slot = READ_IF_EXISTS(pSettings, r_u16, m_base_upgrade_hud, "attach_place_idx", u16(-1));
    for (const shared_str& effect : m_hud_upgrade_sections) {
        LPCSTR key = "hud";
        if (IsSilencerAttached() &&
            READ_IF_EXISTS(pSettings, r_bool, effect, "hud_when_silencer_is_attached", false))
            key = "hud_silencer";
        else if (IsGrenadeLauncherAttached() && GrenadeLauncherAttachable() &&
                 READ_IF_EXISTS(pSettings, r_bool, effect, "hud_when_gl_is_attached", false))
            key = "hud_gl";
        else if (IsScopeAttached() &&
                 READ_IF_EXISTS(pSettings, r_bool, effect, "hud_when_scope_is_attached", false))
            key = "hud_scope";

        const LPCSTR candidate = READ_IF_EXISTS(pSettings, r_string, effect, key, nullptr);
        if (!candidate || !candidate[0] || xr_strcmp(candidate, "skip_reassign") == 0)
            continue;
        if (!pSettings->section_exist(candidate) ||
            !pSettings->line_exist(candidate, "item_visual") ||
            slot > 1 ||
            READ_IF_EXISTS(pSettings, r_u16, candidate, "attach_place_idx", u16(-1)) != slot ||
            !UpgradeHudAllowed(candidate)) {
            Msg("! Invalid HUD [%s] in weapon upgrade [%s]", candidate, effect.c_str());
            continue;
        }
        target = candidate;
    }

    m_pending_upgrade_hud = target == HudSection() ? shared_str() : target;
    ApplyPendingUpgradeHud();
}

void CWeapon::ApplyPendingUpgradeHud() {
    if (!m_pending_upgrade_hud.size())
        return;

    const bool attached = HudItemData() != nullptr;
    if (m_bStopAtEndAnimIsRunning ||
        (attached && (GetState() != eIdle || GetNextState() != eIdle || IsPending())))
        return;

    const shared_str target = m_pending_upgrade_hud;
    m_pending_upgrade_hud = shared_str();
    if (attached && !g_player_hud->can_attach_controller_item(target)) {
        Msg("! Cannot apply weapon upgrade HUD [%s]", target.c_str());
        return;
    }

    StopCurrentAnimWithoutCallback();
    if (attached)
        g_player_hud->detach_item(this);

    hud_sect = target;
    LoadHudFeatureFlags();
    OnUpgradeHudChanged();
    UpdateWorldFireModeBones();

    if (attached) {
        g_player_hud->attach_item(this);
        UpdateHUDAddonsVisibility();
        PlayAnimIdle();
    }
}

void CWeapon::Load(LPCSTR section) {
    m_use_world_anims = READ_IF_EXISTS(pSettings, r_bool, section, "use_world_anims", false);
    m_world_animation = nullptr;
    m_bone_upgrade_sections.clear();
    LoadUpgradeBoneDefaults(section);
    m_hud_upgrade_bones = UpgradeBoneMask();
    m_world_upgrade_bones = UpgradeBoneMask();
    m_icon_upgrade_sections.clear();
    m_hud_upgrade_sections.clear();
    m_scope_upgrade_sections.clear();
    m_pending_upgrade_hud = shared_str();
    m_upgrade_world_visual = shared_str();
    inherited::Load(section);
    SetTacticalTorchEffects(false);
    m_tactical_torch = TacticalTorch();
    if (READ_IF_EXISTS(pSettings, r_bool, section, "torch_installed", false))
        LoadTacticalTorch(section);
    StopWeaponLaserEffects();
    m_weapon_laser = WeaponLaser();
    if (READ_IF_EXISTS(pSettings, r_bool, section, "laser_installed", false))
        LoadWeaponLaser(section);
    m_bayonet_section = nullptr;
    m_bayonet_has_sound = false;
    m_bayonet_hit_pending = false;
    LoadBayonet(section);

    m_base_upgrade_hud = HudSection();
    CShootingObject::Load(section);
    LoadHudFeatureFlags();
    m_sounds.LoadSound(section, "snd_boring", "sndInspect", true);
    for (u32 i = 0; i < 3; ++i) {
        const LPCSTR key = scope_zoom_sounds[i];
        const LPCSTR sound = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
        string256 path;
        if (sound && sound[0])
            _GetItem(sound, 0, path);
        m_scope_zoom_sound[i] = sound && sound[0] && path[0] && xr_strcmp(path, "$silent") != 0;
        if (m_scope_zoom_sound[i])
            m_sounds.LoadSound(section, key, key, false);
    }
    for (int i = 0; i < 2; ++i) {
        const LPCSTR key = scope_brightness_sounds[i];
        const LPCSTR sound = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
        m_scope_brightness_sound[i] = sound && sound[0] && xr_strcmp(sound, "none") != 0;
        if (m_scope_brightness_sound[i])
            m_sounds.LoadSound(section, key, key, false);
    }

    if (pSettings->line_exist(section, "flame_particles_2"))
        m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

    // load ammo classes
    m_ammoTypes.clear();
    LPCSTR S = pSettings->r_string(section, "ammo_class");
    if (S && S[0]) {
        string128 _ammoItem;
        int count = _GetItemCount(S);
        for (int it = 0; it < count; ++it) {
            _GetItem(S, it, _ammoItem);
            m_ammoTypes.push_back(_ammoItem);
        }
    }

    iAmmoElapsed = pSettings->r_s32(section, "ammo_elapsed");
    iMagazineSize = pSettings->r_s32(section, "ammo_mag_size");

    ////////////////////////////////////////////////////
    // äèñïåðñèÿ ñòðåëüáû

    //ïîäáðàñûâàíèå êàìåðû âî âðåìÿ îòäà÷è
    u8 rm = READ_IF_EXISTS(pSettings, r_u8, section, "cam_return", 1);
    cam_recoil.ReturnMode = (rm == 1);

    rm = READ_IF_EXISTS(pSettings, r_u8, section, "cam_return_stop", 0);
    cam_recoil.StopReturn = (rm == 1);

    float temp_f = 0.0f;
    temp_f = pSettings->r_float(section, "cam_relax_speed");
    cam_recoil.RelaxSpeed = xr::abs(deg2rad(temp_f));
    VERIFY(!fis_zero(cam_recoil.RelaxSpeed));
    if (fis_zero(cam_recoil.RelaxSpeed)) {
        cam_recoil.RelaxSpeed = EPS_L;
    }

    cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed;
    if (pSettings->line_exist(section, "cam_relax_speed_ai")) {
        temp_f = pSettings->r_float(section, "cam_relax_speed_ai");
        cam_recoil.RelaxSpeed_AI = xr::abs(deg2rad(temp_f));
        VERIFY(!fis_zero(cam_recoil.RelaxSpeed_AI));
        if (fis_zero(cam_recoil.RelaxSpeed_AI)) {
            cam_recoil.RelaxSpeed_AI = EPS_L;
        }
    }
    temp_f = pSettings->r_float(section, "cam_max_angle");
    cam_recoil.MaxAngleVert = xr::abs(deg2rad(temp_f));
    VERIFY(!fis_zero(cam_recoil.MaxAngleVert));
    if (fis_zero(cam_recoil.MaxAngleVert)) {
        cam_recoil.MaxAngleVert = EPS;
    }

    temp_f = pSettings->r_float(section, "cam_max_angle_horz");
    cam_recoil.MaxAngleHorz = xr::abs(deg2rad(temp_f));
    VERIFY(!fis_zero(cam_recoil.MaxAngleHorz));
    if (fis_zero(cam_recoil.MaxAngleHorz)) {
        cam_recoil.MaxAngleHorz = EPS;
    }

    temp_f = pSettings->r_float(section, "cam_step_angle_horz");
    cam_recoil.StepAngleHorz = deg2rad(temp_f);

    cam_recoil.DispersionFrac =
        xr::abs(READ_IF_EXISTS(pSettings, r_float, section, "cam_dispersion_frac", 0.7f));

    //ïîäáðàñûâàíèå êàìåðû âî âðåìÿ îòäà÷è â ðåæèìå zoom ==> ironsight or scope
    // zoom_cam_recoil.Clone( cam_recoil ); ==== íåëüçÿ !!!!!!!!!!
    zoom_cam_recoil.RelaxSpeed = cam_recoil.RelaxSpeed;
    zoom_cam_recoil.RelaxSpeed_AI = cam_recoil.RelaxSpeed_AI;
    zoom_cam_recoil.DispersionFrac = cam_recoil.DispersionFrac;
    zoom_cam_recoil.MaxAngleVert = cam_recoil.MaxAngleVert;
    zoom_cam_recoil.MaxAngleHorz = cam_recoil.MaxAngleHorz;
    zoom_cam_recoil.StepAngleHorz = cam_recoil.StepAngleHorz;

    zoom_cam_recoil.ReturnMode = cam_recoil.ReturnMode;
    zoom_cam_recoil.StopReturn = cam_recoil.StopReturn;

    if (pSettings->line_exist(section, "zoom_cam_relax_speed")) {
        zoom_cam_recoil.RelaxSpeed =
            xr::abs(deg2rad(pSettings->r_float(section, "zoom_cam_relax_speed")));
        VERIFY(!fis_zero(zoom_cam_recoil.RelaxSpeed));
        if (fis_zero(zoom_cam_recoil.RelaxSpeed)) {
            zoom_cam_recoil.RelaxSpeed = EPS_L;
        }
    }
    if (pSettings->line_exist(section, "zoom_cam_relax_speed_ai")) {
        zoom_cam_recoil.RelaxSpeed_AI =
            xr::abs(deg2rad(pSettings->r_float(section, "zoom_cam_relax_speed_ai")));
        VERIFY(!fis_zero(zoom_cam_recoil.RelaxSpeed_AI));
        if (fis_zero(zoom_cam_recoil.RelaxSpeed_AI)) {
            zoom_cam_recoil.RelaxSpeed_AI = EPS_L;
        }
    }
    if (pSettings->line_exist(section, "zoom_cam_max_angle")) {
        zoom_cam_recoil.MaxAngleVert =
            xr::abs(deg2rad(pSettings->r_float(section, "zoom_cam_max_angle")));
        VERIFY(!fis_zero(zoom_cam_recoil.MaxAngleVert));
        if (fis_zero(zoom_cam_recoil.MaxAngleVert)) {
            zoom_cam_recoil.MaxAngleVert = EPS;
        }
    }
    if (pSettings->line_exist(section, "zoom_cam_max_angle_horz")) {
        zoom_cam_recoil.MaxAngleHorz =
            xr::abs(deg2rad(pSettings->r_float(section, "zoom_cam_max_angle_horz")));
        VERIFY(!fis_zero(zoom_cam_recoil.MaxAngleHorz));
        if (fis_zero(zoom_cam_recoil.MaxAngleHorz)) {
            zoom_cam_recoil.MaxAngleHorz = EPS;
        }
    }
    if (pSettings->line_exist(section, "zoom_cam_step_angle_horz")) {
        zoom_cam_recoil.StepAngleHorz =
            deg2rad(pSettings->r_float(section, "zoom_cam_step_angle_horz"));
    }
    if (pSettings->line_exist(section, "zoom_cam_dispersion_frac")) {
        zoom_cam_recoil.DispersionFrac =
            xr::abs(pSettings->r_float(section, "zoom_cam_dispersion_frac"));
    }

    m_pdm.m_fPDM_disp_base = pSettings->r_float(section, "PDM_disp_base");
    m_pdm.m_fPDM_disp_vel_factor = pSettings->r_float(section, "PDM_disp_vel_factor");
    m_pdm.m_fPDM_disp_accel_factor = pSettings->r_float(section, "PDM_disp_accel_factor");
    m_pdm.m_fPDM_disp_crouch = pSettings->r_float(section, "PDM_disp_crouch");
    m_pdm.m_fPDM_disp_crouch_no_acc = pSettings->r_float(section, "PDM_disp_crouch_no_acc");
    m_crosshair_inertion = READ_IF_EXISTS(pSettings, r_float, section, "crosshair_inertion", 5.91f);
    m_actor_camera_speed_factor = READ_IF_EXISTS(pSettings, r_float, section, "actor_camera_speed_factor", 1.f);
    m_first_bullet_controller.load(section);
    fireDispersionConditionFactor = pSettings->r_float(section, "fire_dispersion_condition_factor");

    // modified by Peacemaker [17.10.08]
    //	misfireProbability			  =
    //pSettings->r_float(section,"misfire_probability"); 	misfireConditionK
    //= READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k",	1.0f);
    misfireStartCondition = pSettings->r_float(section, "misfire_start_condition");
    misfireEndCondition = READ_IF_EXISTS(pSettings, r_float, section, "misfire_end_condition", 0.f);
    misfireStartProbability =
        READ_IF_EXISTS(pSettings, r_float, section, "misfire_start_prob", 0.f);
    misfireEndProbability = pSettings->r_float(section, "misfire_end_prob");
    conditionDecreasePerShot = pSettings->r_float(section, "condition_shot_dec");
    conditionDecreasePerQueueShot = READ_IF_EXISTS(
        pSettings, r_float, section, "condition_queue_shot_dec", conditionDecreasePerShot);

    vLoadedFirePoint = pSettings->r_fvector3(section, "fire_point");

    if (pSettings->line_exist(section, "fire_point2"))
        vLoadedFirePoint2 = pSettings->r_fvector3(section, "fire_point2");
    else
        vLoadedFirePoint2 = vLoadedFirePoint;

    // hands
    eHandDependence = EHandDependence(pSettings->r_s32(section, "hand_dependence"));
    m_bIsSingleHanded = true;
    if (pSettings->line_exist(section, "single_handed"))
        m_bIsSingleHanded = !!pSettings->r_bool(section, "single_handed");
    //
    m_fMinRadius = pSettings->r_float(section, "min_radius");
    m_fMaxRadius = pSettings->r_float(section, "max_radius");

    // èíôîðìàöèÿ î âîçìîæíûõ àïãðåéäàõ è èõ âèçóàëèçàöèè â èíâåíòàðå
    m_eScopeStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "scope_status");
    m_eSilencerStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "silencer_status");
    m_eGrenadeLauncherStatus =
        (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "grenade_launcher_status");

    m_zoom_params.m_bZoomEnabled = !!pSettings->r_bool(section, "zoom_enabled");
    m_gunslinger_zoom = READ_IF_EXISTS(pSettings, r_bool, section, "gunslinger_zoom", false);
    m_zoom_params.m_fCurrentZoomFactor = m_gunslinger_zoom ? 1.f : g_fov;
    m_zoom_params.m_fZoomRotateTime =
        READ_IF_EXISTS(pSettings, r_float, section, "zoom_rotate_time", 0.25f);
    if (!std::isfinite(m_zoom_params.m_fZoomRotateTime) || m_zoom_params.m_fZoomRotateTime <= 0.f)
        m_zoom_params.m_fZoomRotateTime = 0.25f;

    if (m_eScopeStatus == ALife::eAddonAttachable) {
        if (pSettings->line_exist(section, "scopes_sect")) {
            LPCSTR str = pSettings->r_string(section, "scopes_sect");
            for (int i = 0, count = _GetItemCount(str); i < count; ++i) {
                string128 scope_section;
                _GetItem(str, i, scope_section);
                m_scopes.push_back(scope_section);
            }
        } else {
            m_scopes.push_back(section);
        }
    } else if (m_eScopeStatus == ALife::eAddonPermanent) {
        shared_str scope_tex_name = pSettings->r_string(cNameSect(), "scope_texture");
        m_zoom_params.m_fScopeZoomFactor = READ_IF_EXISTS(pSettings, r_float, section,
            "scope_zoom_factor", m_gunslinger_zoom ? 1.f : 50.f);
        if (UseScopeTexture() && scope_tex_name.size()) {
            m_UIScope = xr_new<CUIWindow>();
            if (!pWpnScopeXml) {
                pWpnScopeXml = xr_new<CUIXml>();
                pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
            }
            CUIXmlInit::InitWindow(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
        }
    }

    if (m_eSilencerStatus == ALife::eAddonAttachable) {
        m_sSilencerName = pSettings->r_string(section, "silencer_name");
        m_iSilencerX = pSettings->r_s32(section, "silencer_x");
        m_iSilencerY = pSettings->r_s32(section, "silencer_y");
    }

    if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable) {
        m_sGrenadeLauncherName = pSettings->r_string(section, "grenade_launcher_name");
        m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x");
        m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y");
    }

    InitAddons();
    if (pSettings->line_exist(section, "weapon_remove_time"))
        m_dwWeaponRemoveTime = pSettings->r_u32(section, "weapon_remove_time");
    else
        m_dwWeaponRemoveTime = WEAPON_REMOVE_TIME;

    if (pSettings->line_exist(section, "auto_spawn_ammo"))
        m_bAutoSpawnAmmo = pSettings->r_bool(section, "auto_spawn_ammo");
    else
        m_bAutoSpawnAmmo = TRUE;

    m_zoom_params.m_bHideCrosshairInZoom = true;

    if (pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
        m_zoom_params.m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");

    Fvector def_dof;
    def_dof.set(-1, -1, -1);
    //	m_zoom_params.m_ZoomDof		= READ_IF_EXISTS(pSettings, r_fvector3, section, "zoom_dof",
    //Fvector().set(-1,-1,-1)); 	m_zoom_params.m_bZoomDofEnabled	=
    //!def_dof.similar(m_zoom_params.m_ZoomDof);

    //	m_zoom_params.m_ReloadDof	= READ_IF_EXISTS(pSettings, r_fvector4, section, "reload_dof",
    //Fvector4().set(-1,-1,-1,-1));

    m_bHasTracers = !!READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
    m_u8TracerColorID = READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

    string256 temp;
    for (int i = egdNovice; i < egdCount; ++i) {
        strconcat(sizeof(temp), temp, "hit_probability_", get_token_name(difficulty_type_token, i));
        m_hit_probability[i] = READ_IF_EXISTS(pSettings, r_float, section, temp, 1.f);
    }

    m_zoom_params.m_bUseDynamicZoom =
        READ_IF_EXISTS(pSettings, r_bool, section, "scope_dynamic_zoom", FALSE);
    m_zoom_params.m_sUseZoomPostprocess = 0;
    m_zoom_params.m_sUseBinocularVision = 0;
}

void CWeapon::LoadFireParams(LPCSTR section) {
    cam_recoil.Dispersion = deg2rad(pSettings->r_float(section, "cam_dispersion"));
    cam_recoil.DispersionInc = 0.0f;

    if (pSettings->line_exist(section, "cam_dispersion_inc")) {
        cam_recoil.DispersionInc = deg2rad(pSettings->r_float(section, "cam_dispersion_inc"));
    }

    zoom_cam_recoil.Dispersion = cam_recoil.Dispersion;
    zoom_cam_recoil.DispersionInc = cam_recoil.DispersionInc;

    if (pSettings->line_exist(section, "zoom_cam_dispersion")) {
        zoom_cam_recoil.Dispersion = deg2rad(pSettings->r_float(section, "zoom_cam_dispersion"));
    }
    if (pSettings->line_exist(section, "zoom_cam_dispersion_inc")) {
        zoom_cam_recoil.DispersionInc =
            deg2rad(pSettings->r_float(section, "zoom_cam_dispersion_inc"));
    }

    CShootingObject::LoadFireParams(section);
};

LPCSTR CWeapon::visual_name(CSE_Abstract* server_entity) {
    // Resolve the visual before PhysicItem builds the shell for a dropped weapon.
    if (m_upgrade_world_visual.size())
        return m_upgrade_world_visual.c_str();

    const LPCSTR section = server_entity->s_name.c_str();
    if (READ_IF_EXISTS(pSettings, r_bool, section, "force_reset_visual_on_load", false)) {
        const LPCSTR visual = READ_IF_EXISTS(pSettings, r_string, section, "visual", nullptr);
        if (visual && visual[0])
            return visual;
    }

    return inherited::visual_name(server_entity);
}

bool CWeapon::SetUpgradeWorldVisual(LPCSTR visual) {
    string_path model;
    xr_strcpy(model, visual);
    if (auto* ext = strext(model))
        *ext = 0;
    xr_strlwr(model);
    if (!model[0] || m_upgrade_world_visual == model)
        return false;

    string_path filename, resolved;
    strconcat(sizeof(filename), filename, model, ".ogf");
    if (!FS.exist(resolved, "$game_meshes$", filename) &&
        !FS.exist(resolved, "$level$", filename)) {
        Msg("! Missing weapon upgrade visual [%s] for [%s]", visual, cNameSect().c_str());
        return false;
    }

    m_upgrade_world_visual = model;
    return true;
}

bool CWeapon::ApplyUpgradeWorldVisual() {
    // A physics shell can retain bone callbacks on its current visual.
    if (!m_upgrade_world_visual.size() || !Visual() || PPhysicsShell() ||
        cNameVisual() == m_upgrade_world_visual)
        return false;

    m_world_upgrade_bones.Restore(nullptr);
    cNameVisual_set(m_upgrade_world_visual);
    if (!visual_callbacks().empty())
        SetKinematicsCallback(true);
    return true;
}

BOOL CWeapon::net_Spawn(CSE_Abstract* DC) {
    m_fRTZoomFactor = m_zoom_params.m_fScopeZoomFactor;
    CSE_ALifeItemWeapon* E = smart_cast<CSE_ALifeItemWeapon*>(DC);
    // Ground objects build their physics shell inside the base spawn.
    iAmmoElapsed = E->a_elapsed;
    m_flagsAddOnState = E->m_addon_flags.get();
    const BOOL bResult = inherited::net_Spawn(DC);
    if (!bResult)
        return FALSE;
    iAmmoElapsed = E->a_elapsed;
    m_flagsAddOnState = E->m_addon_flags.get();
    m_ammoType = E->ammo_type < m_ammoTypes.size() ? E->ammo_type : 0;
    const u8 state = (E->wpn_state == eTacticalTorch || E->wpn_state == eBayonetStrike ||
                      E->wpn_state == eLaserSwitch || E->wpn_state == eFireModeSwitch ||
                      E->wpn_state == eUnloadMagazine)
        ? u8(eIdle) : E->wpn_state;
    SetState(state);
    SetNextState(state);

    m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
    if (iAmmoElapsed) {
        m_fCurrentCartirdgeDisp = m_DefaultCartridge.param_s.kDisp;
        for (int i = 0; i < iAmmoElapsed; ++i)
            m_magazine.push_back(m_DefaultCartridge);
    }

    UpdateAddonsVisibility();
    InitAddons();

    m_dwWeaponIndependencyTime = 0;

    VERIFY((u32)iAmmoElapsed == m_magazine.size());
    m_bAmmoWasSpawned = false;

    return bResult;
}

void CWeapon::net_Destroy() {
    StopWeaponLaserEffects();
    m_weapon_laser = WeaponLaser();
    SetTacticalTorchEffects(false);
    m_tactical_torch = TacticalTorch();
    m_world_upgrade_bones.Restore(nullptr);
    m_upgrade_world_visual = shared_str();
    inherited::net_Destroy();

    //óäàëèòü îáúåêòû ïàðòèêëîâ
    StopFlameParticles();
    StopFlameParticles2();
    StopLight();
    Light_Destroy();

    while (m_magazine.size())
        m_magazine.pop_back();
}

BOOL CWeapon::IsUpdating() {
    bool bIsActiveItem = m_pInventory && m_pInventory->ActiveItem() == this;
    return bIsActiveItem || bWorking; // || IsPending() || getVisible();
}

void CWeapon::net_Export(NET_Packet& P) {
    inherited::net_Export(P);

    P.w_float_q8(GetCondition(), 0.0f, 1.0f);

    u8 need_upd = IsUpdating() ? 1 : 0;
    P.w_u8(need_upd);
    P.w_u16(u16(iAmmoElapsed));
    P.w_u8(m_flagsAddOnState);
    P.w_u8(m_ammoType);
    P.w_u8((u8)GetState());
    P.w_u8((u8)IsZoomed());
}

void CWeapon::net_Import(NET_Packet& P) {
    inherited::net_Import(P);

    float _cond;
    P.r_float_q8(_cond, 0.0f, 1.0f);
    SetCondition(_cond);

    u8 flags = 0;
    P.r_u8(flags);

    u16 ammo_elapsed = 0;
    P.r_u16(ammo_elapsed);

    u8 NewAddonState;
    P.r_u8(NewAddonState);

    m_flagsAddOnState = NewAddonState;
    UpdateAddonsVisibility();

    u8 ammoType, wstate;
    P.r_u8(ammoType);
    P.r_u8(wstate);

    u8 Zoom;
    P.r_u8(Zoom);

    if (H_Parent() && H_Parent()->Remote()) {
        if (Zoom)
            OnZoomIn();
        else
            OnZoomOut();
    };
    switch (wstate) {
    case eFire:
    case eFire2:
    case eSwitch:
    case eReload: {
    } break;
    default: {
        if (ammoType >= m_ammoTypes.size())
            Msg("!! Weapon [%d], State - [%d]", ID(), wstate);
        else {
            m_ammoType = ammoType;
            SetAmmoElapsed((ammo_elapsed));
        }
    } break;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::save(NET_Packet& output_packet) {
    inherited::save(output_packet);
    save_data(iAmmoElapsed, output_packet);
    save_data(m_cur_scope, output_packet);
    save_data(m_flagsAddOnState, output_packet);
    save_data(m_ammoType, output_packet);
    save_data(m_zoom_params.m_bIsZoomModeNow, output_packet);
    save_data(m_bRememberActorNVisnStatus, output_packet);
}

void CWeapon::load(IReader& input_packet) {
    inherited::load(input_packet);
    load_data(iAmmoElapsed, input_packet);
    load_data(m_cur_scope, input_packet);
    load_data(m_flagsAddOnState, input_packet);
    if (m_cur_scope >= m_scopes.size())
        m_cur_scope = 0;
    UpdateAddonsVisibility();
    InitAddons();
    load_data(m_ammoType, input_packet);
    if (m_ammoType >= m_ammoTypes.size())
        m_ammoType = 0;
    load_data(m_zoom_params.m_bIsZoomModeNow, input_packet);

    if (m_zoom_params.m_bIsZoomModeNow)
        OnZoomIn();
    else
        OnZoomOut();

    load_data(m_bRememberActorNVisnStatus, input_packet);
}

void CWeapon::OnEvent(NET_Packet& P, u16 type) {
    switch (type) {
    case GE_ADDON_CHANGE: {
        P.r_u8(m_flagsAddOnState);
        InitAddons();
        UpdateAddonsVisibility();
    } break;

    case GE_WPN_STATE_CHANGE: {
        u8 state;
        P.r_u8(state);
        P.r_u8(m_sub_state);
        //			u8 NewAmmoType =
        P.r_u8();
        u8 AmmoElapsed = P.r_u8();
        u8 NextAmmo = P.r_u8();
        if (NextAmmo == undefined_ammo_type)
            m_set_next_ammoType_on_reload = undefined_ammo_type;
        else
            m_set_next_ammoType_on_reload = NextAmmo;

        if (OnClient())
            SetAmmoElapsed(int(AmmoElapsed));
        OnStateSwitch(u32(state));
    } break;
    default: { inherited::OnEvent(P, type); } break;
    }
};

void CWeapon::shedule_Update(u32 dT) {
    // Queue shrink
    //	u32	dwTimeCL		= Level().timeServer()-NET_Latency;
    //	while ((NET.size()>2) && (NET[1].dwTimeStamp<dwTimeCL)) NET.pop_front();

    // Inherited
    inherited::shedule_Update(dT);
}

void CWeapon::OnH_B_Independent(bool just_before_destroy) {
    RemoveShotEffector();
    m_tactical_torch.enabled = false;
    m_tactical_torch.switch_pending = false;
    SetTacticalTorchEffects(false);
    m_weapon_laser.enabled = false;
    m_weapon_laser.switch_pending = false;
    StopWeaponLaserEffects();

    if (!just_before_destroy && ApplyUpgradeWorldVisual())
        UpdateAddonsVisibility();

    inherited::OnH_B_Independent(just_before_destroy);

    FireEnd();
    SetPending(FALSE);
    SwitchState(eHidden);

    m_strapped_mode = false;
    m_zoom_params.m_bIsZoomModeNow = false;
    UpdateXForm();
}

void CWeapon::OnH_A_Independent() {
    m_dwWeaponIndependencyTime = Level().timeServer();
    inherited::OnH_A_Independent();
    Light_Destroy();
    UpdateAddonsVisibility();
};

void CWeapon::OnH_A_Chield() {
    inherited::OnH_A_Chield();
    m_world_animation = nullptr;
    UpdateAddonsVisibility();
};

void CWeapon::on_a_hud_attach() {
    inherited::on_a_hud_attach();
    UpdateHUDAddonsVisibility();
}

void CWeapon::OnActiveItem() {
    //. from Activate
    UpdateAddonsVisibility();
    m_BriefInfo_CalcFrame = 0;

    //. Show
    SwitchState(eShowing);
    //-

    inherited::OnActiveItem();
    //åñëè ìû çàíðóæàåìñÿ è îðóæèå áûëî â ðóêàõ
    //.	SetState					(eIdle);
    //.	SetNextState				(eIdle);
}

void CWeapon::OnHiddenItem() {
    m_BriefInfo_CalcFrame = 0;
    SetTacticalTorchEffects(false);
    StopWeaponLaserEffects();

    if (IsGameTypeSingle())
        SwitchState(eHiding);
    else
        SwitchState(eHidden);

    OnZoomOut();
    inherited::OnHiddenItem();

    m_set_next_ammoType_on_reload = undefined_ammo_type;
}

void CWeapon::SendHiddenItem() {
    if (!CHudItem::object().getDestroy() && m_pInventory) {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
        P.w_u8(u8(eHiding));
        P.w_u8(u8(m_sub_state));
        P.w_u8(m_ammoType);
        P.w_u8(u8(iAmmoElapsed & 0xff));
        P.w_u8(m_set_next_ammoType_on_reload);
        CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
        SetPending(TRUE);
    }
}

void CWeapon::OnH_B_Chield() {
    m_dwWeaponIndependencyTime = 0;
    inherited::OnH_B_Chield();

    OnZoomOut();
    m_set_next_ammoType_on_reload = undefined_ammo_type;
}

extern u32 hud_adj_mode;
bool CWeapon::AllowBore() { return true; }

void CWeapon::UpdateCL() {
    inherited::UpdateCL();
    UpdateScopeLensZoom();
    ApplyPendingUpgradeHud();
    UpdateTacticalTorch();
    UpdateWeaponLaser();
    UpdateBayonetStrike();
    UpdateHUDAddonsVisibility();
    //ïîäñâåòêà îò âûñòðåëà
    UpdateLight();

    //íàðèñîâàòü ïàðòèêëû
    UpdateFlameParticles();
    UpdateFlameParticles2();

    if (!IsGameTypeSingle())
        make_Interpolation();

    if ((GetNextState() == GetState()) && IsGameTypeSingle() &&
        H_Parent() == Level().CurrentEntity()) {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor && !pActor->AnyMove() && this == pActor->inventory().ActiveItem()) {
            if (hud_adj_mode == 0 && GetState() == eIdle &&
                (Device.dwTimeGlobal - m_dw_curr_substate_time > 20000) && !IsZoomed() &&
                g_player_hud->attached_item(1) == NULL) {
                if (AllowBore() &&
                    !READ_IF_EXISTS(pSettings, r_bool, HudSection(), "disable_bore", false))
                    SwitchState(eBore);

                ResetSubStateTime();
            }
        }
    }

    if (m_zoom_params.m_pNight_vision && !need_renderable()) {
        if (!m_zoom_params.m_pNight_vision->IsActive()) {
            CActor* pA = smart_cast<CActor*>(H_Parent());
            R_ASSERT(pA);
            CTorch* pTorch = smart_cast<CTorch*>(pA->inventory().ItemFromSlot(TORCH_SLOT));
            if (pTorch && pTorch->GetNightVisionStatus()) {
                m_bRememberActorNVisnStatus = pTorch->GetNightVisionStatus();
                pTorch->SwitchNightVision(false, false);
            }
            m_zoom_params.m_pNight_vision->Start(m_zoom_params.m_sUseZoomPostprocess, pA, false);
        }

    } else if (m_bRememberActorNVisnStatus) {
        m_bRememberActorNVisnStatus = false;
        EnableActorNVisnAfterZoom();
    }

    if (m_zoom_params.m_pVision)
        m_zoom_params.m_pVision->Update();
}
void CWeapon::EnableActorNVisnAfterZoom() {
    CActor* pA = smart_cast<CActor*>(H_Parent());
    if (IsGameTypeSingle() && !pA)
        pA = g_actor;

    if (pA) {
        CTorch* pTorch = smart_cast<CTorch*>(pA->inventory().ItemFromSlot(TORCH_SLOT));
        if (pTorch) {
            pTorch->SwitchNightVision(true, false);
            pTorch->GetNightVision()->PlaySounds(CNightVisionEffector::eIdleSound);
        }
    }
}

bool CWeapon::need_renderable() { return !(IsZoomed() && ZoomTexture() && !IsRotatingToZoom()); }

void CWeapon::renderable_Render() {
    UpdateXForm();

    //íàðèñîâàòü ïîäñâåòêó

    RenderLight();

    //åñëè ìû â ðåæèìå ñíàéïåðêè, òî ñàì HUD ðèñîâàòü íå íàäî
    if (IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
        RenderHud(FALSE);
    else
        RenderHud(TRUE);

    inherited::renderable_Render();
}

void CWeapon::signal_HideComplete() {
    if (H_Parent())
        setVisible(FALSE);
    SetPending(FALSE);
}

void CWeapon::SetDefaults() {
    SetPending(FALSE);

    m_flags.set(FUsingCondition, TRUE);
    bMisfire = false;
    m_flagsAddOnState = 0;
    m_zoom_params.m_bIsZoomModeNow = false;
    m_bAlternativeAimActive = false;
    m_bAlternativeAimOwnsZoom = false;
    m_fAlternativeAimFactor = 0.f;
    m_fZoomFactorBeforeAlternativeAim = g_fov;
}

void CWeapon::UpdatePosition(const Fmatrix& trans) {
    Position().set(trans.c);
    XFORM().mul(trans, m_strapped_mode ? m_StrapOffset : m_Offset);
    VERIFY(!fis_zero(DET(renderable.xform)));
}

bool CWeapon::Action(u16 cmd, u32 flags) {
    if (inherited::Action(cmd, flags))
        return true;

    switch (cmd) {
    case kWPN_TORCH:
    case kWPN_LASER: {
        const bool laser = cmd == kWPN_LASER;
        if (!(laser ? m_weapon_laser.section.size() : m_tactical_torch.section.size()))
            return false;
        if ((flags & CMD_START) && IsGameTypeSingle() && GetState() == eIdle &&
            GetNextState() == eIdle && !IsPending() && !m_bStopAtEndAnimIsRunning &&
            !IsZoomed() && !hud_adj_mode)
            SwitchState(laser ? eLaserSwitch : eTacticalTorch);
        return true;
    }
    case kWPN_INSPECT: {
        if ((flags & CMD_START) && IsGameTypeSingle() && GetState() == eIdle &&
            GetNextState() == eIdle && !IsPending() && !IsZoomed() && GetHUDmode() &&
            !hud_adj_mode && AllowBore() && !g_player_hud->attached_item(1) &&
            InspectMotion().c_str())
            SwitchState(eInspect);
        return true;
    }
    case kWPN_FIRE: {
        //åñëè îðóæèå ÷åì-òî çàíÿòî, òî íè÷åãî íå äåëàòü
        {
            if (IsPending())
                return false;

            if (flags & CMD_START)
                FireStart();
            else
                FireEnd();
        };
    }
        return true;
    case kWPN_NEXT: {
        return SwitchAmmoType(flags);
    }

    case kWPN_ZOOM:
        if (IsZoomEnabled()) {
            if (m_bAlternativeAimActive)
                return true;

            if (b_toggle_weapon_aim) {
                if (flags & CMD_START) {
                    if (!IsZoomed()) {
                        if (!IsPending()) {
                            if (GetState() != eIdle)
                                SwitchState(eIdle);
                            OnZoomIn();
                        }
                    } else
                        OnZoomOut();
                }
            } else {
                if (flags & CMD_START) {
                    if (!IsZoomed() && !IsPending()) {
                        if (GetState() != eIdle)
                            SwitchState(eIdle);
                        OnZoomIn();
                    }
                } else if (IsZoomed())
                    OnZoomOut();
            }
            return true;
        } else
            return false;

    case kWPN_ZOOM_ALTER:
        if (!IsZoomEnabled() || !IsAlternativeAimAllowed())
            return false;

        if (flags & CMD_START) {
            if (IsPending())
                return true;

            const bool was_zoomed = IsZoomed();
            m_fZoomFactorBeforeAlternativeAim = GetZoomFactor();
            m_bAlternativeAimActive = true;
            m_bAlternativeAimOwnsZoom = !was_zoomed;

            // Entering alternative aim from hip already has the regular ADS
            // transition available. Blend only when switching between two
            // fully aimed positions.
            if (!was_zoomed)
                m_fAlternativeAimFactor = 1.f;

            if (!was_zoomed) {
                if (GetState() != eIdle)
                    SwitchState(eIdle);
                OnZoomIn();
            } else {
                xr_delete(m_zoom_params.m_pVision);
                if (m_zoom_params.m_pNight_vision) {
                    m_zoom_params.m_pNight_vision->Stop(100000.0f, false);
                    xr_delete(m_zoom_params.m_pNight_vision);
                }
                m_zoom_params.m_fCurrentZoomFactor = CurrentZoomFactor();
            }
        } else if (m_bAlternativeAimActive) {
            const bool owns_zoom = m_bAlternativeAimOwnsZoom;

            if (owns_zoom && IsZoomed()) {
                OnZoomOut();
            } else {
                m_bAlternativeAimActive = false;
                m_bAlternativeAimOwnsZoom = false;
                if (IsZoomed()) {
                    OnZoomIn();
                    m_zoom_params.m_fCurrentZoomFactor =
                        m_fZoomFactorBeforeAlternativeAim;
                }
            }
        }
        return true;

    case kSCOPE_BRIGHTNESS_PLUS:
    case kSCOPE_BRIGHTNESS_MINUS:
        if (!IsZoomed() || !UsesScopeZoom() || UpgradeGrenadeMode() ||
            !Is3DScopeEnabled() || !m_scope_brightness.steps)
            return false;
        if (flags & CMD_START)
            ChangeScopeBrightness(cmd == kSCOPE_BRIGHTNESS_PLUS);
        return true;

    case kWPN_ZOOM_INC:
    case kWPN_ZOOM_DEC:
        if (IsZoomEnabled() && IsZoomed()) {
            if (cmd == kWPN_ZOOM_INC)
                ZoomInc();
            else
                ZoomDec();
            return true;
        } else
            return false;
    }
    return false;
}

bool CWeapon::SwitchAmmoType(u32 flags) {
    if (IsPending() || OnClient() || !m_pInventory || m_ammoTypes.empty())
        return false;

    if (!(flags & CMD_START))
        return false;

    u8 l_newType = m_ammoType;
    bool b1, b2;
    do {
        l_newType = u8((u32(l_newType + 1)) % m_ammoTypes.size());
        b1 = (l_newType != m_ammoType);
        b2 = unlimited_ammo() ? false : (!m_pInventory->GetAny(m_ammoTypes[l_newType].c_str()));
    } while (b1 && b2);

    if (l_newType != m_ammoType) {
        m_set_next_ammoType_on_reload = l_newType;
        if (OnServer()) {
            Reload();
        }
    }
    return true;
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID) {
    if (!m_ammoTypes.size())
        return;
    if (OnClient())
        return;
    m_bAmmoWasSpawned = true;

    int l_type = 0;
    l_type %= m_ammoTypes.size();

    if (!ammoSect)
        ammoSect = m_ammoTypes[l_type].c_str();

    ++l_type;
    l_type %= m_ammoTypes.size();

    CSE_Abstract* D = F_entity_Create(ammoSect);

    {
        CSE_ALifeItemAmmo* l_pA = smart_cast<CSE_ALifeItemAmmo*>(D);
        R_ASSERT(l_pA);
        l_pA->m_boxSize = (u16)pSettings->r_s32(ammoSect, "box_size");
        D->s_name = ammoSect;
        D->set_name_replace("");
        //.		D->s_gameid					= u8(GameID());
        D->s_RP = 0xff;
        D->ID = 0xffff;
        if (ParentID == 0xffffffff)
            D->ID_Parent = (u16)H_Parent()->ID();
        else
            D->ID_Parent = (u16)ParentID;

        D->ID_Phantom = 0xffff;
        D->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
        D->RespawnTime = 0;
        l_pA->m_tNodeID = ai_location().level_vertex_id();

        if (boxCurr == 0xffffffff)
            boxCurr = l_pA->m_boxSize;

        while (boxCurr) {
            l_pA->a_elapsed = (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
            NET_Packet P;
            D->Spawn_Write(P, TRUE);
            Level().Send(P, net_flags(TRUE));

            if (boxCurr > l_pA->m_boxSize)
                boxCurr -= l_pA->m_boxSize;
            else
                boxCurr = 0;
        }
    }
    F_entity_Destroy(D);
}

int CWeapon::GetSuitableAmmoTotal(bool use_item_to_spawn) const {
    int ae_count = iAmmoElapsed;
    if (!m_pInventory) {
        return ae_count;
    }

    //÷òîá íå äåëàòü ëèøíèõ ïåðåñ÷åòîâ
    if (m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame) {
        return ae_count + m_iAmmoCurrentTotal;
    }
    m_BriefInfo_CalcFrame = Device.dwFrame;

    m_iAmmoCurrentTotal = 0;
    for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i) {
        m_iAmmoCurrentTotal += GetAmmoCount_forType(m_ammoTypes[i]);

        if (!use_item_to_spawn) {
            continue;
        }
        if (!inventory_owner().item_to_spawn()) {
            continue;
        }
        m_iAmmoCurrentTotal += inventory_owner().ammo_in_box_to_spawn();
    }
    return ae_count + m_iAmmoCurrentTotal;
}

int CWeapon::GetAmmoCount(u8 ammo_type) const {
    VERIFY(m_pInventory);
    R_ASSERT(ammo_type < m_ammoTypes.size());

    return GetAmmoCount_forType(m_ammoTypes[ammo_type]);
}

int CWeapon::GetAmmoCount_forType(shared_str const& ammo_type) const {
    int res = 0;

    TIItemContainer::iterator itb = m_pInventory->m_belt.begin();
    TIItemContainer::iterator ite = m_pInventory->m_belt.end();
    for (; itb != ite; ++itb) {
        CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(*itb);
        if (pAmmo && (pAmmo->cNameSect() == ammo_type)) {
            res += pAmmo->m_boxCurr;
        }
    }

    itb = m_pInventory->m_ruck.begin();
    ite = m_pInventory->m_ruck.end();
    for (; itb != ite; ++itb) {
        CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>(*itb);
        if (pAmmo && (pAmmo->cNameSect() == ammo_type)) {
            res += pAmmo->m_boxCurr;
        }
    }
    return res;
}

float CWeapon::GetConditionMisfireProbability() const {
    // modified by Peacemaker [17.10.08]
    //	if(GetCondition() > 0.95f)
    //		return 0.0f;
    if (GetCondition() > misfireStartCondition)
        return 0.0f;
    if (GetCondition() < misfireEndCondition)
        return misfireEndProbability;
    //	float mis = misfireProbability+powf(1.f-GetCondition(), 3.f)*misfireConditionK;
    float mis =
        misfireStartProbability +
        ((misfireStartCondition - GetCondition()) *          // condition goes from 1.f to 0.f
         (misfireEndProbability - misfireStartProbability) / // probability goes from 0.f to 1.f
         ((misfireStartCondition == misfireEndCondition)
              ? // !!!say "No" to devision by zero
              misfireStartCondition
              : (misfireStartCondition - misfireEndCondition)));
    clamp(mis, 0.0f, 0.99f);
    return mis;
}

bool CWeapon::RollForMisfire() {
    return !OnClient() && ::Random.randF(0.f, 1.f) < GetConditionMisfireProbability();
}

BOOL CWeapon::CheckForMisfire() {
    if (RollForMisfire()) {
        FireEnd();

        bMisfire = true;
        SwitchState(eMisfire);

        return TRUE;
    } else {
        return FALSE;
    }
}

BOOL CWeapon::IsMisfire() const { return bMisfire; }
void CWeapon::Reload() { OnZoomOut(); }

bool CWeapon::IsGrenadeLauncherAttached() const {
    return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
            0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) ||
           ALife::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const {
    return (ALife::eAddonAttachable == m_eScopeStatus &&
            0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)) ||
           ALife::eAddonPermanent == m_eScopeStatus;
}

shared_str CWeapon::ScopeSettingSection(LPCSTR key) const {
    if (IsScopeAttached() && m_eScopeStatus == ALife::eAddonAttachable &&
        m_cur_scope < m_scopes.size()) {
        const shared_str& wrapper = m_scopes[m_cur_scope];
        if (pSettings->line_exist(wrapper, key))
            return wrapper;
        const shared_str optic = GetScopeName();
        if (pSettings->line_exist(optic, key))
            return optic;
    }
    return cNameSect();
}

shared_str CWeapon::ScopeLensUpgradeSection(LPCSTR key) const {
    if (m_eScopeStatus == ALife::eAddonPermanent) {
        for (auto it = m_scope_upgrade_sections.rbegin(); it != m_scope_upgrade_sections.rend(); ++it) {
            if (pSettings->line_exist(*it, key))
                return *it;
        }
    }
    return ScopeSettingSection(key);
}

shared_str CWeapon::AlternativeAimSettingSection(LPCSTR key) const {
    if (IsScopeAttached() && m_eScopeStatus == ALife::eAddonAttachable &&
        m_cur_scope < m_scopes.size()) {
        const shared_str& wrapper = m_scopes[m_cur_scope];
        if (pSettings->line_exist(wrapper, key))
            return wrapper;

        const shared_str optic = GetScopeName();
        if (pSettings->line_exist(optic, key))
            return optic;
    }

    const shared_str hud_section = HudSection();
    if (pSettings->line_exist(hud_section, key))
        return hud_section;

    // Permanent optics can keep their alternative-aim settings in the
    // weapon section because they do not have an attachable scope wrapper.
    if (pSettings->line_exist(cNameSect(), key))
        return cNameSect();

    return shared_str();
}

bool CWeapon::IsAlternativeAimAllowed() const {
    const shared_str section = AlternativeAimSettingSection("alter_zoom_allowed");
    return section.size() && pSettings->r_bool(section, "alter_zoom_allowed");
}

float CWeapon::AlternativeHudFovFactor() const {
    // When alternative aim starts from hip, its HUD offset factor is set to
    // one immediately because the regular ADS transition supplies the offset
    // blend. Combine both factors so HUD FOV follows that transition too.
    const float blend =
        m_fAlternativeAimFactor * m_zoom_params.m_fZoomRotationFactor;
    if (blend <= EPS_S)
        return 1.f;

    const shared_str section =
        AlternativeAimSettingSection("hud_fov_alter_zoom_factor");
    if (!section.size())
        return 1.f;

    const float factor = pSettings->r_float(section, "hud_fov_alter_zoom_factor");
    if (factor <= EPS_S)
        return 1.f;

    return 1.f + (factor - 1.f) * blend;
}

bool CWeapon::IsScopeCollimator() const {
    return IsScopeAttached() &&
        READ_IF_EXISTS(pSettings, r_bool, ScopeSettingSection("collimator"), "collimator", false);
}

bool CWeapon::UseScopeTexture() {
    return !IsScopeCollimator() && !Is3DScopeEnabled();
}

bool CWeapon::UsesGunslingerScopeLens() const {
    if (!IsScopeAttached() || IsScopeCollimator())
        return false;

    const shared_str section = ScopeSettingSection("need_lens_frame");
    if (pSettings->line_exist(section, "need_lens_frame"))
        return pSettings->r_bool(section, "need_lens_frame");

    return READ_IF_EXISTS(pSettings, r_bool, HudSection(), "need_lens_frame", false);
}

bool CWeapon::Is3DScopeEnabled() const {
    if (!IsScopeAttached() || !(psDeviceFlags.test(rsR4) || psDeviceFlags.test(rsR3) || psDeviceFlags.test(rsR2)) ||
        !pSettings->section_exist("shader_extensions") ||
        !pSettings->line_exist("shader_extensions", "enable_3d_scopes") ||
        !pSettings->r_bool("shader_extensions", "enable_3d_scopes"))
        return false;

    const shared_str section = ScopeSettingSection("scope_3d");
    const bool enabled = pSettings->line_exist(section, "scope_3d")
        ? pSettings->r_bool(section, "scope_3d") : UsesGunslingerScopeLens();
    return enabled && ScopeLensFov() > 0.f;
}

namespace {
float FovWithMagnification(float base_fov, float magnification) {
    if (!std::isfinite(magnification) || magnification <= 0.f)
        return base_fov;

    return rad2deg(2.f * std::atan(std::tan(deg2rad(base_fov * 0.5f)) / magnification));
}

float MinScopeMagnification(float maximum) {
    return 1.f + 0.3f * (maximum - 1.f);
}
} // namespace

bool CWeapon::GunslingerScopeLensRange(float& minimum, float& maximum) const {
    if (!UsesGunslingerScopeLens() ||
        pSettings->line_exist(ScopeSettingSection("scope_lens_fov"), "scope_lens_fov"))
        return false;

    minimum = READ_IF_EXISTS(pSettings, r_float,
        ScopeLensUpgradeSection("min_lens_factor"), "min_lens_factor", 1.f);
    maximum = READ_IF_EXISTS(pSettings, r_float,
        ScopeLensUpgradeSection("max_lens_factor"), "max_lens_factor", minimum);
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum > 0.f && maximum >= minimum;
}

float CWeapon::ScopeLensFov() const {
    if (!IsScopeAttached())
        return 0.f;

    const shared_str fov_section = ScopeSettingSection("scope_lens_fov");
    if (!pSettings->line_exist(fov_section, "scope_lens_fov")) {
        float minimum, maximum;
        if (!GunslingerScopeLensRange(minimum, maximum))
            return 0.f;

        const float factor = minimum + (maximum - minimum) * m_gunslinger_lens_position;
        return clampr(FovWithMagnification(g_fov, factor), 2.f, 90.f);
    }

    float fov = pSettings->r_float(fov_section, "scope_lens_fov");
    if (!(fov >= 5.f && fov <= 90.f))
        return 0.f;

    if (IsZoomed() && UsesScopeZoom() &&
        m_zoom_params.m_bUseDynamicZoom && m_zoom_params.m_fScopeZoomFactor > 0.f) {
        const float ratio = GetZoomFactor() / m_zoom_params.m_fScopeZoomFactor;
        fov = UsesGunslingerZoom() ? FovWithMagnification(fov, ratio) : fov * ratio;
        clamp(fov, 5.f, 90.f);
    }
    return fov;
}

float CWeapon::ScopeLensBlend() const {
    if (UpgradeGrenadeMode())
        return 0.f;
    return m_zoom_params.m_fZoomRotationFactor * (1.f - m_fAlternativeAimFactor);
}

bool CWeapon::ScopeLensShouldRender() const {
    if (!Is3DScopeEnabled() || UpgradeGrenadeMode())
        return false;

    const bool gunslinger = UsesGunslingerScopeLens();
    if (m_bAlternativeAimActive && (!gunslinger || ScopeLensBlend() <= EPS_S))
        return false;

    const LPCSTR setting = READ_IF_EXISTS(pSettings, r_string,
        "shader_extensions", "scope_render_mode", "balanced");
    if (!strcmp(setting, "quality"))
        return true;
    if (!strcmp(setting, "performance") || !strcmp(setting, "perfomance"))
        return IsZoomed() && !IsRotatingToZoom();
    if (gunslinger)
        return ScopeLensBlend() > EPS_S || (IsZoomed() && !m_bAlternativeAimActive);
    return IsZoomed(); // balanced: from the start of the aim transition
}

u8 CWeapon::ScopeLensMode() const {
    if (!Is3DScopeEnabled())
        return 0;

    // An explicit 3D effect can be set on the weapon's scope wrapper. Older
    // scopes continue to derive the mode from their 2D night-vision section.
    const shared_str mode_section = ScopeSettingSection("scope_lens_effect");
    if (pSettings->line_exist(mode_section, "scope_lens_effect")) {
        const LPCSTR mode = pSettings->r_string(mode_section, "scope_lens_effect");
        if (strstr(mode, "contrast"))
            return 1;
        if (strstr(mode, "night"))
            return 2;
        return 0;
    }

    const shared_str effect_section = ScopeSettingSection("scope_nightvision");
    const shared_str effect_name = m_zoom_params.m_sUseZoomPostprocess.size()
        ? m_zoom_params.m_sUseZoomPostprocess
        : shared_str(READ_IF_EXISTS(pSettings, r_string, effect_section, "scope_nightvision", nullptr));
    if (!effect_name.size())
        return 0;

    LPCSTR effect = effect_name.c_str();
    if (pSettings->section_exist(effect_name) && pSettings->line_exist(effect_name, "pp_eff_name"))
        effect = pSettings->r_string(effect_name, "pp_eff_name");
    if (strstr(effect, "contrast"))
        return 1;
    if (strstr(effect, "night"))
        return 2;
    return 0;
}

void CWeapon::ScopeLensGlass(float* params) const {
    params[0] = READ_IF_EXISTS(pSettings, r_float,
        ScopeSettingSection("scope_lens_eye_relief"), "scope_lens_eye_relief", 0.12f);
    params[1] = READ_IF_EXISTS(pSettings, r_float,
        ScopeSettingSection("scope_lens_reflection_strength"), "scope_lens_reflection_strength", 0.025f);
    params[2] = READ_IF_EXISTS(pSettings, r_float,
        ScopeSettingSection("scope_lens_shadow_strength"), "scope_lens_shadow_strength", 0.40f);
    clamp(params[0], 0.f, 0.5f);
    clamp(params[1], 0.f, 0.15f);
    clamp(params[2], 0.f, 0.8f);
    params[3] = READ_IF_EXISTS(pSettings, r_bool,
        ScopeSettingSection("scope_lens_white_key"), "scope_lens_white_key", false) ? 1.f : 0.f;
}

void CWeapon::LoadScopeBrightness(bool new_scope) {
    auto& brightness = m_scope_brightness;
    const ScopeBrightness previous = brightness;
    brightness = {};
    if (!m_gunslinger_lens_scope.size())
        return;

    const auto read_float = [this](LPCSTR key, float fallback, float scale) {
        float value = READ_IF_EXISTS(pSettings, r_float, ScopeSettingSection(key), key, fallback) * scale;
        if (m_eScopeStatus == ALife::eAddonPermanent) {
            for (const auto& effect : m_scope_upgrade_sections)
                value += READ_IF_EXISTS(pSettings, r_float, effect, key, 0.f);
        }
        return std::isfinite(value) ? std::max(0.f, value) : fallback * scale;
    };
    const shared_str minimum = ScopeLensUpgradeSection("min_night_brightness");
    const shared_str maximum = ScopeLensUpgradeSection("max_night_brightness");
    brightness.configured = pSettings->line_exist(minimum, "min_night_brightness") ||
        pSettings->line_exist(maximum, "max_night_brightness");
    // GUNSLINGER stores the LTX range in thirds of the shader's gain.
    brightness.minimum = read_float("min_night_brightness", 1.f, 1.f / 3.f);
    brightness.maximum = std::max(brightness.minimum, read_float("max_night_brightness", 1.f, 1.f / 3.f));
    brightness.noise = read_float("jitter_brightness", 1.f, 1.f);
    brightness.steps = std::max(0, READ_IF_EXISTS(pSettings, r_s32,
        ScopeLensUpgradeSection("steps_brightness"), "steps_brightness", 0));
    if (new_scope || previous.steps != brightness.steps || previous.minimum != brightness.minimum ||
        previous.maximum != brightness.maximum) {
        brightness.step = clampr(READ_IF_EXISTS(pSettings, r_s32,
            ScopeLensUpgradeSection("default_brightness_step"), "default_brightness_step", brightness.steps),
            0, brightness.steps);
    } else {
        brightness.step = previous.step;
    }
}

void CWeapon::ChangeScopeBrightness(bool increase) {
    auto& brightness = m_scope_brightness;
    if (increase ? brightness.step >= brightness.steps : brightness.step == 0)
        return;

    brightness.step += increase ? 1 : -1;
    if (m_scope_brightness_sound[increase])
        PlaySound(scope_brightness_sounds[increase], get_LastFP());
}

void CWeapon::ScopeLensElectronics(float* params) const {
    const auto& brightness = m_scope_brightness;
    const float fraction = brightness.steps ? float(brightness.step) / brightness.steps : 0.f;
    const float gain = brightness.minimum + (brightness.maximum - brightness.minimum) * fraction;
    params[0] = brightness.configured ? std::min(gain, 1.f) : 0.f;
    params[1] = gain;
    params[2] = brightness.noise;
}

float CWeapon::ScopeLensAberration() const {
    const LPCSTR key = "scope_abberation"; // Original GUNSLINGER LTX spelling.
    float amount = READ_IF_EXISTS(pSettings, r_float, ScopeSettingSection(key), key, 0.f);
    if (m_eScopeStatus == ALife::eAddonPermanent) {
        for (const auto& effect : m_scope_upgrade_sections)
            amount += READ_IF_EXISTS(pSettings, r_float, effect, key, 0.f);
    }
    return std::isfinite(amount) ? clampr(amount, 0.f, 0.05f) : 0.f;
}

bool CWeapon::HasScopeDetector() const {
    return IsZoomed() && !m_bAlternativeAimActive && Is3DScopeEnabled() &&
           m_zoom_params.m_sUseBinocularVision.size();
}

bool CWeapon::IsSilencerAttached() const {
    return (ALife::eAddonAttachable == m_eSilencerStatus &&
            0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer)) ||
           ALife::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable() {
    return (ALife::eAddonAttachable == m_eGrenadeLauncherStatus);
}
bool CWeapon::ScopeAttachable() { return (ALife::eAddonAttachable == m_eScopeStatus); }
bool CWeapon::SilencerAttachable() { return (ALife::eAddonAttachable == m_eSilencerStatus); }

shared_str wpn_scope = "wpn_scope";
shared_str wpn_silencer = "wpn_silencer";
shared_str wpn_grenade_launcher = "wpn_launcher";

namespace {
bool BoneListContains(LPCSTR list, int count, LPCSTR name) {
    string128 candidate;
    for (int i = 0; i < count; ++i) {
        _GetItem(list, i, candidate);
        if (xr_strcmp(candidate, name) == 0)
            return true;
    }
    return false;
}

void ApplyBoneList(IKinematics* model, LPCSTR section, LPCSTR key, BOOL visible,
                   LPCSTR exclude = nullptr, BOOL recursive = TRUE) {
    if (!section || !pSettings->section_exist(section) || !pSettings->line_exist(section, key))
        return;

    LPCSTR names = pSettings->r_string(section, key);
    const int exclude_count = exclude ? _GetItemCount(exclude) : 0;
    string128 name;
    for (int i = 0, count = _GetItemCount(names); i < count; ++i) {
        _GetItem(names, i, name);
        if (!name[0] || BoneListContains(exclude, exclude_count, name))
            continue;
        const u16 bone = model->LL_BoneID(name);
        // World and HUD visuals may use different skeletons.
        if (bone != BI_NONE && model->LL_GetBoneVisible(bone) != visible)
            model->LL_SetBoneVisible(bone, visible, recursive);
    }
}
void CollectUpgradeBoneDefaults(LPCSTR group, xr_vector<shared_str>& visited,
                                xr_vector<shared_str>& effects) {
    if (!pSettings->section_exist(group) ||
        std::find(visited.begin(), visited.end(), shared_str(group)) != visited.end())
        return;

    visited.emplace_back(group);
    const LPCSTR elements = READ_IF_EXISTS(pSettings, r_string, group, "elements", nullptr);
    if (!elements || !elements[0])
        return;

    string128 node;
    for (int i = 0, count = _GetItemCount(elements); i < count; ++i) {
        _GetItem(elements, i, node);
        if (!node[0] || !pSettings->section_exist(node))
            continue;

        const LPCSTR effect = READ_IF_EXISTS(pSettings, r_string, node, "section", nullptr);
        if (effect && effect[0] && pSettings->section_exist(effect) &&
            pSettings->line_exist(effect, "show_bones") &&
            std::find(effects.begin(), effects.end(), shared_str(effect)) == effects.end())
            effects.emplace_back(effect);

        const LPCSTR next = READ_IF_EXISTS(pSettings, r_string, node, "effects", nullptr);
        if (!next || !next[0])
            continue;

        string128 child;
        for (int j = 0, children = _GetItemCount(next); j < children; ++j) {
            _GetItem(next, j, child);
            if (child[0])
                CollectUpgradeBoneDefaults(child, visited, effects);
        }
    }
}
} // namespace

void CWeapon::LoadUpgradeBoneDefaults(LPCSTR section) {
    m_upgrade_bone_defaults.clear();
    const LPCSTR groups = READ_IF_EXISTS(pSettings, r_string, section, "upgrades", nullptr);
    if (!groups || !groups[0])
        return;

    xr_vector<shared_str> visited;
    string128 group;
    for (int i = 0, count = _GetItemCount(groups); i < count; ++i) {
        _GetItem(groups, i, group);
        if (group[0])
            CollectUpgradeBoneDefaults(group, visited, m_upgrade_bone_defaults);
    }
}

void CWeapon::UpgradeBoneMask::Restore(IKinematics* current) {
    if (current && current == model)
        for (u16 bone : hidden)
            current->LL_SetBoneVisible(bone, TRUE, FALSE);
    hidden.clear();
    model = current;
}

void CWeapon::UpgradeBoneMask::Hide(IKinematics* current, LPCSTR section, LPCSTR key) {
    const LPCSTR names = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
    if (!names || !names[0])
        return;

    string128 name;
    for (int i = 0, count = _GetItemCount(names); i < count; ++i) {
        _GetItem(names, i, name);
        const u16 bone = current->LL_BoneID(name);
        if (bone == BI_NONE || !current->LL_GetBoneVisible(bone))
            continue;
        hidden.push_back(bone);
        current->LL_SetBoneVisible(bone, FALSE, FALSE);
    }
}

void CWeapon::UpdateWorldFireModeBones() {
    const auto& hud_section = HudSection();
    if (!Visual() || !hud_section.size() || !pSettings->line_exist(hud_section, "firemode_bones_total"))
        return;

    if (auto* model = Visual()->dcast_PKinematics()) {
        ApplyConfiguredBoneVisibility(model);
        model->CalculateBones_Invalidate();
    }
}

void CWeapon::ApplyConfiguredBoneVisibility(IKinematics* model) {
    if (!model)
        return;

    const auto* hud_item = HudItemData();
    const bool hud = hud_item && hud_item->m_model == model;
    auto& hidden = hud ? m_hud_upgrade_bones : m_world_upgrade_bones;
    hidden.Restore(model);
    const shared_str weapon_section = cNameSect();
    ApplyBoneList(model, weapon_section.c_str(), "def_hide_bones", FALSE);

    ApplyBoneList(model, m_sSilencerName.c_str(), "bones", FALSE);
    ApplyBoneList(model, m_sGrenadeLauncherName.c_str(), "bones", FALSE);
    ApplyBoneList(model, weapon_section.c_str(), "def_show_bones", TRUE);

    // Reset upgrade-only meshes even on models that show them by default.
    for (const shared_str& effect : m_upgrade_bone_defaults)
        ApplyBoneList(model, effect.c_str(), "show_bones", FALSE);

    // Installed upgrades are replayed in order when a save is loaded.
    for (const shared_str& section : m_bone_upgrade_sections) {
        ApplyBoneList(model, section.c_str(), "hide_bones", FALSE);
        ApplyBoneList(model, section.c_str(), "show_bones", TRUE);
    }

    // A mount upgrade can list scope meshes in show_bones. The attachment
    // state owns those meshes and must be applied after all upgrade effects.
    for (const shared_str& scope : m_scopes) {
        if (!pSettings->section_exist(scope.c_str()))
            continue;
        const shared_str optic = pSettings->line_exist(scope.c_str(), "scope_name") ?
            pSettings->r_string(scope.c_str(), "scope_name") : scope;
        const LPCSTR section = pSettings->line_exist(scope.c_str(), "bones") ?
            scope.c_str() : optic.c_str();
        ApplyBoneList(model, section, "bones", FALSE);
    }

    // An attached optic can override a rail or sight enabled by an upgrade.
    if (IsScopeAttached() && m_eScopeStatus == ALife::eAddonAttachable &&
        m_cur_scope < m_scopes.size()) {
        const LPCSTR wrapper = m_scopes[m_cur_scope].c_str();
        const shared_str optic = GetScopeName();
        // Wrapper rules take priority, with a fallback to the addon section.
        const LPCSTR hide_section = pSettings->line_exist(wrapper, "overriding_hide_bones") ?
            wrapper : optic.c_str();
        const LPCSTR show_section = pSettings->line_exist(wrapper, "bones") ?
            wrapper : optic.c_str();
        const LPCSTR extra_show_section = pSettings->line_exist(wrapper, "overriding_show_bones") ?
            wrapper : optic.c_str();
        ApplyBoneList(model, show_section, "bones", TRUE);
        ApplyBoneList(model, hide_section, "overriding_hide_bones", FALSE);
        ApplyBoneList(model, extra_show_section, "overriding_show_bones", TRUE);
    } else if (!IsScopeAttached()) {
        ApplyBoneList(model, weapon_section.c_str(), "no_scope_overriding_hide_bones", FALSE);
        ApplyBoneList(model, weapon_section.c_str(), "no_scope_overriding_show_bones", TRUE);
    }
    if (IsSilencerAttached() && m_eSilencerStatus == ALife::eAddonAttachable) {
        ApplyBoneList(model, m_sSilencerName.c_str(), "overriding_hide_bones", FALSE);
        ApplyBoneList(model, m_sSilencerName.c_str(), "bones", TRUE);
    }
    if (IsGrenadeLauncherAttached() && m_eGrenadeLauncherStatus == ALife::eAddonAttachable) {
        ApplyBoneList(model, m_sGrenadeLauncherName.c_str(), "overriding_hide_bones", FALSE);
        ApplyBoneList(model, m_sGrenadeLauncherName.c_str(), "bones", TRUE);
    }

    const auto& hud_section = HudSection();
    if (hud_section.size() && pSettings->line_exist(hud_section, "firemode_bones_total")) {
        string64 key;
        const int mode = FireModeForHudBones();
        if (mode < 0)
            xr_strcpy(key, "firemode_bones_a");
        else
            xr_sprintf(key, "firemode_bones_%d", mode);
        const LPCSTR visible = READ_IF_EXISTS(pSettings, r_string, hud_section, key, nullptr);
        ApplyBoneList(model, hud_section.c_str(), "firemode_bones_total", FALSE, visible, hud);
        ApplyBoneList(model, hud_section.c_str(), key, TRUE, nullptr, hud);
    }

    const bool launcher = IsGrenadeLauncherAttached();
    const bool grenade_pose = launcher && UpgradeGrenadeMode() && GetState() != eSwitch;
    if (launcher)
        hidden.Hide(model, weapon_section.c_str(), "def_hide_bones_override_when_gl_attached");
    if (grenade_pose)
        hidden.Hide(model, weapon_section.c_str(), "def_hide_bones_grenade");

    for (const shared_str& effect : m_bone_upgrade_sections) {
        hidden.Hide(model, effect.c_str(), "hide_bones_override");
        if (IsSilencerAttached())
            hidden.Hide(model, effect.c_str(), "hide_bones_override_when_silencer_attached");
        if (IsScopeAttached())
            hidden.Hide(model, effect.c_str(), "hide_bones_override_when_scope_attached");
        if (launcher)
            hidden.Hide(model, effect.c_str(), "hide_bones_override_when_gl_attached");
        if (grenade_pose)
            hidden.Hide(model, effect.c_str(), "hide_bones_override_grenade");
    }

    ApplyBoneList(model, m_tactical_torch.section.c_str(), "torch_cone_bones", m_tactical_torch.active);
    ApplyBoneList(model, m_weapon_laser.section.c_str(), "laser_ray_bones", m_weapon_laser.beam_visible);
}

namespace {
Fvector WeaponAccessoryOffset(LPCSTR section, LPCSTR prefix, const Fvector& fallback) {
    string64 key;
    Fvector offset;
    xr_sprintf(key, "%s_x", prefix);
    offset.x = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.x);
    xr_sprintf(key, "%s_y", prefix);
    offset.y = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.y);
    xr_sprintf(key, "%s_z", prefix);
    offset.z = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.z);
    return offset;
}

Fcolor TorchColor(LPCSTR section, LPCSTR prefix, const Fcolor& fallback) {
    string64 key;
    Fcolor color;
    xr_sprintf(key, "%s_r", prefix);
    color.r = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.r);
    xr_sprintf(key, "%s_g", prefix);
    color.g = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.g);
    xr_sprintf(key, "%s_b", prefix);
    color.b = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.b);
    xr_sprintf(key, "%s_a", prefix);
    color.a = READ_IF_EXISTS(pSettings, r_float, section, key, fallback.a);
    return color;
}

u32 WeaponActionDelay(LPCSTR hud, LPCSTR prefix, LPCSTR motion) {
    string256 key;
    xr_sprintf(key, "%s%s", prefix, motion);
    const float seconds = READ_IF_EXISTS(pSettings, r_float, hud, key, 0.f);
    return std::isfinite(seconds) ? u32(clampr(seconds, 0.f, 60.f) * 1000.f + 0.5f) : 0;
}
} // namespace

void CWeapon::LoadTacticalTorch(LPCSTR section) {
    auto& torch = m_tactical_torch;
    if (torch.section.size() && xr_strcmp(torch.section.c_str(), section) == 0)
        return;

    SetTacticalTorchEffects(false);
    torch.spot.destroy();
    torch.omni.destroy();
    torch.glow.destroy();
    torch.section = section;
    torch.bone = READ_IF_EXISTS(pSettings, r_string, section, "torch_light_bone", nullptr);
    torch.direction_bone = READ_IF_EXISTS(pSettings, r_bool, section, "light_directions_by_bones", false) ?
        READ_IF_EXISTS(pSettings, r_string, section, "light_dir_bone", nullptr) : nullptr;

    Fvector zero;
    zero.set(0.f, 0.f, 0.f);
    torch.hud_offset = WeaponAccessoryOffset(section, "torch_attach_offset", zero);
    torch.world_offset = WeaponAccessoryOffset(section, "torch_world_attach_offset", zero);
    torch.omni_hud_offset = WeaponAccessoryOffset(section, "torch_omni_attach_offset", torch.hud_offset);
    torch.omni_world_offset = WeaponAccessoryOffset(section, "torch_omni_world_attach_offset", torch.world_offset);
    torch.breaking.set(-1.f, -1.f, 0.f);
    if (pSettings->line_exist(section, "torch_breaking_params"))
        torch.breaking = pSettings->r_fvector3(section, "torch_breaking_params");
    torch.damaged = false;
    torch.damage_checked_at = Device.dwTimeGlobal - 100;

    const shared_str weapon = cNameSect();
    const LPCSTR torch_sounds[] = {"snd_torch_off", "snd_torch_on"};
    for (int i = 0; i < 2; ++i) {
        if (torch.has_switch_sound[i])
            continue;
        const LPCSTR value = READ_IF_EXISTS(pSettings, r_string, weapon, torch_sounds[i], nullptr);
        if (!value || !value[0])
            continue;
        string256 sound_path;
        _GetItem(value, 0, sound_path);
        if (!sound_path[0] || xr_strcmp(sound_path, "$silent") == 0)
            continue;
        m_sounds.LoadSound(weapon.c_str(), torch_sounds[i], torch_sounds[i], false);
        torch.has_switch_sound[i] = true;
    }
}

void CWeapon::CreateTacticalTorchLights() {
    auto& torch = m_tactical_torch;
    const LPCSTR section = torch.section.c_str();
    const bool deferred = psDeviceFlags.test(rsR2) || psDeviceFlags.test(rsR3) ||
        psDeviceFlags.test(rsR4);
    const float cone = deg2rad(clampr(
        READ_IF_EXISTS(pSettings, r_float, section, "torch_spot_angle", 75.f), 1.f, 120.f));

    Fcolor spot_default, omni_default;
    if (deferred)
        spot_default.set(0.6f, 0.55f, 0.55f, 0.8f);
    else
        spot_default.set(1.f, 1.f, 1.f, 0.2f);
    omni_default.set(1.f, 1.f, 1.f, 0.1f);
    const Fcolor spot_color = TorchColor(section, deferred ? "torch_r2_color" : "torch_color", spot_default);
    const Fcolor omni_color = TorchColor(section, deferred ? "torch_r2_omni_color" : "torch_omni_color", omni_default);

    const auto create_light = [&](ref_light& light, int type, bool shadow, float range,
                                  const Fcolor& color) {
        light = ::Render->light_create();
        const bool spot = type == IRender_Light::SPOT;
        light->set_type(spot ? IRender_Light::SPOT : IRender_Light::POINT);
        light->set_shadow(shadow);
        light->set_range(std::max(range, 0.01f));
        light->set_color(color);
        if (spot)
            light->set_cone(cone);
    };
    create_light(torch.spot,
        READ_IF_EXISTS(pSettings, r_s32, section, "torch_render_type", int(IRender_Light::SPOT)),
        READ_IF_EXISTS(pSettings, r_bool, section, "torch_render_shadow", true),
        READ_IF_EXISTS(pSettings, r_float, section, deferred ? "torch_r2_range" : "torch_range", 15.f),
        spot_color);
    create_light(torch.omni,
        READ_IF_EXISTS(pSettings, r_s32, section, "torch_omni_type", int(IRender_Light::POINT)),
        READ_IF_EXISTS(pSettings, r_bool, section, "torch_omni_shadow", false),
        READ_IF_EXISTS(pSettings, r_float, section, deferred ? "torch_r2_omni_range" : "torch_omni_range",
            deferred ? 0.75f : 1.5f),
        omni_color);
    const LPCSTR spot_texture = READ_IF_EXISTS(pSettings, r_string, section, "torch_spot_texture", nullptr);
    if (spot_texture && spot_texture[0])
        torch.spot->set_texture(spot_texture);

    const LPCSTR glow_texture = READ_IF_EXISTS(pSettings, r_string, section, "torch_glow_texture", nullptr);
    if (READ_IF_EXISTS(pSettings, r_bool, section, "create_glow", false) && glow_texture && glow_texture[0]) {
        torch.glow = ::Render->glow_create();
        torch.glow->set_color(spot_color);
        torch.glow->set_texture(glow_texture);
        torch.glow->set_radius(READ_IF_EXISTS(pSettings, r_float, section, "torch_glow_radius", 0.3f));
    }
}

shared_str CWeapon::WeaponActionMotion(LPCSTR base) const {
    const LPCSTR hud = HudSection().c_str();
    const int mode = FireModeForHudBones();
    string64 key;
    if (mode < 0)
        xr_strcpy(key, "mask_firemode_a");
    else
        xr_sprintf(key, "mask_firemode_%d", mode);
    const bool fire_mode_transition = std::strncmp(base, "anm_changefiremode_",
        sizeof("anm_changefiremode_") - 1) == 0;
    const LPCSTR mask = fire_mode_transition ? nullptr :
        READ_IF_EXISTS(pSettings, r_string, hud, key, nullptr);
    const LPCSTR state = IsMisfire() ? "_jammed" : (iAmmoElapsed == 0 ? "_empty" : "");

    string256 motion;
    if (mask && mask[0]) {
        xr_sprintf(motion, "%s%s%s", base, mask, state);
        if (pSettings->line_exist(hud, motion))
            return motion;
    }
    xr_sprintf(motion, "%s%s", base, state);
    if (pSettings->line_exist(hud, motion))
        return motion;
    if (mask && mask[0] && state[0]) {
        xr_sprintf(motion, "%s%s", base, mask);
        if (pSettings->line_exist(hud, motion))
            return motion;
    }
    return pSettings->line_exist(hud, base) ? shared_str(base) : shared_str();
}

void CWeapon::StartTacticalTorchAction() {
    auto& torch = m_tactical_torch;
    torch.switch_pending = false;
    const bool turn_on = !torch.enabled;
    u32 duration = 0;
    shared_str motion;
    auto* item = GetHUDmode() ? HudItemData() : nullptr;
    if (item && !g_player_hud->attached_item(1)) {
        motion = WeaponActionMotion(turn_on ? "anm_torch_on" : "anm_torch_off");
        if (HasWeaponActionMotion(motion))
            duration = PlayHUDMotion(motion, TRUE, this, GetState());
    }

    if (torch.has_switch_sound[turn_on])
        PlaySound(turn_on ? "snd_torch_on" : "snd_torch_off", get_LastFP());

    if (!duration) {
        torch.enabled = turn_on;
        SwitchState(eIdle);
        return;
    }

    const LPCSTR hud = HudSection().c_str();
    torch.action_started = Device.dwTimeGlobal;
    torch.switch_delay = WeaponActionDelay(hud, "lock_time_start_", motion.c_str());
    torch.action_duration = std::max(duration, torch.switch_delay +
        WeaponActionDelay(hud, "lock_time_end_", motion.c_str()));
    torch.switch_pending = true;
    SetPending(TRUE);
}

void CWeapon::SetTacticalTorchEffects(bool active) {
    auto& torch = m_tactical_torch;
    if (torch.active == active)
        return;

    torch.active = active;
    if (torch.spot)
        torch.spot->set_active(active);
    if (torch.omni)
        torch.omni->set_active(active);
    if (torch.glow)
        torch.glow->set_active(active);

    if (auto* model = smart_cast<IKinematics*>(Visual()))
        ApplyBoneList(model, torch.section.c_str(), "torch_cone_bones", active);
}

void CWeapon::UpdateTacticalTorch() {
    auto& torch = m_tactical_torch;
    if (!torch.section.size())
        return;

    const u32 now = Device.dwTimeGlobal;
    if (GetState() == eTacticalTorch && GetNextState() == eTacticalTorch) {
        const u32 elapsed = now - torch.action_started;
        if (torch.switch_pending && elapsed >= torch.switch_delay) {
            torch.switch_pending = false;
            torch.enabled = !torch.enabled;
        }
        if (elapsed >= torch.action_duration && !m_bStopAtEndAnimIsRunning) {
            SetPending(FALSE);
            SwitchState(eIdle);
        }
    }

    auto* actor = smart_cast<CActor*>(H_Parent());
    if (!torch.enabled || !actor || !actor->g_Alive() || actor->inventory().ActiveItem() != this ||
        IsHidden() || GetState() == eHiding || GetNextState() == eHiding ||
        !actor->can_use_dynamic_lights()) {
        SetTacticalTorchEffects(false);
        return;
    }

    const float condition = GetCondition();
    const auto& breaking = torch.breaking;
    if (condition <= breaking.y) {
        torch.damaged = true;
    } else if (condition >= breaking.x) {
        torch.damaged = false;
    } else if (now - torch.damage_checked_at >= 100) {
        const float progress = (breaking.x - condition) / (breaking.x - breaking.y);
        const float start_chance = clampr(breaking.z, 0.f, 1.f);
        torch.damaged = ::Random.randF(0.f, 1.f) < start_chance + progress * (1.f - start_chance);
        torch.damage_checked_at = now;
    }
    if (torch.damaged) {
        SetTacticalTorchEffects(false);
        return;
    }

    if (!torch.spot)
        CreateTacticalTorchLights();

    auto* hud = GetHUDmode() ? HudItemData() : nullptr;
    Fvector position, omni_position, direction;
    if (hud) {
        hud->update(false);
        Fmatrix mount = hud->m_item_transform;
        const u16 bone = torch.bone.size() ? hud->m_model->LL_BoneID(torch.bone.c_str()) : BI_NONE;
        if (bone != BI_NONE)
            mount.mul_43(hud->m_item_transform, hud->m_model->LL_GetTransform(bone));
        mount.transform_tiny(position, torch.hud_offset);
        mount.transform_tiny(omni_position, torch.omni_hud_offset);
        direction = hud->m_item_transform.k;

        const u16 target = torch.direction_bone.size() ?
            hud->m_model->LL_BoneID(torch.direction_bone.c_str()) : BI_NONE;
        if (target != BI_NONE) {
            Fvector target_position, target_direction;
            hud->m_item_transform.transform_tiny(target_position, hud->m_model->LL_GetTransform(target).c);
            target_direction.sub(target_position, position);
            if (target_direction.square_magnitude() > EPS_S)
                direction = target_direction;
        }
    } else {
        UpdateXForm();
        XFORM().transform_tiny(position, torch.world_offset);
        XFORM().transform_tiny(omni_position, torch.omni_world_offset);
        direction = get_LastFD();
    }

    direction.normalize_safe();
    Fvector up, right;
    Fvector::generate_orthonormal_basis_normalized(direction, up, right);
    torch.spot->set_hud_mode(hud != nullptr);
    torch.spot->set_position(position);
    torch.spot->set_rotation(direction, right);
    torch.omni->set_hud_mode(hud != nullptr);
    torch.omni->set_position(omni_position);
    torch.omni->set_rotation(direction, right);
    if (torch.glow)
        torch.glow->set_position(position);

    SetTacticalTorchEffects(true);
}

void CWeapon::LoadWeaponLaser(LPCSTR section) {
    auto& laser = m_weapon_laser;
    if (laser.section.size() && xr_strcmp(laser.section.c_str(), section) == 0)
        return;

    StopWeaponLaserEffects();
    laser.section = section;
    laser.bone = READ_IF_EXISTS(pSettings, r_string, section, "laserdot_attach_bone", nullptr);
    laser.dots.clear();
    laser.green = false;
    laser.checked_effects = false;
    laser.damaged = false;
    laser.damage_checked_at = Device.dwTimeGlobal - 100;
    Fvector zero;
    zero.set(0.f, 0.f, 0.f);
    laser.hud_offset = WeaponAccessoryOffset(section, "laserdot_attach_offset", zero);
    laser.world_offset = WeaponAccessoryOffset(section, "laserdot_world_attach_offset", zero);
    laser.breaking.set(-1.f, -1.f, 0.f);
    if (pSettings->line_exist(section, "laser_breaking_params"))
        laser.breaking = pSettings->r_fvector3(section, "laser_breaking_params");

    const int count = clampr(READ_IF_EXISTS(pSettings, r_s32, section, "particles_count", 1), 1, 64);
    float previous_distance = 0.f;
    for (int i = 0; i < count; ++i) {
        string64 key;
        xr_sprintf(key, "laserdot_particle_%d", i);
        const LPCSTR effect = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
        xr_sprintf(key, "laserdot_dist_%d", i);
        const float distance = READ_IF_EXISTS(pSettings, r_float, section, key, previous_distance);
        if (i != 0 && std::isfinite(distance))
            previous_distance = std::max(previous_distance, distance);
        if (effect && effect[0] && xr_strcmp(effect, "none") != 0) {
            if (laser.dots.empty())
                laser.green = std::strstr(effect, "green") != nullptr;
            laser.dots.push_back({effect, previous_distance});
        }
    }

    const shared_str weapon = cNameSect();
    const LPCSTR sounds[] = {"snd_laser_off", "snd_laser_on"};
    for (int i = 0; i < 2; ++i) {
        const LPCSTR sound_section = pSettings->line_exist(section, sounds[i]) ?
            section : weapon.c_str();
        if (laser.has_switch_sound[i])
            continue;
        const LPCSTR value = READ_IF_EXISTS(pSettings, r_string, sound_section, sounds[i], nullptr);
        if (!value || !value[0])
            continue;
        string256 path;
        _GetItem(value, 0, path);
        if (!path[0] || xr_strcmp(path, "$silent") == 0)
            continue;
        m_sounds.LoadSound(sound_section, sounds[i], sounds[i], false);
        laser.has_switch_sound[i] = true;
    }
}

void CWeapon::StartWeaponLaserAction() {
    auto& laser = m_weapon_laser;
    const bool turn_on = !laser.enabled;
    u32 duration = 0;
    shared_str motion;
    if (GetHUDmode() && !g_player_hud->attached_item(1)) {
        motion = WeaponActionMotion(turn_on ? "anm_laser_on" : "anm_laser_off");
        if (HasWeaponActionMotion(motion))
            duration = PlayHUDMotion(motion, TRUE, this, eLaserSwitch);
    }

    if (laser.has_switch_sound[turn_on])
        PlaySound(turn_on ? "snd_laser_on" : "snd_laser_off", get_LastFP());

    if (!duration) {
        laser.enabled = turn_on;
        SwitchState(eIdle);
        return;
    }

    const LPCSTR hud = HudSection().c_str();
    laser.action_started = Device.dwTimeGlobal;
    laser.switch_delay = WeaponActionDelay(hud, "lock_time_start_", motion.c_str());
    laser.action_duration = std::max(duration, laser.switch_delay +
        WeaponActionDelay(hud, "lock_time_end_", motion.c_str()));
    laser.switch_pending = true;
    SetPending(TRUE);
}

void CWeapon::SetLaserBeamVisible(bool visible) {
    auto& laser = m_weapon_laser;
    if (laser.beam_visible == visible)
        return;

    laser.beam_visible = visible;
    if (auto* model = smart_cast<IKinematics*>(Visual()))
        ApplyBoneList(model, laser.section.c_str(), "laser_ray_bones", visible);
    if (auto* hud = HudItemData())
        ApplyBoneList(hud->m_model, laser.section.c_str(), "laser_ray_bones", visible);
}

void CWeapon::StopLaserDot() {
    if (auto* dot = m_weapon_laser.dot)
        dot->Stop(FALSE);
    CParticlesObject::Destroy(m_weapon_laser.dot);
    m_weapon_laser.current_effect = nullptr;
    m_weapon_laser.draw_hud_impact = false;
}

bool CWeapon::HasActiveHudLaser() {
    const auto& laser = m_weapon_laser;
    return laser.enabled && !laser.damaged && GetHUDmode() && !IsHidden() &&
        GetState() != eHiding && GetNextState() != eHiding;
}

bool CWeapon::TraceHudLaserImpact(Fvector& position, bool& green) {
    const auto& laser = m_weapon_laser;
    if (!laser.draw_hud_impact || !GetHUDmode() || !AllowsWeaponLaserEffects())
        return false;

    // Use the crosshair's world ray independently of HUD animation and weapon sway.
    const Fvector& origin = Device.vCameraPosition;
    const Fvector& direction = Device.vCameraDirection;
    collide::rq_result hit;
    if (!Level().ObjectSpace.RayPick(origin, direction, 300.f, collide::rqtBoth, hit, H_Parent()))
        return false;

    position.mad(origin, direction, hit.range);
    green = laser.green;
    return true;
}

void CWeapon::StopWeaponLaserEffects() {
    SetLaserBeamVisible(false);
    StopLaserDot();
}

bool CWeapon::AllowsWeaponLaserEffects() const {
    const auto ready = [](u32 state) {
        return state == eIdle || state == eFire || state == eFire2 ||
               state == eMagEmpty || state == eMisfire;
    };
    if (!ready(GetState()) || !ready(GetNextState()))
        return false;

    if (const auto* actor = smart_cast<const CActor*>(H_Parent())) {
        const auto* item_use = actor->GetItemUseController();
        if ((item_use && item_use->IsWeaponLocked()) || (actor->MovingState() & mcClimb))
            return false;
        if (!IsZoomed() && (actor->MovingState() & mcSprint))
            return false;
    }

    const LPCSTR motion = m_current_motion.c_str();
    return IsZoomed() || !m_bStopAtEndAnimIsRunning || !motion ||
        std::strncmp(motion, "anm_idle_sprint_", sizeof("anm_idle_sprint_") - 1) != 0;
}

void CWeapon::UpdateWeaponLaser() {
    auto& laser = m_weapon_laser;
    if (!laser.section.size())
        return;

    const u32 now = Device.dwTimeGlobal;
    if (GetState() == eLaserSwitch && GetNextState() == eLaserSwitch) {
        const u32 elapsed = now - laser.action_started;
        if (laser.switch_pending && elapsed >= laser.switch_delay) {
            laser.enabled = !laser.enabled;
            laser.switch_pending = false;
        }
        if (elapsed >= laser.action_duration && !m_bStopAtEndAnimIsRunning) {
            SetPending(FALSE);
            SwitchState(eIdle);
        }
    }

    auto* actor = smart_cast<CActor*>(H_Parent());
    if (!laser.enabled || !actor || !actor->g_Alive() ||
        actor->inventory().ActiveItem() != this || IsHidden() ||
        !AllowsWeaponLaserEffects()) {
        StopWeaponLaserEffects();
        return;
    }

    const float condition = GetCondition();
    const auto& breaking = laser.breaking;
    if (condition <= breaking.y) {
        laser.damaged = true;
    } else if (condition >= breaking.x) {
        laser.damaged = false;
    } else if (now - laser.damage_checked_at >= 100) {
        const float progress = (breaking.x - condition) / (breaking.x - breaking.y);
        const float chance = clampr(breaking.z, 0.f, 1.f);
        laser.damaged = ::Random.randF(0.f, 1.f) < chance + progress * (1.f - chance);
        laser.damage_checked_at = now;
    }
    if (laser.damaged) {
        StopWeaponLaserEffects();
        return;
    }

    const LPCSTR hud_section = HudSection().c_str();
    const bool hide_gl_dot = UpgradeGrenadeMode() &&
        READ_IF_EXISTS(pSettings, r_bool, hud_section, "disable_laserdot_when_gl_enabled", false);
    SetLaserBeamVisible(!hide_gl_dot ||
        !READ_IF_EXISTS(pSettings, r_bool, hud_section, "disable_laserray_when_gl_enabled", false));
    if (hide_gl_dot) {
        StopLaserDot();
        return;
    }

    laser.draw_hud_impact = false;
    if (GetHUDmode() && HudItemData()) {
        if (laser.dot)
            StopLaserDot();
        laser.draw_hud_impact = true;
        return;
    }

    if (laser.dots.empty())
        return;

    UpdateXForm();
    Fvector origin;
    XFORM().transform_tiny(origin, laser.world_offset);
    Fvector direction = get_LastFD();
    direction.normalize_safe();
    collide::rq_result hit;
    if (direction.square_magnitude() < EPS_S ||
        !Level().ObjectSpace.RayPick(origin, direction, 300.f, collide::rqtBoth, hit, actor)) {
        StopLaserDot();
        return;
    }
    Fvector position;
    position.mad(origin, direction, std::max(0.f, hit.range - 0.01f));

    if (!laser.checked_effects) {
        laser.dots.erase(std::remove_if(laser.dots.begin(), laser.dots.end(),
            [&laser](const LaserDot& dot) {
                if (::Render->model_HasParticles(dot.effect.c_str()))
                    return false;
                Msg("! [%s] Laser dot particle not found: %s", laser.section.c_str(), dot.effect.c_str());
                return true;
            }), laser.dots.end());
        laser.checked_effects = true;
    }
    if (laser.dots.empty())
        return;

    const LaserDot* selected = &laser.dots.front();
    for (const auto& dot : laser.dots) {
        if (dot.distance > hit.range)
            break;
        selected = &dot;
    }
    if (laser.current_effect != selected->effect) {
        StopLaserDot();
        laser.current_effect = selected->effect;
        laser.dot = CParticlesObject::Create(laser.current_effect.c_str(), FALSE);
    }

    Fmatrix transform;
    transform.translate(position);
    laser.dot->SetXFORM(transform);
    if (!laser.dot->IsPlaying())
        laser.dot->Play(false);
}

bool CWeapon::IsHudActionLocked() const {
    return GetState() == eTacticalTorch || GetNextState() == eTacticalTorch ||
           GetState() == eBayonetStrike || GetNextState() == eBayonetStrike ||
           GetState() == eLaserSwitch || GetNextState() == eLaserSwitch ||
           GetState() == eFireModeSwitch || GetNextState() == eFireModeSwitch ||
           GetState() == eUnloadMagazine || GetNextState() == eUnloadMagazine;
}

bool CWeapon::HasWeaponActionMotion(const shared_str& motion) {
    auto* item = GetHUDmode() ? HudItemData() : nullptr;
    if (!item || !motion.size())
        return false;

    string256 alias;
    xr_sprintf(alias, "%s%s", motion.c_str(),
        item->m_attach_place_idx == 1 && ui_core::is_widescreen() ? "_16x9" : "");
    const auto* hud_motion = item->m_hand_motions.find_motion(alias);
    return hud_motion && !hud_motion->m_animations.empty();
}

shared_str CWeapon::EquipmentToggleMotion(bool night_vision, bool switch_on) {
    const LPCSTR disabled = night_vision ? "disable_nv_anim" : "disable_headlamp_anim";
    if (READ_IF_EXISTS(pSettings, r_bool, cNameSect().c_str(), disabled, false) ||
        READ_IF_EXISTS(pSettings, r_bool, HudSection().c_str(), disabled, false))
        return {};

    const LPCSTR base = night_vision ? (switch_on ? "anm_nv_on" : "anm_nv_off")
                                    : (switch_on ? "anm_headlamp_on" : "anm_headlamp_off");
    const shared_str motion = WeaponActionMotion(base);
    return HasWeaponActionMotion(motion) ? motion : shared_str();
}

void CWeapon::StopEquipmentToggleMotion(const shared_str& motion) {
    if (GetState() == eIdle && GetNextState() == eIdle && m_current_motion == motion) {
        StopCurrentAnimWithoutCallback();
        if (GetHUDmode())
            PlayAnimIdle();
    }
}

void CWeapon::LoadBayonet(LPCSTR section) {
    if (!pSettings->line_exist(section, "kick_hit_power") ||
        !pSettings->line_exist(section, "kick_distance"))
        return;

    m_bayonet_section = section;
    const shared_str weapon = cNameSect();
    const LPCSTR sound = READ_IF_EXISTS(pSettings, r_string, weapon, "snd_kick", nullptr);
    if (!m_bayonet_has_sound && sound && sound[0] && xr_strcmp(sound, "$silent") != 0) {
        m_sounds.LoadSound(weapon.c_str(), "snd_kick", "sndBayonet", false);
        m_bayonet_has_sound = true;
    }
}

LPCSTR CWeapon::BayonetSettingSection(LPCSTR key) const {
    if (m_bayonet_section.size() && pSettings->line_exist(m_bayonet_section, key))
        return m_bayonet_section.c_str();
    return cNameSect_str();
}

bool CWeapon::TryBayonetStrike() {
    if (!m_bayonet_section.size() || !IsGameTypeSingle() || !GetHUDmode())
        return false;

    const auto disabled = [this](LPCSTR key) {
        return READ_IF_EXISTS(pSettings, r_bool, BayonetSettingSection(key), key, false);
    };
    if (disabled("disable_kick_anim") ||
        (IsScopeAttached() && disabled("disable_kick_anim_when_scope_attached")) ||
        (IsSilencerAttached() && disabled("disable_kick_anim_when_sil_attached")) ||
        (IsGrenadeLauncherAttached() && disabled("disable_kick_anim_when_gl_attached")) ||
        (UpgradeGrenadeMode() && disabled("disable_kick_anim_when_gl_enabled")) ||
        pSettings->r_float(m_bayonet_section, "kick_distance") <= 0.f ||
        !HasWeaponActionMotion(WeaponActionMotion("anm_kick")))
        return false;

    // An eligible bayonet owns this input even when the weapon is briefly busy.
    if (GetState() != eIdle || GetNextState() != eIdle || IsPending() || IsZoomed())
        return true;

    if (m_bStopAtEndAnimIsRunning) {
        const LPCSTR motion = m_current_motion.c_str();
        const bool movement_idle = m_startedMotionState == eIdle && motion &&
            (std::strcmp(motion, "anm_idle") == 0 ||
             std::strncmp(motion, "anm_idle_", sizeof("anm_idle_") - 1) == 0);
        if (!movement_idle)
            return true;
    }

    SwitchState(eBayonetStrike);
    return true;
}

void CWeapon::StartBayonetStrike() {
    const shared_str motion = WeaponActionMotion("anm_kick");
    if (!HasWeaponActionMotion(motion)) {
        SwitchState(eIdle);
        return;
    }

    SetPending(TRUE);
    const u32 duration = PlayHUDMotion(motion, FALSE, this, eBayonetStrike);
    const LPCSTR hud = HudSection().c_str();
    const u32 hit_time = WeaponActionDelay(hud, "lock_time_start_", motion.c_str());
    m_bayonet_start_time = Device.dwTimeGlobal;
    m_bayonet_hit_time = hit_time ? hit_time : duration / 2;
    m_bayonet_duration = std::max(duration, m_bayonet_hit_time +
        WeaponActionDelay(hud, "lock_time_end_", motion.c_str()));
    m_bayonet_hit_pending = true;
    if (m_bayonet_has_sound)
        PlaySound("sndBayonet", get_LastFP());
}

void CWeapon::UpdateBayonetStrike() {
    if (GetState() != eBayonetStrike || GetNextState() != eBayonetStrike)
        return;

    const u32 elapsed = Device.dwTimeGlobal - m_bayonet_start_time;
    if (m_bayonet_hit_pending && elapsed >= m_bayonet_hit_time) {
        m_bayonet_hit_pending = false;
        PerformBayonetHit();
    }
    if (elapsed >= m_bayonet_duration && !m_bStopAtEndAnimIsRunning) {
        SetPending(FALSE);
        SwitchState(eIdle);
    }
}

void CWeapon::PerformBayonetHit() {
    auto* actor = smart_cast<CActor*>(H_Parent());
    if (!actor || !actor->g_Alive() || actor->inventory().ActiveItem() != this)
        return;

    const auto setting = [this](LPCSTR key, float fallback) {
        return READ_IF_EXISTS(pSettings, r_float, BayonetSettingSection(key), key, fallback);
    };
    const float distance = setting("kick_distance", 0.f);
    const float power = setting("kick_hit_power", 0.f);
    if (distance <= 0.f || power <= 0.f)
        return;

    const float impulse = setting("kick_hit_impulse", 0.f);
    const int type = READ_IF_EXISTS(pSettings, r_s32, BayonetSettingSection("kick_hit_type"),
        "kick_hit_type", int(ALife::eHitTypeWound));
    const auto hit_type = type >= 0 && type < ALife::eHitTypeMax
        ? ALife::EHitType(type) : ALife::eHitTypeWound;

    CCartridge cartridge;
    cartridge.param_s.kAP = setting("kick_ap", EPS_L);
    cartridge.param_s.fWallmarkSize = setting("kick_wallmark_size", 0.05f);
    cartridge.m_flags.set(CCartridge::cfTracer, FALSE);
    cartridge.m_flags.set(CCartridge::cfRicochet, FALSE);
    const LPCSTR material = READ_IF_EXISTS(pSettings, r_string, BayonetSettingSection("kick_material"),
        "kick_material", "objects\\knife");
    cartridge.bullet_material_idx = GMLib.GetMaterialIdx(material);

    Fvector position = Device.vCameraPosition;
    Fvector direction = Device.vCameraDirection;
    actor->g_fireParams(this, position, direction);
    Fvector up, right;
    Fvector::generate_orthonormal_basis_normalized(direction, up, right);
    const float vertical = setting("kick_disp_ver", 0.f);
    const float horizontal = setting("kick_disp_hor", 0.f);
    const int count = clampr(READ_IF_EXISTS(pSettings, r_s32, BayonetSettingSection("kick_hit_count"),
        "kick_hit_count", 1), 1, 32);
    const bool send_hit = SendHitAllowed(actor);
    for (int i = 0; i < count; ++i) {
        const float offset = count > 1 ? 2.f * float(i) / float(count - 1) - 1.f : 0.f;
        Fvector ray = direction;
        ray.mad(up, vertical * offset);
        ray.mad(right, horizontal * offset);
        ray.normalize_safe();
        Level().BulletManager().AddBullet(position, ray, 10000.f, power, impulse,
            actor->ID(), ID(), hit_type, distance, cartridge, 1.f, send_hit);
    }
}

void CWeapon::UpdateHudAmmoBones(const CCartridge* fired) {
    if (!m_advanced_ammo_bones || IsGrenadeLauncherAttached())
        return;

    if (fired)
        m_last_shot_ammo_section = fired->m_ammoSect;
    if (!GetHUDmode())
        return;

    const auto& hud = HudSection();
    if (!fired && GetState() == eFire &&
        READ_IF_EXISTS(pSettings, r_bool, hud, "ammo_params_toggle_shooting", false))
        return;

    const LPCSTR motion = m_current_motion.c_str();
    const bool reloading = GetState() == eReload && !IsMisfire();
    if (reloading && IsTriStateReload() && motion && m_bStopAtEndAnimIsRunning &&
        std::strncmp(motion, "anm_open", sizeof("anm_open") - 1) == 0) {
        string256 key;
        xr_sprintf(key, "ejection_delay_%s", motion);
        const u32 delay = READ_IF_EXISTS(pSettings, r_u32, hud, key, 0);
        if (Device.dwTimeGlobal - m_dwMotionStartTm < delay)
            return;
    }

    u8 ammo_type = m_ammoType;
    if (reloading) {
        if (m_set_next_ammoType_on_reload < m_ammoTypes.size() &&
            (IsTriStateReload() ||
             READ_IF_EXISTS(pSettings, r_bool, hud, "ammo_params_changing_when_reload_starts", false)))
            ammo_type = m_set_next_ammoType_on_reload;
    } else {
        const shared_str* ammo_section = nullptr;
        if (fired)
            ammo_section = &fired->m_ammoSect;
        else if (m_last_shot_ammo_section.c_str() &&
                 READ_IF_EXISTS(pSettings, r_bool, hud, "ammo_params_use_previous_shot_type", false))
            ammo_section = &m_last_shot_ammo_section;
        else if (!m_magazine.empty() &&
                 READ_IF_EXISTS(pSettings, r_bool, hud, "ammo_params_use_last_cartridge_type", false))
            ammo_section = &m_magazine.back().m_ammoSect;

        if (ammo_section) {
            const auto it = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), *ammo_section);
            if (it != m_ammoTypes.end())
                ammo_type = static_cast<u8>(it - m_ammoTypes.begin());
        }
    }

    int count = static_cast<int>(m_magazine.size());
    if (!fired)
        AdjustHudAmmoState(ammo_type, count, m_last_shot_ammo_section);

    string64 key;
    xr_sprintf(key, "ammo_params_section_%u", static_cast<unsigned>(ammo_type));
    LPCSTR section = READ_IF_EXISTS(pSettings, r_string, hud, key, nullptr);
    if (!section || !pSettings->section_exist(section))
        section = READ_IF_EXISTS(pSettings, r_string, hud, "ammo_params_section", nullptr);
    if (!section || !pSettings->section_exist(section))
        return;

    if (count > 0 &&
        ((reloading && !IsTriStateReload() && motion && !std::strstr(motion, "_empty") &&
          READ_IF_EXISTS(pSettings, r_bool, hud, "minus_ammo_in_usual_reloads", false)) ||
         (motion && std::strncmp(motion, "anm_bore", sizeof("anm_bore") - 1) == 0 &&
          READ_IF_EXISTS(pSettings, r_bool, hud, "minus_ammo_in_bore", false))))
        --count;
    if (IsMisfire() &&
        READ_IF_EXISTS(pSettings, r_bool, section, "additional_ammo_bone_when_jammed", false))
        ++count;

    xr_sprintf(key, "configuration_%d", count);
    auto* model = HudItemData()->m_model;
    if (count > 12 && !pSettings->line_exist(section, key) &&
        pSettings->line_exist(section, "configuration_12") &&
        model->LL_BoneID("mui_amount_12") != BI_NONE)
        xr_strcpy(key, "configuration_12");
    const LPCSTR visible = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
    ApplyBoneList(model, section, "all_bones", FALSE, visible);
    ApplyBoneList(model, section, key, TRUE);
}

void CWeapon::UpdateHUDAddonsVisibility() { // actor only
    if (!GetHUDmode() || !HudItemData())
        return;

    //.	return;

    if (ScopeAttachable()) {
        HudItemData()->set_bone_visible(wpn_scope, IsScopeAttached(), TRUE);
    }

    if (m_eScopeStatus == ALife::eAddonDisabled) {
        HudItemData()->set_bone_visible(wpn_scope, FALSE, TRUE);
    } else if (m_eScopeStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(wpn_scope, TRUE, TRUE);

    if (SilencerAttachable()) {
        HudItemData()->set_bone_visible(wpn_silencer, IsSilencerAttached(), TRUE);
    }
    if (m_eSilencerStatus == ALife::eAddonDisabled) {
        HudItemData()->set_bone_visible(wpn_silencer, FALSE, TRUE);
    } else if (m_eSilencerStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(wpn_silencer, TRUE, TRUE);

    if (GrenadeLauncherAttachable()) {
        HudItemData()->set_bone_visible(wpn_grenade_launcher, IsGrenadeLauncherAttached(), TRUE);
    }
    if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled) {
        HudItemData()->set_bone_visible(wpn_grenade_launcher, FALSE, TRUE);
    } else if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(wpn_grenade_launcher, TRUE, TRUE);
    ApplyConfiguredBoneVisibility(HudItemData()->m_model);
    UpdateHudAmmoBones();
}

void CWeapon::UpdateAddonsVisibility() {
    ApplyUpgradeWorldVisual();
    RefreshUpgradeHud();
    IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual());
    R_ASSERT(pWeaponVisual);

    u16 bone_id;
    UpdateHUDAddonsVisibility();

    pWeaponVisual->CalculateBones_Invalidate();

    // TODO: [imdex] remove shared_str
    bone_id = pWeaponVisual->LL_BoneID(*wpn_scope);
    if (ScopeAttachable() && bone_id != BI_NONE) {
        if (IsScopeAttached()) {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        } else {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eScopeStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
        pWeaponVisual->LL_GetBoneVisible(bone_id)) {
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        //		Log("scope", pWeaponVisual->LL_GetBoneVisible		(bone_id));
    }
    // TODO: [imdex] remove shared_str
    bone_id = pWeaponVisual->LL_BoneID(*wpn_silencer);
    if (SilencerAttachable() && bone_id != BI_NONE) {
        if (IsSilencerAttached()) {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        } else {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eSilencerStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
        pWeaponVisual->LL_GetBoneVisible(bone_id)) {
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        //		Log("silencer", pWeaponVisual->LL_GetBoneVisible	(bone_id));
    }

    // TODO: [imdex] remove shared_str
    bone_id = pWeaponVisual->LL_BoneID(*wpn_grenade_launcher);
    if (GrenadeLauncherAttachable() && bone_id != BI_NONE) {
        if (IsGrenadeLauncherAttached()) {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        } else {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }
    if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled && bone_id != BI_NONE &&
        pWeaponVisual->LL_GetBoneVisible(bone_id)) {
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        //		Log("gl", pWeaponVisual->LL_GetBoneVisible			(bone_id));
    }

    ApplyConfiguredBoneVisibility(pWeaponVisual);
    UpdateWorldAnimation(H_Parent() ? GetState() : u32(eIdle));
    pWeaponVisual->CalculateBones_Invalidate();
    pWeaponVisual->CalculateBones(TRUE);
}

void CWeapon::InitAddons() {
    shared_str lens_scope;
    if (UsesGunslingerScopeLens()) {
        lens_scope = m_eScopeStatus == ALife::eAddonAttachable && m_cur_scope < m_scopes.size()
            ? m_scopes[m_cur_scope] : cNameSect();
    }
    const bool new_scope = lens_scope != m_gunslinger_lens_scope;
    if (new_scope) {
        m_gunslinger_lens_scope = lens_scope;
        m_gunslinger_lens_position = 0.f;
        m_gunslinger_lens_target = 0.f;
    }
    LoadScopeBrightness(new_scope);
    const float speed = m_gunslinger_lens_scope.size() ? READ_IF_EXISTS(pSettings, r_float,
        ScopeLensUpgradeSection("lens_speed"), "lens_speed", 0.f) : 0.f;
    m_gunslinger_lens_speed = std::isfinite(speed) ? std::max(0.f, speed) : 0.f;

    for (const shared_str& effect : m_scope_upgrade_sections) {
        process_if_exists_set(effect.c_str(), "scope_dynamic_zoom", &CInifile::r_bool,
                              m_zoom_params.m_bUseDynamicZoom, false);
        process_if_exists_set(effect.c_str(), "scope_nightvision", &CInifile::r_string_wb,
                              m_zoom_params.m_sUseZoomPostprocess, false);
        process_if_exists_set(effect.c_str(), "scope_alive_detector", &CInifile::r_string_wb,
                              m_zoom_params.m_sUseBinocularVision, false);
    }
}

bool CWeapon::UsesScopeZoom() const {
    return IsScopeAttached() && !m_bAlternativeAimActive;
}

bool CWeapon::UsesGunslingerZoom() const {
    return UsesScopeZoom()
        ? READ_IF_EXISTS(pSettings, r_bool, ScopeSettingSection("gunslinger_zoom"),
                         "gunslinger_zoom", m_gunslinger_zoom)
        : m_gunslinger_zoom;
}

float CWeapon::ZoomCameraFov(float base_fov) const {
    return UsesGunslingerZoom() ? FovWithMagnification(base_fov, GetZoomFactor())
                                : GetZoomFactor() * 0.75f;
}

float CWeapon::CurrentZoomFactor() {
    if (UsesScopeZoom())
        return m_zoom_params.m_fScopeZoomFactor;
    if (m_gunslinger_zoom && m_bAlternativeAimActive) {
        const shared_str section = AlternativeAimSettingSection("alter_scope_zoom_factor");
        return section.size() ? pSettings->r_float(section, "alter_scope_zoom_factor") : 1.f;
    }

    const float ironsight = m_zoom_params.m_fIronSightZoomFactor;
    if (m_gunslinger_zoom && !IsScopeAttached())
        return READ_IF_EXISTS(pSettings, r_float, cNameSect(), "nonscoped_zoom_factor", ironsight);
    return ironsight;
}
void GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor);
void CWeapon::OnZoomIn() {
    m_zoom_params.m_bIsZoomModeNow = true;
    // Screen Space Shaders - aim depth of field. Only the player's own weapon: an NPC aiming must
    // not move the player's focus.
    if (H_Parent() == static_cast<CObject*>(Actor()))
        ssfx_script_bridge::on_weapon_zoom(true);
    if (IsScopeAttached()) {
        const shared_str scope_section = ScopeSettingSection("scope_3d");
        const bool global = pSettings->section_exist("shader_extensions") &&
            pSettings->line_exist("shader_extensions", "enable_3d_scopes") &&
            pSettings->r_bool("shader_extensions", "enable_3d_scopes");
        const bool configured = READ_IF_EXISTS(pSettings, r_bool, scope_section, "scope_3d",
            UsesGunslingerScopeLens());
        Msg("* ScopeLens: weapon=%s optic=%s R4=%d global=%d scope_3d=%d lens_fov=%.1f active=%d",
            cNameSect().c_str(), scope_section.c_str(), !!psDeviceFlags.test(rsR4),
            !!global, !!configured, ScopeLensFov(), !!Is3DScopeEnabled());
        Msg("* ScopeLensEffect: wrapper=%s config=%s section=%s effect=%s mode=%u",
            m_eScopeStatus == ALife::eAddonAttachable && m_cur_scope < m_scopes.size()
                ? m_scopes[m_cur_scope].c_str() : cNameSect().c_str(),
            ScopeSettingSection("scope_lens_effect").c_str(),
            ScopeSettingSection("scope_nightvision").c_str(),
            m_zoom_params.m_sUseZoomPostprocess.size()
                ? m_zoom_params.m_sUseZoomPostprocess.c_str() : "none",
            (u32)ScopeLensMode());
    }
    const float zoom = CurrentZoomFactor();
    if (!m_zoom_params.m_bUseDynamicZoom || !UsesScopeZoom()) {
        SetZoomFactor(zoom);
    } else if (UsesGunslingerZoom()) {
        SetZoomFactor(std::isfinite(zoom) && zoom > 1.f
            ? clampr(m_fRTZoomFactor, MinScopeMagnification(zoom), zoom) : zoom);
    } else {
        SetZoomFactor(m_fRTZoomFactor);
    }

    EnableHudInertion(FALSE);

    // if(m_zoom_params.m_bZoomDofEnabled && !IsScopeAttached())
    //	GamePersistent().SetEffectorDOF	(m_zoom_params.m_ZoomDof);

    if (GetHUDmode())
        GamePersistent().SetPickableEffectorDOF(true);

    if (!m_bAlternativeAimActive && m_zoom_params.m_sUseBinocularVision.size() &&
        IsScopeAttached() &&
        NULL == m_zoom_params.m_pVision)
        m_zoom_params.m_pVision =
            xr_new<CBinocularsVision>(m_zoom_params.m_sUseBinocularVision /*"wpn_binoc"*/);

    if (!m_bAlternativeAimActive && m_zoom_params.m_sUseZoomPostprocess.size() &&
        IsScopeAttached()) {
        CActor* pA = smart_cast<CActor*>(H_Parent());
        if (pA) {
            if (NULL == m_zoom_params.m_pNight_vision) {
                m_zoom_params.m_pNight_vision = xr_new<CNightVisionEffector>(
                    m_zoom_params.m_sUseZoomPostprocess /*"device_torch"*/);
            }
        }
    }
}

void CWeapon::OnZoomOut() {
    // Screen Space Shaders - see OnZoomIn.
    if (H_Parent() == static_cast<CObject*>(Actor()))
        ssfx_script_bridge::on_weapon_zoom(false);
    const bool was_alternative_aim = m_bAlternativeAimActive;
    if (IsZoomed() && UsesScopeZoom() && m_zoom_params.m_bUseDynamicZoom)
        m_fRTZoomFactor = GetZoomFactor();
    m_zoom_params.m_bIsZoomModeNow = false;
    if (!was_alternative_aim || fis_zero(m_zoom_params.m_fZoomRotationFactor))
        m_bAlternativeAimActive = false;
    if (fis_zero(m_zoom_params.m_fZoomRotationFactor))
        m_fAlternativeAimFactor = 0.f;
    m_bAlternativeAimOwnsZoom = false;
    SetZoomFactor(UsesGunslingerZoom() ? 1.f : g_fov);
    EnableHudInertion(TRUE);

    // 	GamePersistent().RestoreEffectorDOF	();

    if (GetHUDmode())
        GamePersistent().SetPickableEffectorDOF(false);

    ResetSubStateTime();

    xr_delete(m_zoom_params.m_pVision);
    if (m_zoom_params.m_pNight_vision) {
        m_zoom_params.m_pNight_vision->Stop(100000.0f, false);
        xr_delete(m_zoom_params.m_pNight_vision);
    }
}

CUIWindow* CWeapon::ZoomTexture() {
    if (UseScopeTexture() && !m_bAlternativeAimActive)
        return m_UIScope;
    else
        return NULL;
}

void CWeapon::SwitchState(u32 S) {
    if (OnClient())
        return;

#ifndef MASTER_GOLD
    if (bDebug) {
        Msg("---Server is going to send GE_WPN_STATE_CHANGE to [%d], weapon_section[%s], "
            "parent[%s]",
            S, cNameSect().c_str(), H_Parent() ? H_Parent()->cName().c_str() : "NULL Parent");
    }
#endif // #ifndef MASTER_GOLD

    SetNextState(S);
    if (CHudItem::object().Local() && !CHudItem::object().getDestroy() && m_pInventory &&
        OnServer()) {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
        P.w_u8(u8(S));
        P.w_u8(u8(m_sub_state));
        P.w_u8(m_ammoType);
        P.w_u8(u8(iAmmoElapsed & 0xff));
        P.w_u8(m_set_next_ammoType_on_reload);
        CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
    }
}

void CWeapon::OnMagazineEmpty() { VERIFY((u32)iAmmoElapsed == m_magazine.size()); }

void CWeapon::reinit() {
    CShootingObject::reinit();
    CHudItemObject::reinit();
    m_last_shot_ammo_section = shared_str();
}

void CWeapon::reload(LPCSTR section) {
    CShootingObject::reload(section);
    CHudItemObject::reload(section);

    m_can_be_strapped = true;
    m_strapped_mode = false;

    if (pSettings->line_exist(section, "strap_bone0"))
        m_strap_bone0 = pSettings->r_string(section, "strap_bone0");
    else
        m_can_be_strapped = false;

    if (pSettings->line_exist(section, "strap_bone1"))
        m_strap_bone1 = pSettings->r_string(section, "strap_bone1");
    else
        m_can_be_strapped = false;

    if (m_eScopeStatus == ALife::eAddonAttachable) {
        m_addon_holder_range_modifier = READ_IF_EXISTS(
            pSettings, r_float, GetScopeName(), "holder_range_modifier", m_holder_range_modifier);
        m_addon_holder_fov_modifier = READ_IF_EXISTS(pSettings, r_float, GetScopeName(),
                                                     "holder_fov_modifier", m_holder_fov_modifier);
    } else {
        m_addon_holder_range_modifier = m_holder_range_modifier;
        m_addon_holder_fov_modifier = m_holder_fov_modifier;
    }

    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "position");
        ypr = pSettings->r_fvector3(section, "orientation");
        ypr.mul(PI / 180.f);

        m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
        m_Offset.translate_over(pos);
    }

    m_StrapOffset = m_Offset;
    if (pSettings->line_exist(section, "strap_position") &&
        pSettings->line_exist(section, "strap_orientation")) {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "strap_position");
        ypr = pSettings->r_fvector3(section, "strap_orientation");
        ypr.mul(PI / 180.f);

        m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
        m_StrapOffset.translate_over(pos);
    } else
        m_can_be_strapped = false;

    m_ef_main_weapon_type =
        READ_IF_EXISTS(pSettings, r_u32, section, "ef_main_weapon_type", u32(-1));
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_weapon_type", u32(-1));
}

void CWeapon::OnChangeVisual() {
    inherited::OnChangeVisual();
    m_world_animation = nullptr;
}

void CWeapon::UpdateWorldAnimation(u32 state, bool shot) {
    if (!m_use_world_anims || PPhysicsShell() || !Visual() || (state == eFire && !shot))
        return;

    auto* animated = Visual()->dcast_PKinematicsAnimated();
    if (!animated)
        return;

    LPCSTR base = "wanm_idle";
    switch (state) {
    case eShowing: base = "wanm_draw"; break;
    case eHiding: base = "wanm_holster"; break;
    case eFire: base = "wanm_shoot"; break;
    case eReload: base = "wanm_reload"; break;
    }

    const shared_str section = cNameSect();
    const auto read_motion = [&](LPCSTR key) {
        return READ_IF_EXISTS(pSettings, r_string, section.c_str(), key, nullptr);
    };
    LPCSTR name = read_motion(base);
    if (!name || !name[0]) {
        base = "wanm_idle";
        name = read_motion(base);
    }

    string128 key;
    xr_strcpy(key, base);
    const auto suffix = [&](LPCSTR value) {
        string128 candidate;
        xr_sprintf(candidate, "%s%s", key, value);
        const LPCSTR variant = read_motion(candidate);
        if (variant && variant[0]) {
            xr_strcpy(key, candidate);
            name = variant;
        }
    };

    if (state == eFire && iAmmoElapsed <= 1)
        suffix("_last");
    if (IsMisfire())
        suffix("_jammed");
    else if (state != eFire && iAmmoElapsed == 0)
        suffix("_empty");

    const int mode = FireModeForHudBones();
    string32 mode_suffix;
    if (mode < 0)
        xr_strcpy(mode_suffix, "_a");
    else
        xr_sprintf(mode_suffix, "_%d", mode);
    suffix(mode_suffix);
    if (UpgradeGrenadeMode())
        suffix("_g");
    else if (IsGrenadeLauncherAttached())
        suffix("_w_gl");

    if (!name || !name[0])
        return;
    MotionID motion = animated->ID_Cycle_Safe(name);
    if (!motion.valid()) {
        name = read_motion("wanm_idle");
        if (!name || !name[0])
            return;
        motion = animated->ID_Cycle_Safe(name);
    }
    if (!motion.valid() || (!shot && m_world_animation == name))
        return;

    animated->PlayCycle(motion, m_world_animation.size() != 0 && !shot);
    m_world_animation = name;
}

void CWeapon::create_physic_shell() {
    // Build the shell from the assembled pose, before physics takes over the bones.
    if (m_use_world_anims) {
        m_world_animation = nullptr;
        UpdateWorldAnimation(eIdle);
        if (auto* model = Visual() ? Visual()->dcast_PKinematics() : nullptr) {
            ApplyConfiguredBoneVisibility(model);
            model->CalculateBones_Invalidate();
            model->CalculateBones(TRUE);
        }
    }
    CPhysicsShellHolder::create_physic_shell();
}

bool CWeapon::ActivationSpeedOverriden(Fvector& dest, bool clear_override) {
    if (m_activation_speed_is_overriden) {
        if (clear_override) {
            m_activation_speed_is_overriden = false;
        }

        dest = m_overriden_activation_speed;
        return true;
    }

    return false;
}

void CWeapon::SetActivationSpeedOverride(Fvector const& speed) {
    m_overriden_activation_speed = speed;
    m_activation_speed_is_overriden = true;
}

void CWeapon::activate_physic_shell() {
    UpdateXForm();
    CPhysicsShellHolder::activate_physic_shell();
}

void CWeapon::setup_physic_shell() { CPhysicsShellHolder::setup_physic_shell(); }

int g_iWeaponRemove = 1;

bool CWeapon::NeedToDestroyObject() const {
    if (GameID() == eGameIDSingle)
        return false;
    if (Remote())
        return false;
    if (H_Parent())
        return false;
    if (g_iWeaponRemove == -1)
        return false;
    if (g_iWeaponRemove == 0)
        return true;
    if (TimePassedAfterIndependant() > m_dwWeaponRemoveTime)
        return true;

    return false;
}

ALife::_TIME_ID CWeapon::TimePassedAfterIndependant() const {
    if (!H_Parent() && m_dwWeaponIndependencyTime != 0)
        return Level().timeServer() - m_dwWeaponIndependencyTime;
    else
        return 0;
}

bool CWeapon::can_kill() const {
    if (GetSuitableAmmoTotal(true) || m_ammoTypes.empty())
        return (true);

    return (false);
}

CInventoryItem* CWeapon::can_kill(CInventory* inventory) const {
    if (GetAmmoElapsed() || m_ammoTypes.empty())
        return (const_cast<CWeapon*>(this));

    TIItemContainer::iterator I = inventory->m_all.begin();
    TIItemContainer::iterator E = inventory->m_all.end();
    for (; I != E; ++I) {
        CInventoryItem* inventory_item = smart_cast<CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i =
            std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

const CInventoryItem* CWeapon::can_kill(const xr_vector<const CGameObject*>& items) const {
    if (m_ammoTypes.empty())
        return (this);

    xr_vector<const CGameObject*>::const_iterator I = items.begin();
    xr_vector<const CGameObject*>::const_iterator E = items.end();
    for (; I != E; ++I) {
        const CInventoryItem* inventory_item = smart_cast<const CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i =
            std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

bool CWeapon::ready_to_kill() const {
    return (!IsMisfire() &&
            ((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) &&
            GetAmmoElapsed());
}

void CWeapon::UpdateHudAdditonal(Fmatrix& trans) {
    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (!pActor)
        return;

    const float alternative_step =
        Device.fTimeDelta / std::max(m_zoom_params.m_fZoomRotateTime, EPS_S);
    if (m_bAlternativeAimActive)
        m_fAlternativeAimFactor += alternative_step;
    else
        m_fAlternativeAimFactor -= alternative_step;
    clamp(m_fAlternativeAimFactor, 0.f, 1.f);

    if ((IsZoomed() && m_zoom_params.m_fZoomRotationFactor <= 1.f) ||
        (!IsZoomed() && m_zoom_params.m_fZoomRotationFactor > 0.f)) {
        u8 idx = GetCurrentHudOffsetIdx();
        //		if(idx==0)					return;

        attachable_hud_item* hi = HudItemData();
        R_ASSERT(hi);
        Fvector curr_offs, curr_rot;
        curr_offs = hi->m_measures.m_hands_offset[0][idx]; // pos,aim
        curr_rot = hi->m_measures.m_hands_offset[1][idx];  // rot,aim

        if (idx == 1 && IsScopeAttached() &&
            m_eScopeStatus == ALife::eAddonAttachable) {
            const bool widescreen =
                hi->m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now);
            LPCSTR pos_key = widescreen ? "aim_hud_offset_pos_16x9"
                                        : "aim_hud_offset_pos";
            LPCSTR rot_key = widescreen ? "aim_hud_offset_rot_16x9"
                                        : "aim_hud_offset_rot";
            if (pSettings->line_exist(ScopeSettingSection(pos_key), pos_key))
                curr_offs = pSettings->r_fvector3(ScopeSettingSection(pos_key), pos_key);
            else if (widescreen && pSettings->line_exist(
                         ScopeSettingSection("aim_hud_offset_pos"),
                         "aim_hud_offset_pos"))
                curr_offs = pSettings->r_fvector3(
                    ScopeSettingSection("aim_hud_offset_pos"), "aim_hud_offset_pos");
            if (pSettings->line_exist(ScopeSettingSection(rot_key), rot_key))
                curr_rot = pSettings->r_fvector3(ScopeSettingSection(rot_key), rot_key);
            else if (widescreen && pSettings->line_exist(
                         ScopeSettingSection("aim_hud_offset_rot"),
                         "aim_hud_offset_rot"))
                curr_rot = pSettings->r_fvector3(
                    ScopeSettingSection("aim_hud_offset_rot"), "aim_hud_offset_rot");
        }

        if (idx == 1 && m_fAlternativeAimFactor > EPS_S) {
            const Fvector primary_offs = curr_offs;
            const Fvector primary_rot = curr_rot;
            Fvector alternative_offs = primary_offs;
            Fvector alternative_rot = primary_rot;
            const bool widescreen =
                hi->m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now);
            LPCSTR pos_key = widescreen ? "alter_aim_hud_offset_pos_16x9"
                                        : "alter_aim_hud_offset_pos";
            LPCSTR rot_key = widescreen ? "alter_aim_hud_offset_rot_16x9"
                                        : "alter_aim_hud_offset_rot";
            shared_str pos_section = AlternativeAimSettingSection(pos_key);
            shared_str rot_section = AlternativeAimSettingSection(rot_key);

            if (!pos_section.size() && widescreen) {
                pos_key = "alter_aim_hud_offset_pos";
                pos_section = AlternativeAimSettingSection(pos_key);
            }
            if (!rot_section.size() && widescreen) {
                rot_key = "alter_aim_hud_offset_rot";
                rot_section = AlternativeAimSettingSection(rot_key);
            }

            if (pos_section.size())
                alternative_offs = pSettings->r_fvector3(pos_section, pos_key);
            if (rot_section.size())
                alternative_rot = pSettings->r_fvector3(rot_section, rot_key);

            curr_offs.lerp(primary_offs, alternative_offs, m_fAlternativeAimFactor);
            curr_rot.lerp(primary_rot, alternative_rot, m_fAlternativeAimFactor);
        }
        curr_offs.mul(m_zoom_params.m_fZoomRotationFactor);
        curr_rot.mul(m_zoom_params.m_fZoomRotationFactor);

        Fmatrix hud_rotation;
        hud_rotation.identity();
        hud_rotation.rotateX(curr_rot.x);

        Fmatrix hud_rotation_y;
        hud_rotation_y.identity();
        hud_rotation_y.rotateY(curr_rot.y);
        hud_rotation.mulA_43(hud_rotation_y);

        hud_rotation_y.identity();
        hud_rotation_y.rotateZ(curr_rot.z);
        hud_rotation.mulA_43(hud_rotation_y);

        hud_rotation.translate_over(curr_offs);
        trans.mulB_43(hud_rotation);

        if (pActor->IsZoomAimingMode())
            m_zoom_params.m_fZoomRotationFactor +=
                Device.fTimeDelta / m_zoom_params.m_fZoomRotateTime;
        else
            m_zoom_params.m_fZoomRotationFactor -=
                Device.fTimeDelta / m_zoom_params.m_fZoomRotateTime;

        clamp(m_zoom_params.m_fZoomRotationFactor, 0.f, 1.f);

        if (!IsZoomed() && fis_zero(m_zoom_params.m_fZoomRotationFactor) &&
            m_bAlternativeAimActive && !m_bAlternativeAimOwnsZoom) {
            m_bAlternativeAimActive = false;
            m_fAlternativeAimFactor = 0.f;
        }
    }

    if (m_gunslinger_hud_movement)
        UpdateGunslingerHudMovement(trans, *pActor);
}

void CWeapon::on_b_hud_detach() {
    StopWeaponLaserEffects();
    if (const auto* data = HudItemData()) {
        m_hud_upgrade_bones.Restore(data->m_model);
        ApplyBoneList(data->m_model, m_tactical_torch.section.c_str(), "torch_cone_bones", FALSE);
        ApplyBoneList(data->m_model, m_weapon_laser.section.c_str(), "laser_ray_bones", FALSE);
    }
    inherited::on_b_hud_detach();
    m_hud_movement_pos.set(0.f, 0.f, 0.f);
    m_hud_movement_rot.set(0.f, 0.f, 0.f);
    m_hud_movement_stance = -1;
    m_hud_movement_frame = u32(-1);
    m_hud_stance_transition = nullptr;
    m_hud_stance_time = 0.f;
    m_hud_lookout_pos.set(0.f, 0.f, 0.f);
    m_hud_lookout_rot.set(0.f, 0.f, 0.f);
    m_hud_lookout = 0;
    m_hud_lookout_transition = nullptr;
    m_hud_lookout_time = 0.f;
}

void CWeapon::UpdateGunslingerHudMovement(Fmatrix& trans, const CActor& actor) {
    const auto* hud = HudItemData();
    if (!hud)
        return;

    if (m_hud_movement_frame != Device.dwFrame) {
        m_hud_movement_frame = Device.dwFrame;
        const auto& section = HudSection();
        const bool widescreen = hud->m_measures.m_prop_flags.test(
            hud_item_measures::e_16x9_mode_now);
        const u32 movement = actor.MovingState();
        const bool slow = (movement & mcAccel) != 0;
        const int stance = (movement & mcCrouch) ? (slow ? 2 : 1) : 0;

        if (m_hud_movement_stance >= 0 && m_hud_movement_stance != stance) {
            m_hud_stance_transition = stance == 2 ? "to_slow_crouch"
                : stance == 0 ? "from_crouch"
                : m_hud_movement_stance == 2 ? "from_slow_crouch" : "to_crouch";
            string128 key;
            xr_sprintf(key, "%s_time", m_hud_stance_transition);
            m_hud_stance_time = READ_IF_EXISTS(pSettings, r_float, section, key, 0.f);
        }
        m_hud_movement_stance = stance;

        Fvector target_pos, target_rot;
        target_pos.set(0.f, 0.f, 0.f);
        target_rot.set(0.f, 0.f, 0.f);
        const auto add_offset = [&](LPCSTR prefix, LPCSTR action, float scale) {
            string128 pos_key, rot_key;
            xr_sprintf(pos_key, "%s%s_offset_pos%s", prefix, action, widescreen ? "_16x9" : "");
            xr_sprintf(rot_key, "%s%s_offset_rot%s", prefix, action, widescreen ? "_16x9" : "");
            if (widescreen && !pSettings->line_exist(section, pos_key))
                xr_sprintf(pos_key, "%s%s_offset_pos", prefix, action);
            if (widescreen && !pSettings->line_exist(section, rot_key))
                xr_sprintf(rot_key, "%s%s_offset_rot", prefix, action);
            if (pSettings->line_exist(section, pos_key))
                target_pos.mad(pSettings->r_fvector3(section, pos_key), scale);
            if (pSettings->line_exist(section, rot_key))
                target_rot.mad(pSettings->r_fvector3(section, rot_key), scale);
        };

        const float dt = Device.fTimeDelta;
        const float aim = m_zoom_params.m_fZoomRotationFactor;
        const float hip = 1.f - aim;
        const bool hiding = GetState() == eHiding;
        float factor = READ_IF_EXISTS(pSettings, r_float, section, "hud_move_stabilize_factor", 2.f);
        if (hiding) {
            factor = READ_IF_EXISTS(pSettings, r_float, section, "hud_move_weaponhide_factor", 1.f);
            m_hud_stance_time = 0.f;
        } else {
            const char* stance_scale_key = stance == 2 ? "hud_move_slow_crouch_factor"
                : stance == 1 ? "hud_move_crouch_factor" : slow ? "hud_move_slow_factor" : nullptr;
            const float stance_scale = stance_scale_key
                ? READ_IF_EXISTS(pSettings, r_float, section, stance_scale_key, 1.f) : 1.f;
            const float direction_scale = hip * stance_scale;
            if (!(movement & (mcClimb | mcJump | mcFall)) && (movement & mcAnyMove)) {
                const bool left = (movement & mcLStrafe) != 0;
                const bool right = (movement & mcRStrafe) != 0;
                const bool forward = (movement & mcFwd) != 0;
                const bool back = (movement & mcBack) != 0;
                if (left != right) {
                    const LPCSTR direction = left ? "left" : "right";
                    const float strafe_scale = stance == 2
                        ? READ_IF_EXISTS(pSettings, r_float, section, "hud_move_slow_crouch_strafe_factor", 0.65f)
                        : stance == 1
                            ? READ_IF_EXISTS(pSettings, r_float, section, "hud_move_crouch_strafe_factor", 0.8f)
                            : stance_scale;
                    add_offset("hud_move_", direction, hip * strafe_scale);
                    if (aim > 0.f)
                        add_offset("hud_aim_move_", direction, aim * strafe_scale);
                }
                if (forward != back)
                    add_offset("hud_move_", forward ? "forward" : "back", direction_scale);
                factor = 1.f;
            }
            if (m_hud_stance_time > 0.f) {
                add_offset("hud_move_", m_hud_stance_transition, hip);
                add_offset("hud_aim_move_", m_hud_stance_transition, aim);
                m_hud_stance_time -= dt;
                factor = 1.f;
            }
            const float aim_factor =
                READ_IF_EXISTS(pSettings, r_float, section, "hud_move_unzoom_factor", 1.f);
            factor += (aim_factor - factor) * aim;
        }

        // Match the original response at one 8 ms step, independent of frame rate.
        const float speed_pos = READ_IF_EXISTS(pSettings, r_float, section, "hud_move_speed_pos", 0.1f);
        const float speed_rot = READ_IF_EXISTS(pSettings, r_float, section, "hud_move_speed_rot", 0.4f);
        const auto blend = [dt](float speed, float factor) {
            const float step = clampr(speed * factor / 100.f, 0.f, 1.f);
            return 1.f - std::pow(1.f - step, dt * 125.f);
        };
        m_hud_movement_pos.mad(Fvector().sub(target_pos, m_hud_movement_pos), blend(speed_pos, factor));
        m_hud_movement_rot.mad(Fvector().sub(target_rot, m_hud_movement_rot), blend(speed_rot, factor));

        // Blend lookout separately so the lean response does not change strafe or crouch.
        target_pos.set(0.f, 0.f, 0.f);
        target_rot.set(0.f, 0.f, 0.f);
        float lookout_factor = 2.8f;
        if (hiding) {
            m_hud_lookout = 0;
            m_hud_lookout_time = 0.f;
            lookout_factor = factor;
        } else {
            u32 lookout = movement & mcLookout;
            if (lookout == mcLookout || (movement & mcClimb))
                lookout = 0;
            if (lookout != m_hud_lookout) {
                if (lookout)
                    m_hud_lookout_transition = lookout == mcLLookout ? "to_llookout" : "to_rlookout";
                else
                    m_hud_lookout_transition = m_hud_lookout == mcLLookout
                        ? "from_llookout" : "from_rlookout";
                string128 key;
                xr_sprintf(key, "%s_time", m_hud_lookout_transition);
                m_hud_lookout_time = READ_IF_EXISTS(pSettings, r_float, section, key, 0.f);
                m_hud_lookout = lookout;
            }
            if (lookout) {
                const LPCSTR side = lookout == mcLLookout ? "llookout" : "rlookout";
                add_offset("hud_move_", side, hip);
                add_offset("hud_aim_move_", side, aim);
                const LPCSTR key = lookout == mcLLookout ? "hud_move_llookout_offset_speed_factor"
                    : "hud_move_rlookout_offset_speed_factor";
                lookout_factor = READ_IF_EXISTS(pSettings, r_float, section, key, lookout_factor);
            }
            if (m_hud_lookout_time > 0.f) {
                add_offset("hud_move_", m_hud_lookout_transition, hip);
                add_offset("hud_aim_move_", m_hud_lookout_transition, aim);
                m_hud_lookout_time -= dt;
            }
        }
        m_hud_lookout_pos.mad(Fvector().sub(target_pos, m_hud_lookout_pos), blend(speed_pos, lookout_factor));
        m_hud_lookout_rot.mad(Fvector().sub(target_rot, m_hud_lookout_rot), blend(speed_rot, lookout_factor));
    }

    Fvector angles;
    angles.add(m_hud_movement_rot, m_hud_lookout_rot);
    angles.mul(PI / 180.f);
    Fmatrix offset;
    offset.setHPB(angles.x, angles.y, angles.z);
    Fvector position;
    position.add(m_hud_movement_pos, m_hud_lookout_pos);
    offset.translate_over(position);
    trans.mulB_43(offset);
}

void CWeapon::SetAmmoElapsed(int ammo_count) {
    iAmmoElapsed = ammo_count;

    u32 uAmmo = u32(iAmmoElapsed);

    if (uAmmo != m_magazine.size()) {
        if (uAmmo > m_magazine.size()) {
            CCartridge l_cartridge;
            l_cartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
            while (uAmmo > m_magazine.size())
                m_magazine.push_back(l_cartridge);
        } else {
            while (uAmmo < m_magazine.size())
                m_magazine.pop_back();
        };
    };
}

u32 CWeapon::ef_main_weapon_type() const {
    VERIFY(m_ef_main_weapon_type != u32(-1));
    return (m_ef_main_weapon_type);
}

u32 CWeapon::ef_weapon_type() const {
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem(const shared_str& item_sect) {
    return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end());
}

void CWeapon::modify_holder_params(float& range, float& fov) const {
    if (!IsScopeAttached()) {
        inherited::modify_holder_params(range, fov);
        return;
    }
    range *= m_addon_holder_range_modifier;
    fov *= m_addon_holder_fov_modifier;
}

bool CWeapon::render_item_ui_query() {
    bool b_is_active_item = (m_pInventory->ActiveItem() == this);
    bool res = b_is_active_item && IsZoomed() && ZoomHideCrosshair() && ZoomTexture() &&
               !IsRotatingToZoom();
    return res;
}

void CWeapon::render_item_ui() {
    if (m_zoom_params.m_pVision)
        m_zoom_params.m_pVision->Draw();

    ZoomTexture()->Update();
    ZoomTexture()->Draw();
}

bool CWeapon::unlimited_ammo() {
    if (IsGameTypeSingle()) {
        if (m_pInventory) {
            return inventory_owner().unlimited_ammo() &&
                   m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
        } else
            return false;
    }

    return false;
};

float CWeapon::Weight() const {
    float res = CInventoryItemObject::Weight();
    if (IsGrenadeLauncherAttached() && GetGrenadeLauncherName().size()) {
        res += pSettings->r_float(GetGrenadeLauncherName(), "inv_weight");
    }
    if (IsScopeAttached() && m_scopes.size()) {
        res += pSettings->r_float(GetScopeName(), "inv_weight");
    }
    if (IsSilencerAttached() && GetSilencerName().size()) {
        res += pSettings->r_float(GetSilencerName(), "inv_weight");
    }

    if (iAmmoElapsed) {
        float w = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "inv_weight");
        float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

        res += w * (iAmmoElapsed / bs);
    }
    return res;
}

bool CWeapon::show_crosshair() { return !IsPending() && (!IsZoomed() || !ZoomHideCrosshair()); }

bool CWeapon::show_indicators() {
    if (IsZoomed() && UsesScopeZoom() && !UpgradeGrenadeMode() && UsesGunslingerScopeLens() &&
        Is3DScopeEnabled() && m_eScopeStatus == ALife::eAddonPermanent &&
        READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "zoom_hide_ui", false))
        return false;

    return !(IsZoomed() && ZoomTexture());
}

float CWeapon::GetConditionToShow() const {
    return (GetCondition()); // powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet() {
    CObject* O = H_Parent();
    CEntityAlive* EA = smart_cast<CEntityAlive*>(O);
    return EA->cast_actor() != 0;
}

BOOL CWeapon::ParentIsActor() {
    CObject* O = H_Parent();
    if (!O)
        return FALSE;

    CEntityAlive* EA = smart_cast<CEntityAlive*>(O);
    if (!EA)
        return FALSE;

    return EA->cast_actor() != 0;
}

extern u32 hud_adj_mode;

void CWeapon::debug_draw_firedeps() {
#ifdef DEBUG
    if (hud_adj_mode == 5 || hud_adj_mode == 6 || hud_adj_mode == 7) {
        CDebugRenderer& render = Level().debug_renderer();

        if (hud_adj_mode == 5)
            render.draw_aabb(get_LastFP(), 0.005f, 0.005f, 0.005f, D3DCOLOR_XRGB(255, 0, 0));

        if (hud_adj_mode == 6)
            render.draw_aabb(get_LastFP2(), 0.005f, 0.005f, 0.005f, D3DCOLOR_XRGB(0, 0, 255));

        if (hud_adj_mode == 7)
            render.draw_aabb(get_LastSP(), 0.005f, 0.005f, 0.005f, D3DCOLOR_XRGB(0, 255, 0));
    }
#endif // DEBUG
}

const float& CWeapon::hit_probability() const {
    VERIFY((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster));
    return (m_hit_probability[egdNovice]);
}

shared_str CWeapon::InspectMotion() const {
    const auto& section = HudSection();
    if (IsMisfire()) {
        if (pSettings->line_exist(section, "anm_boring_jammed"))
            return "anm_boring_jammed";
        if (pSettings->line_exist(section, "anm_bore_jammed"))
            return "anm_bore_jammed";
    }
    if (pSettings->line_exist(section, "anm_boring"))
        return "anm_boring";
    return pSettings->line_exist(section, "anm_bore") ? shared_str("anm_bore") : shared_str();
}

void CWeapon::OnStateSwitch(u32 S) {
    inherited::OnStateSwitch(S);
    m_BriefInfo_CalcFrame = 0;
    UpdateWorldAnimation(H_Parent() ? S : u32(eIdle));

    if (S != eBayonetStrike)
        m_bayonet_hit_pending = false;
    if (S != eLaserSwitch)
        m_weapon_laser.switch_pending = false;

    if (S == eTacticalTorch) {
        StartTacticalTorchAction();
    } else if (S == eLaserSwitch) {
        StartWeaponLaserAction();
    } else if (S == eBayonetStrike) {
        StartBayonetStrike();
    } else if (S == eInspect) {
        SetPending(TRUE);
        const shared_str motion = InspectMotion();
        if (!motion.c_str() || !PlayHUDMotion(motion, TRUE, this, eInspect)) {
            SetPending(FALSE);
            SwitchState(eIdle);
            return;
        }
        const bool custom_inspection = !xr_strcmp(motion.c_str(), "anm_boring") ||
            std::strncmp(motion.c_str(), "anm_boring_", sizeof("anm_boring_") - 1) == 0;
        PlaySound(custom_inspection && pSettings->line_exist(cNameSect().c_str(), "snd_boring")
                      ? "sndInspect" : "sndBore", get_LastFP());
    }

    // if(GetState()==eReload)
    //{
    //	if(H_Parent()==Level().CurrentEntity() && !fsimilar(m_zoom_params.m_ReloadDof.w,-1.0f))
    //	{
    //		CActor* current_actor	= smart_cast<CActor*>(H_Parent());
    //		if (current_actor)
    //			current_actor->Cameras().AddCamEffector(xr_new<CEffectorDOF>(m_zoom_params.m_ReloadDof)
    //);
    //	}
    //}
}

void CWeapon::OnAnimationEnd(u32 state) {
    if (state == eInspect) {
        SetPending(FALSE);
        SwitchState(eIdle);
    } else
        inherited::OnAnimationEnd(state);
}

u8 CWeapon::GetCurrentHudOffsetIdx() {
    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (!pActor)
        return 0;

    bool b_aiming = ((IsZoomed() && m_zoom_params.m_fZoomRotationFactor <= 1.f) ||
                     (!IsZoomed() && m_zoom_params.m_fZoomRotationFactor > 0.f));

    if (!b_aiming)
        return 0;
    else
        return 1;
}

void CWeapon::render_hud_mode() { RenderLight(); }

bool CWeapon::MovingAnimAllowedNow() { return !IsZoomed(); }

bool CWeapon::IsHudModeNow() { return (HudItemData() != NULL); }

void CWeapon::PlayScopeZoomSound(u32 sound) {
    if (m_scope_zoom_sound[sound] && GetHUDmode())
        PlaySound(scope_zoom_sounds[sound], get_LastFP());
}

void CWeapon::UpdateScopeLensZoom() {
    if (m_gunslinger_lens_position == m_gunslinger_lens_target) {
        m_scope_gyro_started = false;
        return;
    }

    if (m_gunslinger_lens_speed <= EPS_S) {
        m_gunslinger_lens_position = m_gunslinger_lens_target;
        return;
    }

    if (IsZoomed() && UsesScopeZoom() && m_scope_zoom_sound[2]) {
        const LPCSTR key = "lens_gyro_sound_period";
        const float period = READ_IF_EXISTS(pSettings, r_float, ScopeLensUpgradeSection(key), key, 0.f);
        const u32 now = Device.dwTimeGlobal;
        if (std::isfinite(period) && period > 0.f &&
            (!m_scope_gyro_started || now - m_scope_gyro_time >= u32(std::min(period, 60.f) * 1000.f))) {
            PlayScopeZoomSound(2);
            m_scope_gyro_time = now;
            m_scope_gyro_started = true;
        }
    }
    const float movement = m_gunslinger_lens_speed * std::max(0.f, Device.fTimeDelta);
    m_gunslinger_lens_position += clampr(m_gunslinger_lens_target - m_gunslinger_lens_position,
                                        -movement, movement);
}

void CWeapon::ChangeZoomStep(bool increase) {
    if (!UsesScopeZoom())
        return;

    float lens_minimum, lens_maximum;
    if (!UpgradeGrenadeMode() && GunslingerScopeLensRange(lens_minimum, lens_maximum) &&
        Is3DScopeEnabled()) {
        const int steps = READ_IF_EXISTS(pSettings, r_s32,
            ScopeLensUpgradeSection("lens_factor_levels_count"), "lens_factor_levels_count", 1);
        const bool force_sound = READ_IF_EXISTS(pSettings, r_bool,
            ScopeSettingSection("force_zoom_sound"), "force_zoom_sound", false);
        if (steps > 0 && (lens_maximum > lens_minimum || force_sound)) {
            const float step = 1.f / steps;
            const float target = clampr(m_gunslinger_lens_target + (increase ? step : -step), 0.f, 1.f);
            if (target != m_gunslinger_lens_target) {
                m_gunslinger_lens_target = target;
                PlayScopeZoomSound(increase ? 1 : 0);
                if (m_gunslinger_lens_speed <= EPS_S)
                    m_gunslinger_lens_position = target;
            }
        }
        return;
    }

    if (!m_zoom_params.m_bUseDynamicZoom)
        return;

    const float scope = m_zoom_params.m_fScopeZoomFactor;
    if (UsesGunslingerZoom()) {
        if (!std::isfinite(scope) || scope <= 1.f)
            return;

        const float minimum = MinScopeMagnification(scope);
        const float step = (scope - minimum) / 3.f;
        SetZoomFactor(clampr(GetZoomFactor() + (increase ? step : -step), minimum, scope));
        return;
    }

    float step, minimum;
    GetZoomData(scope, step, minimum);
    SetZoomFactor(clampr(GetZoomFactor() + (increase ? -step : step), scope, minimum));
}

void CWeapon::ZoomInc() { ChangeZoomStep(true); }

void CWeapon::ZoomDec() { ChangeZoomStep(false); }
u32 CWeapon::Cost() const {
    u32 res = CInventoryItem::Cost();
    if (IsGrenadeLauncherAttached() && GetGrenadeLauncherName().size()) {
        res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
    }
    if (IsScopeAttached() && m_scopes.size()) {
        res += pSettings->r_u32(GetScopeName(), "cost");
    }
    if (IsSilencerAttached() && GetSilencerName().size()) {
        res += pSettings->r_u32(GetSilencerName(), "cost");
    }

    if (iAmmoElapsed) {
        float w = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "cost");
        float bs = pSettings->r_float(m_ammoTypes[m_ammoType].c_str(), "box_size");

        res += iFloor(w * (iAmmoElapsed / bs));
    }
    return res;
}
