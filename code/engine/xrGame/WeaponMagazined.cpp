#include "pch_script.h"

#include <algorithm>
#include <cstring>

#include "WeaponMagazined.h"
#include "actor.h"
#include "ParticlesObject.h"
#include "scope.h"
#include "silencer.h"
#include "GrenadeLauncher.h"
#include "inventory.h"
#include "CustomDetector.h"
#include "player_hud.h"
#include "InventoryOwner.h"
#include "xrserver_objects_alife_items.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "xr_level_controller.h"
#include "UIGameCustom.h"
#include "object_broker.h"
#include "string_table.h"
#include "ui/UIXmlInit.h"
#include "ui/UIStatic.h"
#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"

CUIXml* pWpnScopeXml = NULL;

void createWpnScopeXML() {
    if (!pWpnScopeXml) {
        pWpnScopeXml = xr_new<CUIXml>();
        pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
    }
}

CWeaponMagazined::CWeaponMagazined(ESoundTypes eSoundType) : CWeapon() {
    m_eSoundShow = ESoundTypes(SOUND_TYPE_ITEM_TAKING | eSoundType);
    m_eSoundHide = ESoundTypes(SOUND_TYPE_ITEM_HIDING | eSoundType);
    m_eSoundShot = ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING | eSoundType);
    m_eSoundEmptyClick = ESoundTypes(SOUND_TYPE_WEAPON_EMPTY_CLICKING | eSoundType);
    m_eSoundReload = ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING | eSoundType);
    m_sounds_enabled = true;

    m_sSndShotCurrent = NULL;
    m_sSilencerFlameParticles = m_sSilencerSmokeParticles = NULL;

    m_bFireSingleShot = false;
    m_iShotNum = 0;
    m_fOldBulletSpeed = 0;
    m_iQueueSize = WEAPON_ININITE_QUEUE;
    m_bLockType = false;
    m_chamber_round = false;
    m_reload_target_capacity = 0;
}

CWeaponMagazined::~CWeaponMagazined() {
    // sounds
}

void CWeaponMagazined::net_Destroy() { inherited::net_Destroy(); }

void CWeaponMagazined::Load(LPCSTR section) {
    inherited::Load(section);
    m_upgrade_scope_zoom = 0.f;
    m_upgrade_ironsight_zoom = 0.f;
    m_chamber_round = pSettings->line_exist(section, "chamber_round")
                          ? pSettings->r_bool(section, "chamber_round")
                          : READ_IF_EXISTS(pSettings, r_bool, section, "ammo_in_chamber", false);
    CWeaponMagazined::OnUpgradeHudChanged();

    // Sounds
    if (pSettings->line_exist(section, "snd_sprint_start"))
        m_sounds.LoadSound(section, "snd_sprint_start", "sndSprintStart", false, m_eSoundShow);
    if (pSettings->line_exist(section, "snd_sprint_end"))
        m_sounds.LoadSound(section, "snd_sprint_end", "sndSprintEnd", false, m_eSoundHide);

    if (pSettings->line_exist(section, "snd_aim_start"))
        m_sounds.LoadSound(section, "snd_aim_start", "sndAimStart", false, m_eSoundShow);
    if (pSettings->line_exist(section, "snd_aim_end"))
        m_sounds.LoadSound(section, "snd_aim_end", "sndAimEnd", false, m_eSoundHide);

    m_sounds.LoadSound(section, "snd_draw", "sndShow", false, m_eSoundShow);
    m_sounds.LoadSound(section, "snd_holster", "sndHide", false, m_eSoundHide);
    m_sounds.LoadSound(section, "snd_shoot", "sndShot", false, m_eSoundShot);
    const auto load_action_sound = [&](LPCSTR key, LPCSTR alias) {
        const LPCSTR value = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
        if (!value || !value[0])
            return false;

        string256 path;
        _GetItem(value, 0, path);
        if (!path[0] || xr_strcmp(path, "$silent") == 0)
            return false;

        m_sounds.LoadSound(section, key, alias, true, m_eSoundReload);
        return true;
    };
    m_has_breechblock_sound = load_action_sound("snd_breechblock", "sndBreechblock");
    m_has_shot_jam_sound = load_action_sound("snd_jam", "sndShotJam");
    m_sounds.LoadSound(section, "snd_empty", "sndEmptyClick", false, m_eSoundEmptyClick);
    if (pSettings->line_exist(section, "snd_light_misfire"))
        m_sounds.LoadSound(section, "snd_light_misfire", "sndLightMisfire", false, m_eSoundEmptyClick);
    m_sounds.LoadSound(section, "snd_reload", "sndReload", true, m_eSoundReload);
    const LPCSTR reload_sounds[] = {"snd_reload_empty", "snd_reload_full", "snd_reload_misfire",
                                   "snd_changecartridgetype",
                                   "snd_reload_jammed", "snd_reload_jammed_last", "snd_unjam",
                                   "snd_reload_detector", "snd_reload_empty_detector",
                                   "snd_reload_jammed_detector", "snd_reload_jammed_last_detector"};
    for (LPCSTR key : reload_sounds)
        if (pSettings->line_exist(section, key))
            m_sounds.LoadSound(section, key, key, true, m_eSoundReload);

    m_sSndShotCurrent = "sndShot";

    //çâóêè è ïàðòèêëû ãëóøèòåëÿ, åñëèò òàêîé åñòü
    if (m_eSilencerStatus == ALife::eAddonAttachable ||
        m_eSilencerStatus == ALife::eAddonPermanent) {
        if (pSettings->line_exist(section, "silencer_flame_particles"))
            m_sSilencerFlameParticles = pSettings->r_string(section, "silencer_flame_particles");
        if (pSettings->line_exist(section, "silencer_smoke_particles"))
            m_sSilencerSmokeParticles = pSettings->r_string(section, "silencer_smoke_particles");

        m_sounds.LoadSound(section, "snd_silncer_shot", "sndSilencerShot", false, m_eSoundShot);
    }

    m_iBaseDispersionedBulletsCount =
        READ_IF_EXISTS(pSettings, r_u8, section, "base_dispersioned_bullets_count", 0);
    m_fBaseDispersionedBulletsSpeed = READ_IF_EXISTS(
        pSettings, r_float, section, "base_dispersioned_bullets_speed", m_fStartBulletSpeed);

    if (pSettings->line_exist(section, "fire_modes")) {
        m_bHasDifferentFireModes = true;
        shared_str FireModesList = pSettings->r_string(section, "fire_modes");
        int ModesCount = _GetItemCount(FireModesList.c_str());
        m_aFireModes.clear();

        for (int i = 0; i < ModesCount; i++) {
            string16 sItem;
            _GetItem(FireModesList.c_str(), i, sItem);
            m_aFireModes.push_back((s8)atoi(sItem));
        }

        m_iCurFireMode = ModesCount - 1;
        m_iPrefferedFireMode = READ_IF_EXISTS(pSettings, r_s16, section, "preffered_fire_mode", -1);
    } else {
        m_bHasDifferentFireModes = false;
    }
    LoadSilencerKoeffs();
}

void CWeaponMagazined::OnUpgradeHudChanged() {
    m_gunslinger_movement_idles =
        READ_IF_EXISTS(pSettings, r_bool, HudSection(), "gunslinger_movement_idles", false) ||
        READ_IF_EXISTS(pSettings, r_bool, HudSection(), "enable_directions_anm_idle_aim", false);
}

void CWeaponMagazined::FireStart() {
    if (!IsMisfire()) {
        if (IsValid()) {
            if (!IsWorking() || AllowFireWhileWorking()) {
                if (GetState() == eReload)
                    return;
                if (GetState() == eShowing)
                    return;
                if (GetState() == eHiding)
                    return;
                if (GetState() == eMisfire)
                    return;

                inherited::FireStart();

                if (iAmmoElapsed == 0)
                    OnMagazineEmpty();
                else {
                    R_ASSERT(H_Parent());
                    SwitchState(eFire);
                }
            }
        } else {
            if (eReload != GetState())
                OnMagazineEmpty();
        }
    } else { // misfire
        if (smart_cast<CActor*>(this->H_Parent()) && (Level().CurrentViewEntity() == H_Parent()))
            CurrentGameUI()->AddCustomStatic("gun_jammed", true);

        OnEmptyClick();
        PlayDryFireMotion("anm_fakeshoot_jammed", "anm_fakeshoot_aim_jammed");
    }
}

void CWeaponMagazined::FireEnd() {
    inherited::FireEnd();

    CActor* actor = smart_cast<CActor*>(H_Parent());
    if (m_pInventory && !iAmmoElapsed && !IsMisfire() && actor && GetState() != eReload)
        Reload();
}

void CWeaponMagazined::Reload() {
    inherited::Reload();
    TryReload();
}

bool CWeaponMagazined::TryReload() {
    if (m_pInventory) {
        if (IsGameTypeSingle() && ParentIsActor()) {
            int AC = GetSuitableAmmoTotal();
            Actor()->callback(GameObject::eWeaponNoAmmoAvailable)(lua_game_object(), AC);
        }

        if (IsMisfire()) {
            SetPending(TRUE);
            SwitchState(eReload);
            return true;
        }

        m_pCurrentAmmo =
            smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[m_ammoType].c_str()));

        if (m_pCurrentAmmo || unlimited_ammo()) {
            SetPending(TRUE);
            SwitchState(eReload);
            return true;
        } else
            for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i) {
                m_pCurrentAmmo =
                    smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[i].c_str()));
                if (m_pCurrentAmmo) {
                    m_ammoType = i;
                    SetPending(TRUE);
                    SwitchState(eReload);
                    return true;
                }
            }
    }

    if (GetState() != eIdle)
        SwitchState(eIdle);

    return false;
}

bool CWeaponMagazined::IsAmmoAvailable() {
    if (smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[m_ammoType].c_str())))
        return (true);
    else
        for (u32 i = 0; i < m_ammoTypes.size(); ++i)
            if (smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[i].c_str())))
                return (true);
    return (false);
}

void CWeaponMagazined::OnMagazineEmpty() {

    if (GetState() == eIdle) {
        OnEmptyClick();
        PlayDryFireMotion("anm_fakeshoot_empty", "anm_fakeshoot_aim_empty");
        return;
    }

    if (GetNextState() != eMagEmpty && GetNextState() != eReload) {
        SwitchState(eMagEmpty);
    }

    inherited::OnMagazineEmpty();
}

void CWeaponMagazined::UnloadMagazine(bool spawn_ammo) {
    xr_map<LPCSTR, u16> l_ammo;

    while (!m_magazine.empty()) {
        CCartridge& l_cartridge = m_magazine.back();
        xr_map<LPCSTR, u16>::iterator l_it;
        for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it) {
            if (!xr_strcmp(*l_cartridge.m_ammoSect, l_it->first)) {
                ++(l_it->second);
                break;
            }
        }

        if (l_it == l_ammo.end())
            l_ammo[*l_cartridge.m_ammoSect] = 1;
        m_magazine.pop_back();
        --iAmmoElapsed;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (!spawn_ammo)
        return;

    xr_map<LPCSTR, u16>::iterator l_it;
    for (l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it) {
        if (m_pInventory) {
            CWeaponAmmo* l_pA = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(l_it->first));
            if (l_pA) {
                u16 l_free = l_pA->m_boxSize - l_pA->m_boxCurr;
                l_pA->m_boxCurr = l_pA->m_boxCurr + (l_free < l_it->second ? l_free : l_it->second);
                l_it->second = l_it->second - (l_free < l_it->second ? l_free : l_it->second);
            }
        }
        if (l_it->second && !unlimited_ammo())
            SpawnAmmo(l_it->second, l_it->first);
    }
}

void CWeaponMagazined::ReloadMagazine() {
    m_BriefInfo_CalcFrame = 0;

    //óñòðàíèòü îñå÷êó ïðè ïåðåçàðÿäêå
    if (IsMisfire())
        bMisfire = false;

    if (!m_bLockType) {
        m_pCurrentAmmo = NULL;
    }

    if (!m_pInventory)
        return;

    // A full magazine with a chambered round needs only one more cartridge.
    // Keep the existing rounds even when the available ammo box has another type.
    const bool chamber_top_off = !m_bLockType && HasChamberRound() &&
        iAmmoElapsed == iMagazineSize &&
        m_set_next_ammoType_on_reload == undefined_ammo_type;

    if (m_set_next_ammoType_on_reload != undefined_ammo_type) {
        m_ammoType = m_set_next_ammoType_on_reload;
        m_set_next_ammoType_on_reload = undefined_ammo_type;
    }

    if (!unlimited_ammo()) {
        if (m_ammoTypes.size() <= m_ammoType)
            return;

        LPCSTR tmp_sect_name = m_ammoTypes[m_ammoType].c_str();

        if (!tmp_sect_name)
            return;

        //ïîïûòàòüñÿ íàéòè â èíâåíòàðå ïàòðîíû òåêóùåãî òèïà
        m_pCurrentAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(tmp_sect_name));

        if (!m_pCurrentAmmo && !m_bLockType) {
            for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i) {
                //ïðîâåðèòü ïàòðîíû âñåõ ïîäõîäÿùèõ òèïîâ
                m_pCurrentAmmo =
                    smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[i].c_str()));
                if (m_pCurrentAmmo) {
                    m_ammoType = i;
                    break;
                }
            }
        }
    }

    //íåò ïàòðîíîâ äëÿ ïåðåçàðÿäêè
    if (!m_pCurrentAmmo && !unlimited_ammo())
        return;

    //ðàçðÿäèòü ìàãàçèí, åñëè çàãðóæàåì ïàòðîíàìè äðóãîãî òèïà
    // Capture whether a round was chambered before an ammo-type change unloads
    // the old magazine. Recursive refills keep the same target capacity.
    if (!m_bLockType)
        m_reload_target_capacity = iMagazineSize + (HasChamberRound() && iAmmoElapsed > 0 ? 1 : 0);

    if (!m_bLockType && !chamber_top_off && !m_magazine.empty() &&
        (!m_pCurrentAmmo || xr_strcmp(m_pCurrentAmmo->cNameSect(), *m_magazine.back().m_ammoSect)))
        UnloadMagazine();

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
        m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
    CCartridge l_cartridge = m_DefaultCartridge;
    while (iAmmoElapsed < m_reload_target_capacity) {
        if (!unlimited_ammo()) {
            if (!m_pCurrentAmmo->Get(l_cartridge))
                break;
        }
        ++iAmmoElapsed;
        l_cartridge.m_LocalAmmoType = m_ammoType;
        m_magazine.push_back(l_cartridge);
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    //âûêèíóòü êîðîáêó ïàòðîíîâ, åñëè îíà ïóñòàÿ
    if (m_pCurrentAmmo && !m_pCurrentAmmo->m_boxCurr && OnServer())
        m_pCurrentAmmo->SetDropManual(TRUE);

    if (m_reload_target_capacity > iAmmoElapsed) {
        m_bLockType = true;
        ReloadMagazine();
        m_bLockType = false;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeaponMagazined::OnStateSwitch(u32 S) {
    inherited::OnStateSwitch(S);
    CInventoryOwner* owner = smart_cast<CInventoryOwner*>(this->H_Parent());
    switch (S) {
    case eIdle:
        switch2_Idle();
        break;
    case eFire:
        switch2_Fire();
        break;
    case eMisfire:
        if (smart_cast<CActor*>(this->H_Parent()) && (Level().CurrentViewEntity() == H_Parent()))
            CurrentGameUI()->AddCustomStatic("gun_jammed", true);
        break;
    case eMagEmpty:
        switch2_Empty();
        break;
    case eReload:
        if (owner)
            m_sounds_enabled = owner->CanPlayShHdRldSounds();
        switch2_Reload();
        break;
    case eShowing:
        if (owner)
            m_sounds_enabled = owner->CanPlayShHdRldSounds();
        switch2_Showing();
        break;
    case eHiding:
        if (owner)
            m_sounds_enabled = owner->CanPlayShHdRldSounds();
        switch2_Hiding();
        break;
    case eHidden:
        switch2_Hidden();
        break;
    }
}

void CWeaponMagazined::UpdateCL() {
    inherited::UpdateCL();
    float dt = Device.fTimeDelta;

    //êîãäà ïðîèñõîäèò àïäåéò ñîñòîÿíèÿ îðóæèÿ
    //íè÷åãî äðóãîãî íå äåëàòü
    if (GetNextState() == GetState()) {
        switch (GetState()) {
        case eShowing:
        case eHiding:
        case eReload:
        case eIdle: {
            fShotTimeCounter -= dt;
            clamp(fShotTimeCounter, 0.0f, flt_max);
        } break;
        case eFire: {
            state_Fire(dt);
        } break;
        case eMisfire:
            state_Misfire(dt);
            break;
        case eMagEmpty:
            state_MagEmpty(dt);
            break;
        case eHidden:
            break;
        }
    }

    if (m_gunslinger_movement_idles)
        UpdateGunslingerMovementIdle();
    UpdateSounds();
}

void CWeaponMagazined::UpdateSounds() {
    if (Device.dwFrame == dwUpdateSounds_Frame)
        return;

    dwUpdateSounds_Frame = Device.dwFrame;

    Fvector P = get_LastFP();
    m_sounds.SetPosition("sndShow", P);
    m_sounds.SetPosition("sndHide", P);
    //. nah	m_sounds.SetPosition("sndShot", P);
    m_sounds.SetPosition("sndReload", P);
    if (m_has_breechblock_sound)
        m_sounds.SetPosition("sndBreechblock", P);
    if (m_has_shot_jam_sound)
        m_sounds.SetPosition("sndShotJam", P);
    //. nah	m_sounds.SetPosition("sndEmptyClick", P);
}

bool CWeaponMagazined::RollForLightMisfire() const {
    const auto section = cNameSect();
    if (!READ_IF_EXISTS(pSettings, r_bool, section, "use_light_misfire", false))
        return false;
    if (READ_IF_EXISTS(pSettings, r_bool, HudSection(), "disable_light_misfires_with_detector", false) &&
        ReloadDetector())
        return false;

    const float start = READ_IF_EXISTS(pSettings, r_float, section, "light_misfire_start_condition", 1.f);
    const float end = READ_IF_EXISTS(pSettings, r_float, section, "light_misfire_end_condition", 0.f);
    const float condition = GetCondition();
    if (condition > start)
        return false;

    const float start_probability =
        READ_IF_EXISTS(pSettings, r_float, section, "light_misfire_start_probability", 0.f);
    const float end_probability =
        READ_IF_EXISTS(pSettings, r_float, section, "light_misfire_end_probability", 0.f);
    const float wear = end < start ? clampr((start - condition) / (start - end), 0.f, 1.f) : 1.f;
    const float probability =
        clampr(start_probability + wear * (end_probability - start_probability), 0.f, 1.f);
    return ::Random.randF(0.f, 1.f) < probability;
}

void CWeaponMagazined::PlayLightMisfire() {
    m_bFireSingleShot = false;
    CWeapon::FireEnd();
    SetPending(TRUE);

    LPCSTR motion = OptionalHudMotion("anm_shoot_lightmisfire", nullptr);
    if (IsZoomed() && !IsRotatingToZoom())
        motion = OptionalHudMotion("anm_shoot_lightmisfire_aim", motion);

    const bool has_misfire_sound = pSettings->line_exist(cNameSect(), "snd_light_misfire");
    PlaySound(has_misfire_sound ? "sndLightMisfire" : "sndEmptyClick", get_LastFP());

    float delay = fOneShotTime;
    if (motion && GetHUDmode()) {
        const u32 duration = PlayHUDMotion(motion, TRUE, this, eFire);
        delay = (std::max)(delay, float(duration) * 0.001f);

        string128 lock_key;
        xr_sprintf(lock_key, "lock_time_%s", motion);
        delay = (std::max)(delay, READ_IF_EXISTS(pSettings, r_float, HudSection(), lock_key, 0.f));
    }
    fShotTimeCounter = delay;
}

void CWeaponMagazined::state_Fire(float dt) {
    if (iAmmoElapsed > 0 && !IsMisfire() && !IsPending()) {
        VERIFY(fOneShotTime > 0.f);

        Fvector p1, d;
        p1.set(get_LastFP());
        d.set(get_LastFD());

        if (!H_Parent())
            return;

        CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
        if (NULL == io->inventory().ActiveItem()) {
            Log("current_state", GetState());
            Log("next_state", GetNextState());
            Log("item_sect", cNameSect().c_str());
            Log("H_Parent", H_Parent()->cNameSect().c_str());
        }

        CEntity* E = smart_cast<CEntity*>(H_Parent());
        E->g_fireParams(this, p1, d);

        const bool jam_prevents_shot = JamPreventsShot();
        const bool gunslinger_jam = IsGameTypeSingle() && ParentIsActor() &&
            (jam_prevents_shot || READ_IF_EXISTS(pSettings, r_bool, HudSection(), "gunslinger_jam_after_shot", false));
        if (!E->g_stateFire() && !(gunslinger_jam && m_bFireSingleShot))
            StopShooting();

        if (m_iShotNum == 0) {
            m_vStartPos = p1;
            m_vStartDir = d;
        };

        VERIFY(!m_magazine.empty());

        while (!m_magazine.empty() && fShotTimeCounter < 0 && (IsWorking() || m_bFireSingleShot) &&
               (m_iQueueSize < 0 || m_iShotNum < m_iQueueSize)) {
            const bool jam_this_shot = gunslinger_jam && RollForMisfire();
            if (jam_this_shot && RollForLightMisfire()) {
                PlayLightMisfire();
                return;
            }
            if (jam_this_shot && jam_prevents_shot) {
                bMisfire = true;
                m_bFireSingleShot = false;
                CWeapon::FireEnd();
                SetPending(TRUE);
                PlaySound(m_has_shot_jam_sound ? "sndShotJam" : "sndEmptyClick", get_LastFP());
                PlayAnimShoot();
                fShotTimeCounter = fOneShotTime;
                if (m_bStopAtEndAnimIsRunning && m_startedMotionState == eFire) {
                    const float duration = float(m_dwMotionEndTm - m_dwMotionStartTm) * 0.001f;
                    fShotTimeCounter = (std::max)(fShotTimeCounter, duration);
                }
                return;
            }
            if (!gunslinger_jam && CheckForMisfire()) {
                StopShooting();
                return;
            }

            m_bFireSingleShot = false;

            fShotTimeCounter += fOneShotTime;

            ++m_iShotNum;

            // The jammed shot clip needs the jam flag before FireTrace consumes the round.
            if (jam_this_shot)
                bMisfire = true;
            OnShot();

            if (m_iShotNum > m_iBaseDispersionedBulletsCount)
                FireTrace(p1, d);
            else
                FireTrace(m_vStartPos, m_vStartDir);

            if (jam_this_shot)
                break;
        }

        if (m_iShotNum == m_iQueueSize)
            m_bStopedAfterQueueFired = true;

        UpdateSounds();
    }

    if (fShotTimeCounter < 0) {
        /*
                        if(bDebug && H_Parent() && (H_Parent()->ID() != Actor()->ID()))
                        {
                                Msg("stop shooting w=[%s] magsize=[%d] sshot=[%s] qsize=[%d]
           shotnum=[%d]", IsWorking()?"true":"false", m_magazine.size(),
                                                m_bFireSingleShot?"true":"false",
                                                m_iQueueSize,
                                                m_iShotNum);
                        }
        */
        if (iAmmoElapsed == 0 && !IsMisfire())
            OnMagazineEmpty();

        StopShooting();
    } else {
        fShotTimeCounter -= dt;
    }
}

void CWeaponMagazined::state_Misfire(float dt) {
    OnEmptyClick();
    SwitchState(eIdle);

    bMisfire = true;

    UpdateSounds();
}

void CWeaponMagazined::state_MagEmpty(float dt) {}

void CWeaponMagazined::SetDefaults() { CWeapon::SetDefaults(); }

void CWeaponMagazined::OnShot() {
    // Sound
    PlaySound(ShotSound(), get_LastFP());

    // Camera
    AddShotEffector();

    // Animation
    PlayAnimShoot();
    if (IsMisfire()) {
        if (m_has_shot_jam_sound)
            PlaySound("sndShotJam", get_LastFP());
    } else if (m_has_breechblock_sound) {
        PlaySound("sndBreechblock", get_LastFP());
    }

    // Shell Drop
    Fvector vel;
    PHGetLinearVell(vel);
    OnShellDrop(get_LastSP(), vel);

    // Îãîíü èç ñòâîëà
    StartFlameParticles();

    //äûì èç ñòâîëà
    ForceUpdateFireParticles();
    StartSmokeParticles(get_LastFP(), vel);
}

void CWeaponMagazined::OnEmptyClick() { PlaySound("sndEmptyClick", get_LastFP()); }

void CWeaponMagazined::PlayDryFireMotion(LPCSTR normal, LPCSTR aimed) {
    if (GetState() != eIdle || GetNextState() != eIdle || IsPending() || !GetHUDmode())
        return;

    const shared_str motion = SelectDryFireMotion(normal, aimed);
    // Stop-at-end clips return to idle through OnAnimationEnd(eIdle).
    if (motion.c_str() && !PlayHUDMotion(motion, TRUE, this, eIdle))
        PlayAnimIdle();
}

shared_str CWeaponMagazined::SelectDryFireMotion(LPCSTR normal, LPCSTR aimed) const {
    LPCSTR motion = OptionalHudMotion(normal, nullptr);
    if (IsZoomed() && !IsRotatingToZoom())
        motion = OptionalHudMotion(aimed, motion);
    return shared_str(motion);
}

void CWeaponMagazined::OnAnimationEnd(u32 state) {
    switch (state) {
    case eReload:
        if (IsMisfire()) {
            // Clearing a jam does not touch the magazine or reserve ammo.
            bMisfire = false;
            m_set_next_ammoType_on_reload = undefined_ammo_type;
        } else {
            ReloadMagazine();
        }
        SwitchState(eIdle);
        break; // End of reload animation
    case eHiding:
        SwitchState(eHidden);
        break; // End of Hide
    case eShowing:
        SwitchState(eIdle);
        break; // End of Show
    case eIdle:
        if (GetState() == eIdle && GetNextState() == eIdle)
            switch2_Idle();
        break; // Keep showing idle
    }
    inherited::OnAnimationEnd(state);
}

void CWeaponMagazined::switch2_Idle() {
    m_iShotNum = 0;
    if (m_fOldBulletSpeed != 0.f)
        SetBulletSpeed(m_fOldBulletSpeed);

    SetPending(FALSE);
    PlayAnimIdle();
}

#ifdef DEBUG
#include "ai\stalker\ai_stalker.h"
#include "object_handler_planner.h"
#endif
void CWeaponMagazined::switch2_Fire() {
    CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());
    CInventoryItem* ii = smart_cast<CInventoryItem*>(this);
#ifdef DEBUG
    if (!io)
        return;
    // VERIFY2					(io,make_string("no inventory owner, item
    // %s",*cName()));

    if (ii != io->inventory().ActiveItem())
        Msg("! not an active item, item %s, owner %s, active item %s", *cName(),
            *H_Parent()->cName(),
            io->inventory().ActiveItem() ? *io->inventory().ActiveItem()->object().cName()
                                         : "no_active_item");

    if (!(io && (ii == io->inventory().ActiveItem()))) {
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(H_Parent());
        if (stalker) {
            stalker->planner().show();
            stalker->planner().show_current_world_state();
            stalker->planner().show_target_world_state();
        }
    }
#else
    if (!io)
        return;
#endif // DEBUG

    //
    //	VERIFY2(
    //		io && (ii == io->inventory().ActiveItem()),
    //		make_string(
    //			"item[%s], parent[%s]",
    //			*cName(),
    //			H_Parent() ? *H_Parent()->cName() : "no_parent"
    //		)
    //	);

    m_bStopedAfterQueueFired = false;
    m_bFireSingleShot = true;
    m_iShotNum = 0;

    if ((OnClient() || Level().IsDemoPlay()) && !IsWorking())
        FireStart();
}

void CWeaponMagazined::switch2_Empty() {
    OnZoomOut();

    if (!TryReload()) {
        OnEmptyClick();
    } else {
        inherited::FireEnd();
    }
}
void CWeaponMagazined::PlayReloadSound() {
    if (m_sounds_enabled)
        PlaySound(ReloadSound(), get_LastFP());
}

bool CWeaponMagazined::IsLastRoundJam() const {
    if (iAmmoElapsed == 0)
        return true;

    // With post-shot jams the fired round has already been removed.
    // A remaining round needs the loaded unjam pose.
    return iAmmoElapsed == 1 &&
           !READ_IF_EXISTS(pSettings, r_bool, HudSection(), "gunslinger_jam_after_shot", false) &&
           READ_IF_EXISTS(pSettings, r_bool, HudSection(), "gunslinger_last_round_jam", false);
}

bool CWeaponMagazined::IsReloadAmmoChange() const {
    return !IsMisfire() && m_set_next_ammoType_on_reload < m_ammoTypes.size();
}

CCustomDetector* CWeaponMagazined::ReloadDetector() const {
    if (!g_player_hud || IsGrenadeLauncherAttached())
        return nullptr;

    const auto* weapon_hud = g_player_hud->attached_item(0);
    const auto* detector_hud = g_player_hud->attached_item(1);
    if (!weapon_hud || weapon_hud->m_parent_hud_item != this || !detector_hud)
        return nullptr;

    return smart_cast<CCustomDetector*>(detector_hud->m_parent_hud_item);
}

LPCSTR CWeaponMagazined::DetectorReloadMotion(CCustomDetector& detector) const {
    if (IsReloadAmmoChange()) {
        LPCSTR motion = iAmmoElapsed == 0 ? "anm_reload_empty_ammochange_detector"
                                         : "anm_reload_ammochange_detector";
        if (pSettings->line_exist(HudSection(), motion) && detector.CanPlayWeaponReload(motion))
            return motion;
    }

    LPCSTR motion = IsMisfire() ? (IsLastRoundJam() ? "anm_reload_jammed_last_detector"
                                                 : "anm_reload_jammed_detector")
                               : (iAmmoElapsed == 0 ? "anm_reload_empty_detector"
                                                    : "anm_reload_detector");
    return pSettings->line_exist(HudSection(), motion) && detector.CanPlayWeaponReload(motion)
               ? motion : nullptr;
}

LPCSTR CWeaponMagazined::ReloadSound() const {
    const shared_str weapon_section = cNameSect();
    LPCSTR section = weapon_section.c_str();
    if (iAmmoElapsed > 0 && IsReloadAmmoChange() &&
        pSettings->line_exist(section, "snd_changecartridgetype"))
        return "snd_changecartridgetype";

    if (auto* detector = ReloadDetector()) {
        if (DetectorReloadMotion(*detector)) {
            LPCSTR sound = IsMisfire()
                               ? (IsLastRoundJam() ? "snd_reload_jammed_last_detector"
                                                   : "snd_reload_jammed_detector")
                               : (iAmmoElapsed == 0 ? "snd_reload_empty_detector"
                                                    : "snd_reload_detector");
            const bool use_detector_sound = !IsMisfire() ||
                READ_IF_EXISTS(pSettings, r_bool, HudSection(),
                               IsLastRoundJam() ? "use_jammed_last_detector_snd" : "use_jammed_detector_snd",
                               true);
            if (use_detector_sound && pSettings->line_exist(section, sound))
                return sound;
        }
    }
    if (IsMisfire()) {
        if (IsLastRoundJam() && pSettings->line_exist(section, "snd_reload_jammed_last"))
            return "snd_reload_jammed_last";
        const LPCSTR keys[] = {"snd_unjam", "snd_reload_misfire", "snd_reload_jammed"};
        for (LPCSTR key : keys)
            if (pSettings->line_exist(section, key)) return key;
    } else if (!iAmmoElapsed) {
        const LPCSTR keys[] = {"snd_reload_full", "snd_reload_empty"};
        for (LPCSTR key : keys)
            if (pSettings->line_exist(section, key)) return key;
    }
    return "sndReload";
}

LPCSTR CWeaponMagazined::OptionalHudMotion(LPCSTR preferred, LPCSTR fallback) const {
    return pSettings->line_exist(HudSection().c_str(), preferred) ? preferred : fallback;
}

LPCSTR CWeaponMagazined::ReloadMotion(bool launcher) const {
    LPCSTR section = HudSection().c_str();
    if (IsReloadAmmoChange()) {
        LPCSTR motion = iAmmoElapsed == 0 ? "anm_reload_empty_ammochange" : "anm_reload_ammochange";
        if (launcher) {
            LPCSTR launcher_motion = iAmmoElapsed == 0 ? "anm_reload_empty_ammochange_w_gl"
                                                       : "anm_reload_ammochange_w_gl";
            if (pSettings->line_exist(section, launcher_motion))
                return launcher_motion;
        } else if (pSettings->line_exist(section, motion)) {
            return motion;
        }
    }
    if (IsMisfire()) {
        if (IsLastRoundJam() && launcher &&
            pSettings->line_exist(section, "anm_reload_jammed_last_w_gl"))
            return "anm_reload_jammed_last_w_gl";
        if (IsLastRoundJam() && pSettings->line_exist(section, "anm_reload_jammed_last"))
            return "anm_reload_jammed_last";
        if (launcher) {
            const LPCSTR keys[] = {"anm_unjam_w_gl", "anm_reload_misfire_w_gl",
                                  "anm_reload_jammed_w_gl"};
            for (LPCSTR key : keys)
                if (pSettings->line_exist(section, key)) return key;
        }
        const LPCSTR keys[] = {"anm_unjam", "anm_reload_misfire", "anm_reload_jammed"};
        for (LPCSTR key : keys)
            if (pSettings->line_exist(section, key)) return key;
    } else if (!iAmmoElapsed) {
        if (launcher) {
            const LPCSTR keys[] = {"anm_reload_full_w_gl", "anm_reload_empty_w_gl"};
            for (LPCSTR key : keys)
                if (pSettings->line_exist(section, key)) return key;
        }
        const LPCSTR keys[] = {"anm_reload_full", "anm_reload_empty"};
        for (LPCSTR key : keys)
            if (pSettings->line_exist(section, key)) return key;
    }
    if (launcher && pSettings->line_exist(section, "anm_reload_w_gl"))
        return "anm_reload_w_gl";
    return "anm_reload";
}

void CWeaponMagazined::switch2_Reload() {
    CWeapon::FireEnd();

    PlayReloadSound();
    PlayAnimReload();
    SetPending(TRUE);
}
void CWeaponMagazined::switch2_Hiding() {
    OnZoomOut();
    CWeapon::FireEnd();

    if (m_sounds_enabled)
        PlaySound("sndHide", get_LastFP());

    PlayAnimHide();
    SetPending(TRUE);
}

void CWeaponMagazined::switch2_Hidden() {
    CWeapon::FireEnd();

    StopCurrentAnimWithoutCallback();

    signal_HideComplete();
    RemoveShotEffector();
}
void CWeaponMagazined::switch2_Showing() {
    if (m_sounds_enabled)
        PlaySound("sndShow", get_LastFP());

    SetPending(TRUE);
    PlayAnimShow();
}

bool CWeaponMagazined::Action(u16 cmd, u32 flags) {
    if (cmd == kWPN_ZOOM) {
        const bool was_zoomed = IsZoomed();
        const bool handled = inherited::Action(cmd, flags);
        if (handled && was_zoomed != IsZoomed())
            PlayAimTransition(IsZoomed());
        return handled;
    }

    if (inherited::Action(cmd, flags))
        return true;

    //åñëè îðóæèå ÷åì-òî çàíÿòî, òî íè÷åãî íå äåëàòü
    if (IsPending())
        return false;

    switch (cmd) {
    case kWPN_RELOAD: {
        if (flags & CMD_START)
            if (iAmmoElapsed < GetAmmoMagSize() || IsMisfire())
                Reload();
    }
        return true;
    case kWPN_FIREMODE_PREV: {
        if (flags & CMD_START) {
            OnPrevFireMode();
            return true;
        };
    } break;
    case kWPN_FIREMODE_NEXT: {
        if (flags & CMD_START) {
            OnNextFireMode();
            return true;
        };
    } break;
    }
    return false;
}

bool CWeaponMagazined::CanAttach(PIItem pIItem) {
    CScope* pScope = smart_cast<CScope*>(pIItem);
    CSilencer* pSilencer = smart_cast<CSilencer*>(pIItem);
    CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);

    if (pScope && m_eScopeStatus == ALife::eAddonAttachable &&
        (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 /*&&
				(m_scopes[cur_scope]->m_sScopeName == pIItem->object().cNameSect())*/) {
        auto it = m_scopes.begin();
        for (; it != m_scopes.end(); it++) {
            if (pSettings->r_string((*it), "scope_name") == pIItem->object().cNameSect())
                return true;
        }
        return false;
    } else if (pSilencer && m_eSilencerStatus == ALife::eAddonAttachable &&
               (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
               (m_sSilencerName == pIItem->object().cNameSect()))
        return true;
    else if (pGrenadeLauncher && m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
             (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
             (m_sGrenadeLauncherName == pIItem->object().cNameSect()))
        return true;
    else
        return inherited::CanAttach(pIItem);
}

bool CWeaponMagazined::CanDetach(const char* item_section_name) {
    if (m_eScopeStatus == ALife::eAddonAttachable &&
        0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)) /* &&
           (m_scopes[cur_scope]->m_sScopeName	== item_section_name))*/
    {
        auto it = m_scopes.begin();
        for (; it != m_scopes.end(); it++) {
            if (pSettings->r_string((*it), "scope_name") == item_section_name)
                return true;
        }
        return false;
    }
    //	   return true;
    else if (m_eSilencerStatus == ALife::eAddonAttachable &&
             0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) &&
             (m_sSilencerName == item_section_name))
        return true;
    else if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
             0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
             (m_sGrenadeLauncherName == item_section_name))
        return true;
    else
        return inherited::CanDetach(item_section_name);
}

bool CWeaponMagazined::Attach(PIItem pIItem, bool b_send_event) {
    bool result = false;

    CScope* pScope = smart_cast<CScope*>(pIItem);
    CSilencer* pSilencer = smart_cast<CSilencer*>(pIItem);
    CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);

    if (pScope && m_eScopeStatus == ALife::eAddonAttachable &&
        (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 /*&&
	   (m_scopes[cur_scope]->m_sScopeName == pIItem->object().cNameSect())*/) {
        auto it = m_scopes.begin();
        for (; it != m_scopes.end(); it++) {
            if (pSettings->r_string((*it), "scope_name") == pIItem->object().cNameSect())
                m_cur_scope = u8(it - m_scopes.begin());
        }
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonScope;
        result = true;
    } else if (pSilencer && m_eSilencerStatus == ALife::eAddonAttachable &&
               (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
               (m_sSilencerName == pIItem->object().cNameSect())) {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSilencer;
        result = true;
    } else if (pGrenadeLauncher && m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
               (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
               (m_sGrenadeLauncherName == pIItem->object().cNameSect())) {
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;
        result = true;
    }

    if (result) {
        if (b_send_event && OnServer()) {
            //óíè÷òîæèòü ïîäñîåäèíåííóþ âåùü èç èíâåíòàðÿ
            //.			pIItem->Drop					();
            pIItem->object().DestroyObject();
        };

        UpdateAddonsVisibility();
        InitAddons();

        return true;
    } else
        return inherited::Attach(pIItem, b_send_event);
}

bool CWeaponMagazined::DetachScope(const char* item_section_name, bool b_spawn_item) {
    bool detached = false;
    auto it = m_scopes.begin();
    for (; it != m_scopes.end(); it++) {
        LPCSTR iter_scope_name = pSettings->r_string((*it), "scope_name");
        if (!xr_strcmp(iter_scope_name, item_section_name)) {
            m_cur_scope = NULL;
            detached = true;
        }
    }
    return detached;
}

bool CWeaponMagazined::Detach(const char* item_section_name, bool b_spawn_item) {
    if (m_eScopeStatus == ALife::eAddonAttachable && DetachScope(item_section_name, b_spawn_item)) {
        if ((m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) == 0) {
            Msg("ERROR: scope addon already detached.");
            return true;
        }
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonScope;

        UpdateAddonsVisibility();
        InitAddons();

        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    } else if (m_eSilencerStatus == ALife::eAddonAttachable &&
               (m_sSilencerName == item_section_name)) {
        if ((m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0) {
            Msg("ERROR: silencer addon already detached.");
            return true;
        }
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonSilencer;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    } else if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
               (m_sGrenadeLauncherName == item_section_name)) {
        if ((m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0) {
            Msg("ERROR: grenade launcher addon already detached.");
            return true;
        }
        m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;

        UpdateAddonsVisibility();
        InitAddons();
        return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
    } else
        return inherited::Detach(item_section_name, b_spawn_item);
    ;
}
/*
void CWeaponMagazined::LoadAddons()
{
        m_zoom_params.m_fIronSightZoomFactor = READ_IF_EXISTS( pSettings, r_float, cNameSect(),
"ironsight_zoom_factor", 50.0f );

}
*/
void CWeaponMagazined::InitAddons() {
    const float scope_fallback = m_gunslinger_zoom ? 1.f : 50.f;
    m_zoom_params.m_fScopeZoomFactor = READ_IF_EXISTS(pSettings, r_float, cNameSect(),
        "scope_zoom_factor", scope_fallback);
    m_zoom_params.m_fIronSightZoomFactor = READ_IF_EXISTS(pSettings, r_float, cNameSect(),
        "ironsight_zoom_factor", m_gunslinger_zoom ? m_zoom_params.m_fScopeZoomFactor : 50.f);
    m_zoom_params.m_bUseDynamicZoom = READ_IF_EXISTS(pSettings, r_bool, cNameSect(),
        "scope_dynamic_zoom", FALSE);
    m_zoom_params.m_sUseZoomPostprocess = 0;
    m_zoom_params.m_sUseBinocularVision = 0;
    if (IsScopeAttached()) {
        shared_str scope_tex_name;
        if (m_eScopeStatus == ALife::eAddonAttachable) {
            // m_scopes[cur_scope]->m_sScopeName = pSettings->r_string(cNameSect(), "scope_name");
            // m_scopes[cur_scope]->m_iScopeX	 = pSettings->r_s32(cNameSect(),"scope_x");
            // m_scopes[cur_scope]->m_iScopeY	 = pSettings->r_s32(cNameSect(),"scope_y");

            scope_tex_name = READ_IF_EXISTS(pSettings, r_string,
                ScopeSettingSection("scope_texture"), "scope_texture", "");
            m_zoom_params.m_fScopeZoomFactor = READ_IF_EXISTS(pSettings, r_float,
                ScopeSettingSection("scope_zoom_factor"), "scope_zoom_factor",
                m_zoom_params.m_fScopeZoomFactor);
            m_zoom_params.m_sUseZoomPostprocess = READ_IF_EXISTS(pSettings, r_string,
                ScopeSettingSection("scope_nightvision"), "scope_nightvision", 0);
            m_zoom_params.m_bUseDynamicZoom = READ_IF_EXISTS(pSettings, r_bool,
                ScopeSettingSection("scope_dynamic_zoom"), "scope_dynamic_zoom", FALSE);
            m_zoom_params.m_sUseBinocularVision = READ_IF_EXISTS(pSettings, r_string,
                ScopeSettingSection("scope_alive_detector"), "scope_alive_detector", 0);
            m_fRTZoomFactor = m_zoom_params.m_fScopeZoomFactor;
            if (m_UIScope) {
                xr_delete(m_UIScope);
            }

            if (UseScopeTexture() && scope_tex_name.size()) {
                m_UIScope = xr_new<CUIWindow>();
                createWpnScopeXML();
                CUIXmlInit::InitWindow(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
            }
        }
    } else {
        if (m_UIScope) {
            xr_delete(m_UIScope);
        }

        if (IsZoomEnabled() && !m_gunslinger_zoom)
            m_zoom_params.m_fIronSightZoomFactor = m_zoom_params.m_fScopeZoomFactor;
    }

    if (IsScopeAttached()) {
        m_zoom_params.m_fScopeZoomFactor += m_upgrade_scope_zoom;
        if (m_upgrade_scope_zoom != 0.f)
            m_fRTZoomFactor = m_zoom_params.m_fScopeZoomFactor;
    } else if (IsZoomEnabled() && !m_gunslinger_zoom) {
        m_zoom_params.m_fIronSightZoomFactor += m_upgrade_scope_zoom;
    }
    m_zoom_params.m_fIronSightZoomFactor += m_upgrade_ironsight_zoom;

    if (IsSilencerAttached() /* && SilencerAttachable() */) {
        m_sFlameParticlesCurrent = m_sSilencerFlameParticles;
        m_sSmokeParticlesCurrent = m_sSilencerSmokeParticles;
        m_sSndShotCurrent = "sndSilencerShot";

        //ïîäñâåòêà îò âûñòðåëà
        LoadLights(*cNameSect(), "silencer_");
        ApplySilencerKoeffs();
    } else {
        m_sFlameParticlesCurrent = m_sFlameParticles;
        m_sSmokeParticlesCurrent = m_sSmokeParticles;
        m_sSndShotCurrent = "sndShot";

        //ïîäñâåòêà îò âûñòðåëà
        LoadLights(*cNameSect(), "");
        ResetSilencerKoeffs();
    }

    inherited::InitAddons();
}

void CWeaponMagazined::LoadSilencerKoeffs() {
    if (m_eSilencerStatus == ALife::eAddonAttachable) {
        LPCSTR sect = m_sSilencerName.c_str();
        m_silencer_koef.hit_power =
            READ_IF_EXISTS(pSettings, r_float, sect, "bullet_hit_power_k", 1.0f);
        m_silencer_koef.hit_impulse =
            READ_IF_EXISTS(pSettings, r_float, sect, "bullet_hit_impulse_k", 1.0f);
        m_silencer_koef.bullet_speed =
            READ_IF_EXISTS(pSettings, r_float, sect, "bullet_speed_k", 1.0f);
        m_silencer_koef.fire_dispersion =
            READ_IF_EXISTS(pSettings, r_float, sect, "fire_dispersion_base_k", 1.0f);
        m_silencer_koef.cam_dispersion =
            READ_IF_EXISTS(pSettings, r_float, sect, "cam_dispersion_k", 1.0f);
        m_silencer_koef.cam_disper_inc =
            READ_IF_EXISTS(pSettings, r_float, sect, "cam_dispersion_inc_k", 1.0f);
    }

    clamp(m_silencer_koef.hit_power, 0.0f, 1.0f);
    clamp(m_silencer_koef.hit_impulse, 0.0f, 1.0f);
    clamp(m_silencer_koef.bullet_speed, 0.0f, 1.0f);
    clamp(m_silencer_koef.fire_dispersion, 0.0f, 3.0f);
    clamp(m_silencer_koef.cam_dispersion, 0.0f, 1.0f);
    clamp(m_silencer_koef.cam_disper_inc, 0.0f, 1.0f);
}

void CWeaponMagazined::ApplySilencerKoeffs() { cur_silencer_koef = m_silencer_koef; }

void CWeaponMagazined::ResetSilencerKoeffs() { cur_silencer_koef.Reset(); }

void CWeaponMagazined::PlayAnimShow() {
    VERIFY(GetState() == eShowing);
    PlayHUDMotion(StateMotion("anm_show", "anm_show_empty", "anm_show_jammed"),
                  FALSE, this, GetState());
}

void CWeaponMagazined::PlayAnimHide() {
    VERIFY(GetState() == eHiding);
    PlayHUDMotion(StateMotion("anm_hide", "anm_hide_empty", "anm_hide_jammed"),
                  TRUE, this, GetState());
}

void CWeaponMagazined::PlayAnimReload() {
    VERIFY(GetState() == eReload);
    if (auto* detector = ReloadDetector()) {
        if (LPCSTR motion = DetectorReloadMotion(*detector)) {
            detector->PlayWeaponReload(motion);
            PlayHUDMotion(motion, TRUE, this, GetState());
            return;
        }
    }
    PlayHUDMotion(ReloadMotion(), TRUE, this, GetState());
}

LPCSTR CWeaponMagazined::StateMotion(LPCSTR normal, LPCSTR empty, LPCSTR jammed) const {
    LPCSTR fallback = iAmmoElapsed == 0 ? OptionalHudMotion(empty, normal) : normal;
    return IsMisfire() ? OptionalHudMotion(jammed, fallback) : fallback;
}

void CWeaponMagazined::PlayAnimAim() {
    PlayHUDMotion(StateMotion("anm_idle_aim", "anm_idle_aim_empty", "anm_idle_aim_jammed"),
                  TRUE, NULL, GetState());
}

void CWeaponMagazined::PlayAnimIdleMoving() {
    PlayHUDMotion(StateMotion("anm_idle_moving", "anm_idle_moving_empty", "anm_idle_moving_jammed"),
                  TRUE, NULL, GetState());
}

void CWeaponMagazined::PlayAnimIdleSprint() {
    PlayHUDMotion(StateMotion("anm_idle_sprint", "anm_idle_sprint_empty", "anm_idle_sprint_jammed"),
                  TRUE, NULL, GetState());
}

void CWeaponMagazined::PlayAnimBore() {
    PlayHUDMotion(StateMotion("anm_bore", "anm_bore_empty", "anm_bore_jammed"),
                  TRUE, this, GetState());
}

shared_str CWeaponMagazined::HudStateVariant(LPCSTR, LPCSTR fallback) const {
    return shared_str(fallback);
}

void CWeaponMagazined::PlayAimTransition(bool entering) {
    if (GetState() != eIdle || GetNextState() != eIdle || IsPending() || !GetHUDmode())
        return;

    const LPCSTR base = entering ? "anm_idle_aim_start" : "anm_idle_aim_end";
    const LPCSTR fallback = entering
        ? StateMotion(base, "anm_idle_aim_start_empty", "anm_idle_aim_start_jammed")
        : StateMotion(base, "anm_idle_aim_end_empty", "anm_idle_aim_end_jammed");
    const shared_str motion = HudStateVariant(base, IsGrenadeLauncherAttached() ? nullptr : fallback);
    if (!motion.c_str() || !pSettings->line_exist(HudSection(), motion.c_str()))
        return;

    if (!PlayHUDMotion(motion, TRUE, this, eIdle)) {
        PlayAnimIdle();
        return;
    }

    if (pSettings->line_exist(cNameSect(), entering ? "snd_aim_start" : "snd_aim_end"))
        PlaySound(entering ? "sndAimStart" : "sndAimEnd", get_LastFP());
}

shared_str CWeaponMagazined::GunslingerMovementIdle(u32 movement) const {
    if (movement & (mcSprint | mcClimb | mcJump | mcFall))
        return {};

    string128 base;
    if (IsZoomed()) {
        if (!(movement & mcAnyMove) ||
            !READ_IF_EXISTS(pSettings, r_bool, HudSection(), "enable_directions_anm_idle_aim", false))
            return {};

        const char* longitudinal = (movement & mcFwd) ? "_forward" : (movement & mcBack) ? "_back" : "";
        const char* lateral = (movement & mcLStrafe) ? "_left" : (movement & mcRStrafe) ? "_right" : "";
        xr_sprintf(base, "anm_idle_aim_moving%s%s", longitudinal, lateral);
    } else {
        const bool crouching = (movement & mcCrouch) != 0;
        const bool slow = (movement & mcAccel) != 0;
        if (!crouching && !slow)
            return {};
        xr_sprintf(base, "anm_idle%s%s%s", (movement & mcAnyMove) ? "_moving" : "",
                   crouching ? "_crouch" : "", slow ? "_slow" : "");
    }

    if (IsGrenadeLauncherAttached())
        return HudStateVariant(base, nullptr);

    string256 variant;
    if (IsMisfire() || iAmmoElapsed == 0) {
        xr_sprintf(variant, "%s%s", base, IsMisfire() ? "_jammed" : "_empty");
        // A normal variant may put the slide in the wrong pose.
        return HudStateVariant(base, pSettings->line_exist(HudSection(), variant) ? variant : nullptr);
    }
    return HudStateVariant(base, pSettings->line_exist(HudSection(), base) ? base : nullptr);
}

void CWeaponMagazined::UpdateGunslingerMovementIdle() {
    const auto* actor = smart_cast<CActor*>(H_Parent());
    if (!actor || !GetHUDmode()) {
        m_previous_hud_movement = 0;
        return;
    }

    const u32 movement = actor->MovingState() &
        (mcAnyMove | mcSprint | mcCrouch | mcAccel | mcClimb | mcJump | mcFall);
    const u32 changed = movement ^ m_previous_hud_movement;
    m_previous_hud_movement = movement;
    if (!changed || GetState() != eIdle || GetNextState() != eIdle || IsPending())
        return;

    const bool sprint_changed = (changed & mcSprint) && !IsZoomed() && MovingAnimAllowedNow();
    LPCSTR current = m_current_motion.c_str();
    const bool sprint_transition = current &&
        std::strncmp(current, "anm_idle_sprint_", sizeof("anm_idle_sprint_") - 1) == 0;
    if (m_bStopAtEndAnimIsRunning && !(sprint_changed && sprint_transition))
        return;

    if (sprint_changed) {
        const bool entering = (movement & mcSprint) != 0;
        const LPCSTR base = entering ? "anm_idle_sprint_start" : "anm_idle_sprint_end";
        const LPCSTR fallback = entering
            ? StateMotion(base, "anm_idle_sprint_start_empty", "anm_idle_sprint_start_jammed")
            : StateMotion(base, "anm_idle_sprint_end_empty", "anm_idle_sprint_end_jammed");
        const shared_str motion = HudStateVariant(base, IsGrenadeLauncherAttached() ? nullptr : fallback);
        if (motion.c_str() && pSettings->line_exist(HudSection(), motion.c_str()) &&
            PlayHUDMotion(motion, TRUE, this, eIdle)) {
            if (pSettings->line_exist(cNameSect(), entering ? "snd_sprint_start" : "snd_sprint_end"))
                PlaySound(entering ? "sndSprintStart" : "sndSprintEnd", get_LastFP());
            return;
        }
        if (sprint_transition)
            StopCurrentAnimWithoutCallback();
    }

    PlayAnimIdle();
}

bool CWeaponMagazined::IsAimTransitionPlaying() const {
    LPCSTR motion = m_current_motion.c_str();
    LPCSTR transition = IsZoomed() ? "anm_idle_aim_start" : "anm_idle_aim_end";
    return m_bStopAtEndAnimIsRunning && motion &&
        std::strncmp(motion, transition, std::strlen(transition)) == 0;
}

void CWeaponMagazined::PlayAnimIdle() {
    if (GetState() != eIdle || IsAimTransitionPlaying())
        return;

    LPCSTR motion = m_current_motion.c_str();
    if (m_gunslinger_movement_idles && GetHUDmode() && !IsGrenadeLauncherAttached() &&
        MovingAnimAllowedNow()) {
        if (m_bStopAtEndAnimIsRunning && motion &&
            std::strncmp(motion, "anm_idle_sprint_", sizeof("anm_idle_sprint_") - 1) == 0)
            return;
        if (const auto* actor = smart_cast<CActor*>(H_Parent())) {
            const shared_str movement_motion = GunslingerMovementIdle(actor->MovingState());
            if (movement_motion.c_str()) {
                if (!m_current_motion_def || m_current_motion != movement_motion)
                    PlayHUDMotion(movement_motion, TRUE, this, eIdle);
                return;
            }
        }
    }

    if (IsZoomed()) {
        PlayAnimAim();
    } else if (!TryPlayAnimIdle()) {
        PlayHUDMotion(StateMotion("anm_idle", "anm_idle_empty", "anm_idle_jammed"),
                      TRUE, NULL, GetState());
    }
}

LPCSTR CWeaponMagazined::ShootMotion(LPCSTR fallback) const {
    // OnShot selects the motion before FireTrace consumes the cartridge.
    const bool last_shot = iAmmoElapsed == 1;
    const bool aimed = IsZoomed() && !IsRotatingToZoom();
    const bool silenced = IsSilencerAttached();

    if (silenced)
        fallback = OptionalHudMotion("anm_shoot_sil", fallback);
    if (aimed) {
        fallback = OptionalHudMotion("anm_shoot_aim", fallback);
        if (silenced)
            fallback = OptionalHudMotion("anm_shoot_aim_sil", fallback);
    }
    if (last_shot) {
        fallback = OptionalHudMotion("anm_shoot_last", fallback);
        if (silenced)
            fallback = OptionalHudMotion("anm_shoot_last_sil", fallback);
        if (aimed) {
            fallback = OptionalHudMotion("anm_shoot_aim_last", fallback);
            if (silenced)
                fallback = OptionalHudMotion("anm_shoot_aim_last_sil", fallback);
        }
    }
    if (IsMisfire()) {
        fallback = OptionalHudMotion("anm_shoot_jammed", fallback);
        if (silenced)
            fallback = OptionalHudMotion("anm_shoot_jammed_sil", fallback);
        if (aimed) {
            fallback = OptionalHudMotion("anm_shoot_aim_jammed", fallback);
            if (silenced)
                fallback = OptionalHudMotion("anm_shoot_aim_jammed_sil", fallback);
        }
    }
    return fallback;
}

BOOL CWeaponMagazined::MixShootAfterIdle() const {
    LPCSTR motion = m_current_motion.c_str();
    return motion && std::strncmp(motion, "anm_idle", sizeof("anm_idle") - 1) == 0 &&
           READ_IF_EXISTS(pSettings, r_bool, HudSection(), "mix_shoot_after_idle", false);
}

void CWeaponMagazined::PlayAnimShoot() {
    VERIFY(GetState() == eFire);
    PlayHUDMotion(ShootMotion(OptionalHudMotion("anm_shots", "anm_shoot")), MixShootAfterIdle(), this,
                  GetState());
}

void CWeaponMagazined::OnZoomIn() {
    inherited::OnZoomIn();

    if (GetState() == eIdle)
        PlayAnimIdle();

    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor) {
        CEffectorZoomInertion* S =
            smart_cast<CEffectorZoomInertion*>(pActor->Cameras().GetCamEffector(eCEZoom));
        if (!S) {
            S = (CEffectorZoomInertion*)pActor->Cameras().AddCamEffector(
                xr_new<CEffectorZoomInertion>());
            S->Init(this);
        };
        S->SetRndSeed(pActor->GetZoomRndSeed());
        R_ASSERT(S);
    }
}
void CWeaponMagazined::OnZoomOut() {
    if (!IsZoomed())
        return;

    inherited::OnZoomOut();

    if (GetState() == eIdle)
        PlayAnimIdle();

    CActor* pActor = smart_cast<CActor*>(H_Parent());

    if (pActor)
        pActor->Cameras().RemoveCamEffector(eCEZoom);
}

//ïåðåêëþ÷åíèå ðåæèìîâ ñòðåëüáû îäèíî÷íûìè è î÷åðåäÿìè
bool CWeaponMagazined::SwitchMode() {
    if (eIdle != GetState() || IsPending())
        return false;

    if (SingleShotMode())
        m_iQueueSize = WEAPON_ININITE_QUEUE;
    else
        m_iQueueSize = 1;

    PlaySound("sndEmptyClick", get_LastFP());

    return true;
}

void CWeaponMagazined::OnNextFireMode() {
    if (!m_bHasDifferentFireModes)
        return;
    if (GetState() != eIdle)
        return;
    m_iCurFireMode = (m_iCurFireMode + 1 + m_aFireModes.size()) % m_aFireModes.size();
    SetQueueSize(GetCurrentFireMode());
};

void CWeaponMagazined::OnPrevFireMode() {
    if (!m_bHasDifferentFireModes)
        return;
    if (GetState() != eIdle)
        return;
    m_iCurFireMode = (m_iCurFireMode - 1 + m_aFireModes.size()) % m_aFireModes.size();
    SetQueueSize(GetCurrentFireMode());
};

void CWeaponMagazined::OnH_A_Chield() {
    if (m_bHasDifferentFireModes) {
        CActor* actor = smart_cast<CActor*>(H_Parent());
        if (!actor)
            SetQueueSize(-1);
        else
            SetQueueSize(GetCurrentFireMode());
    };
    inherited::OnH_A_Chield();
};

void CWeaponMagazined::SetQueueSize(int size) { m_iQueueSize = size; };

float CWeaponMagazined::GetWeaponDeterioration() {
    // modified by Peacemaker [17.10.08]
    //	if (!m_bHasDifferentFireModes || m_iPrefferedFireMode == -1 || u32(GetCurrentFireMode()) <=
    //u32(m_iPrefferedFireMode)) 		return inherited::GetWeaponDeterioration();
    //	return m_iShotNum*conditionDecreasePerShot;
    return (m_iShotNum == 1) ? conditionDecreasePerShot : conditionDecreasePerQueueShot;
};

void CWeaponMagazined::save(NET_Packet& output_packet) {
    inherited::save(output_packet);
    save_data(m_iQueueSize, output_packet);
    save_data(m_iShotNum, output_packet);
    save_data(m_iCurFireMode, output_packet);
}

void CWeaponMagazined::load(IReader& input_packet) {
    inherited::load(input_packet);
    load_data(m_iQueueSize, input_packet);
    SetQueueSize(m_iQueueSize);
    load_data(m_iShotNum, input_packet);
    load_data(m_iCurFireMode, input_packet);
    if (!m_aFireModes.empty() &&
        (m_iCurFireMode < 0 || static_cast<size_t>(m_iCurFireMode) >= m_aFireModes.size())) {
        m_iCurFireMode = static_cast<int>(m_aFireModes.size()) - 1;
        SetQueueSize(GetCurrentFireMode());
    }
}

void CWeaponMagazined::net_Export(NET_Packet& P) {
    inherited::net_Export(P);

    P.w_u8(u8(m_iCurFireMode & 0x00ff));
}

void CWeaponMagazined::net_Import(NET_Packet& P) {
    inherited::net_Import(P);

    m_iCurFireMode = P.r_u8();
    SetQueueSize(GetCurrentFireMode());
}

#include "string_table.h"
bool CWeaponMagazined::GetBriefInfo(II_BriefInfo& info) {
    VERIFY(m_pInventory);
    string32 int_str;

    int ae = GetAmmoElapsed();
    xr_sprintf(int_str, "%d", ae);
    info.cur_ammo = int_str;

    if (HasFireModes()) {
        if (m_iQueueSize == WEAPON_ININITE_QUEUE) {
            info.fire_mode = "A";
        } else {
            xr_sprintf(int_str, "%d", m_iQueueSize);
            info.fire_mode = int_str;
        }
    } else
        info.fire_mode = "";

    if (m_pInventory->ModifyFrame() <= m_BriefInfo_CalcFrame) {
        return false;
    }
    GetSuitableAmmoTotal(); // update m_BriefInfo_CalcFrame
    info.grenade = "";

    u32 at_size = m_ammoTypes.size();
    if (unlimited_ammo() || at_size == 0) {
        info.fmj_ammo._set("--");
        info.ap_ammo._set("--");
    } else {
        // GetSuitableAmmoTotal(); //mp = all type

        xr_sprintf(int_str, "%d", GetAmmoCount(0)); // !!!!!!!!!!! == 0 temp
        if (m_ammoType == 0)
            info.fmj_ammo = int_str;
        else
            info.ap_ammo = int_str;

        if (at_size == 2) {
            xr_sprintf(int_str, "%d", GetAmmoCount(1));
            if (m_ammoType == 0)
                info.ap_ammo = int_str;
            else
                info.fmj_ammo = int_str;
        } else {
            info.ap_ammo = "";
        }
    }

    if (ae != 0 && m_magazine.size() != 0) {
        LPCSTR ammo_type = m_ammoTypes[m_magazine.back().m_LocalAmmoType].c_str();
        info.name = CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short"));
        info.icon = ammo_type;
    } else {
        LPCSTR ammo_type = m_ammoTypes[m_ammoType].c_str();
        info.name = CStringTable().translate(pSettings->r_string(ammo_type, "inv_name_short"));
        info.icon = ammo_type;
    }
    return true;
}

bool CWeaponMagazined::install_upgrade_impl(LPCSTR section, bool test) {
    bool result = inherited::install_upgrade_impl(section, test);

    LPCSTR str;
    // fire_modes = 1, 2, -1
    bool result2 = process_if_exists_set(section, "fire_modes", &CInifile::r_string, str, test);
    if (result2 && !test) {
        int ModesCount = _GetItemCount(str);
        m_aFireModes.clear();
        for (int i = 0; i < ModesCount; ++i) {
            string16 sItem;
            _GetItem(str, i, sItem);
            m_aFireModes.push_back((s8)atoi(sItem));
        }
        if (m_aFireModes.empty())
            m_aFireModes.push_back(1);
        m_bHasDifferentFireModes = true;
        m_iCurFireMode = static_cast<int>(m_aFireModes.size()) - 1;
        SetQueueSize(!H_Parent() || smart_cast<CActor*>(H_Parent()) ? GetCurrentFireMode() : -1);
    }
    result |= result2;

    result |= process_if_exists_set(section, "base_dispersioned_bullets_count", &CInifile::r_s32,
                                    m_iBaseDispersionedBulletsCount, test);
    result |= process_if_exists_set(section, "base_dispersioned_bullets_speed", &CInifile::r_float,
                                    m_fBaseDispersionedBulletsSpeed, test);

    // sounds (name of the sound, volume (0.0 - 1.0), delay (sec))
    result2 = process_if_exists_set(section, "snd_draw", &CInifile::r_string, str, test);
    if (result2 && !test) {
        m_sounds.ReloadSound(section, "snd_draw", "sndShow", false, m_eSoundShow);
    }
    result |= result2;

    result2 = process_if_exists_set(section, "snd_holster", &CInifile::r_string, str, test);
    if (result2 && !test) {
        m_sounds.ReloadSound(section, "snd_holster", "sndHide", false, m_eSoundHide);
    }
    result |= result2;

    result2 = process_if_exists_set(section, "snd_shoot", &CInifile::r_string, str, test);
    if (result2 && !test) {
        m_sounds.ReloadSound(section, "snd_shoot", "sndShot", false, m_eSoundShot);
    }
    result |= result2;

    result2 = process_if_exists_set(section, "snd_empty", &CInifile::r_string, str, test);
    if (result2 && !test) {
        m_sounds.ReloadSound(section, "snd_empty", "sndEmptyClick", false, m_eSoundEmptyClick);
    }
    result |= result2;

    result2 = process_if_exists_set(section, "snd_reload", &CInifile::r_string, str, test);
    if (result2 && !test) {
        m_sounds.ReloadSound(section, "snd_reload", "sndReload", true, m_eSoundReload);
    }
    result |= result2;

    // snd_shoot1     = weapons\ak74u_shot_1 ??
    // snd_shoot2     = weapons\ak74u_shot_2 ??
    // snd_shoot3     = weapons\ak74u_shot_3 ??

    if (m_eSilencerStatus == ALife::eAddonAttachable ||
        m_eSilencerStatus == ALife::eAddonPermanent) {
        result |= process_if_exists_set(section, "silencer_flame_particles", &CInifile::r_string,
                                        m_sSilencerFlameParticles, test);
        result |= process_if_exists_set(section, "silencer_smoke_particles", &CInifile::r_string,
                                        m_sSilencerSmokeParticles, test);

        result2 =
            process_if_exists_set(section, "snd_silncer_shot", &CInifile::r_string, str, test);
        if (result2 && !test) {
            m_sounds.ReloadSound(section, "snd_silncer_shot", "sndSilencerShot", false, m_eSoundShot);
        }
        result |= result2;
    }

    if (!test) {
        const bool silenced = IsSilencerAttached();
        const shared_str flame =
            silenced ? shared_str(m_sSilencerFlameParticles) : m_sFlameParticles;
        if (m_sFlameParticlesCurrent != flame) {
            StopFlameParticles();
            m_sFlameParticlesCurrent = flame;
        }
        m_sSmokeParticlesCurrent =
            silenced ? shared_str(m_sSilencerSmokeParticles) : m_sSmokeParticles;
        m_sSndShotCurrent = silenced ? "sndSilencerShot" : "sndShot";
    }

    bool zoom_changed = process_if_exists(section, "ironsight_zoom_factor", &CInifile::r_float,
                                          m_upgrade_ironsight_zoom, test);
    zoom_changed |= process_if_exists(section, "scope_zoom_factor", &CInifile::r_float,
                                      m_upgrade_scope_zoom, test);
    if (zoom_changed && !test)
        InitAddons();
    result |= zoom_changed;

    return result;
}
//òåêóùàÿ äèñïåðñèÿ (â ðàäèàíàõ) îðóæèÿ ñ ó÷åòîì èñïîëüçóåìîãî ïàòðîíà è íåäèñïåðñèîííûõ ïóëü
float CWeaponMagazined::GetFireDispersion(float cartridge_k, bool for_crosshair) {
    float fire_disp = GetBaseDispersion(cartridge_k);
    if (for_crosshair || !m_iBaseDispersionedBulletsCount || !m_iShotNum ||
        m_iShotNum > m_iBaseDispersionedBulletsCount) {
        fire_disp = inherited::GetFireDispersion(cartridge_k);
    }
    return fire_disp;
}
void CWeaponMagazined::FireBullet(const Fvector& pos, const Fvector& shot_dir, float fire_disp,
                                  const CCartridge& cartridge, u16 parent_id, u16 weapon_id,
                                  bool send_hit) {
    if (m_iBaseDispersionedBulletsCount) {
        if (m_iShotNum <= 1) {
            m_fOldBulletSpeed = GetBulletSpeed();
            SetBulletSpeed(m_fBaseDispersionedBulletsSpeed);
        } else if (m_iShotNum > m_iBaseDispersionedBulletsCount) {
            SetBulletSpeed(m_fOldBulletSpeed);
        }
    }
    inherited::FireBullet(pos, shot_dir, fire_disp, cartridge, parent_id, weapon_id, send_hit);
}
