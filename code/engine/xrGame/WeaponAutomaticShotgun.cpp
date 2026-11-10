#include "stdafx.h"

#include <algorithm>
#include "WeaponAutomaticShotgun.h"
#include "entity.h"
#include "ParticlesObject.h"
#include "xr_level_controller.h"
#include "inventory.h"
#include "InventoryOwner.h"
#include "level.h"
#include "actor.h"
#include "player_hud.h"

CWeaponAutomaticShotgun::CWeaponAutomaticShotgun() {
    m_eSoundOpen = ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING);
    m_eSoundClose = ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING);
    m_eSoundAddCartridge = ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING);
}

CWeaponAutomaticShotgun::~CWeaponAutomaticShotgun() {}

void CWeaponAutomaticShotgun::Load(LPCSTR section) {
    inherited::Load(section);

    if (pSettings->line_exist(section, "tri_state_reload")) {
        m_bTriStateReload = !!pSettings->r_bool(section, "tri_state_reload");
    };
    if (m_bTriStateReload) {
        m_add_cartridge_in_open = READ_IF_EXISTS(pSettings, r_bool, HudSection(), "add_cartridge_in_open", false);
        m_empty_preload_mode = READ_IF_EXISTS(pSettings, r_bool, HudSection(), "empty_preload_mode", false);
        m_keep_chambered_cartridge = READ_IF_EXISTS(pSettings, r_bool, section, "save_cartridge_in_ammochange", true);

        m_sounds.LoadSound(section, "snd_open_weapon", "sndOpen", false, m_eSoundOpen);

        m_sounds.LoadSound(section, "snd_add_cartridge", "sndAddCartridge", false,
                           m_eSoundAddCartridge);

        m_sounds.LoadSound(section, "snd_close_weapon", "sndClose", false, m_eSoundClose);
        if (pSettings->line_exist(section, "snd_add_cartridge_empty"))
            m_sounds.LoadSound(section, "snd_add_cartridge_empty", "sndChamber", false,
                               m_eSoundAddCartridge);
        if (pSettings->line_exist(section, "snd_open_weapon_empty"))
            m_sounds.LoadSound(section, "snd_open_weapon_empty", "sndOpenEmpty", false,
                               m_eSoundOpen);
        if (pSettings->line_exist(section, "snd_add_cartridge_preloaded"))
            m_sounds.LoadSound(section, "snd_add_cartridge_preloaded", "sndAddPreloaded", false,
                               m_eSoundAddCartridge);
        if (pSettings->line_exist(section, "snd_close_weapon_empty"))
            m_sounds.LoadSound(section, "snd_close_weapon_empty", "sndCloseEmpty", false, m_eSoundClose);
        if (pSettings->line_exist(section, "snd_close_weapon_preloaded"))
            m_sounds.LoadSound(section, "snd_close_weapon_preloaded", "sndClosePreloaded", false,
                               m_eSoundClose);
    };
}

bool CWeaponAutomaticShotgun::Action(u16 cmd, u32 flags) {
    if (UsesGunslingerTubeReload() && cmd == kWPN_FIRE &&
        (GetState() == eReload || GetNextState() == eReload)) {
        if ((flags & CMD_START) && !IsMisfire())
            m_tube_stop_requested = true;
        return true;
    }

    if (inherited::Action(cmd, flags))
        return true;

    if (m_bTriStateReload && GetState() == eReload && cmd == kWPN_FIRE && flags & CMD_START &&
        m_sub_state == eSubstateReloadInProcess) //остановить перезагрузку
    {
        AddCartridge(1);
        m_sub_state = eSubstateReloadEnd;
        return true;
    }
    return false;
}

void CWeaponAutomaticShotgun::OnAnimationEnd(u32 state) {
    if (!m_bTriStateReload || state != eReload)
        return inherited::OnAnimationEnd(state);

    if (UsesGunslingerTubeReload()) {
        FinishTubeReloadPhase();
        return;
    }

    switch (m_sub_state) {
    case eSubstateReloadChamber: {
        AddCartridge(1);
        m_sub_state = eSubstateReloadBegin;
        SwitchState(eReload);
    } break;
    case eSubstateReloadBegin: {
        m_sub_state = eSubstateReloadInProcess;
        SwitchState(eReload);
    } break;

    case eSubstateReloadInProcess: {
        if (0 != AddCartridge(1)) {
            m_sub_state = eSubstateReloadEnd;
        }
        SwitchState(eReload);
    } break;

    case eSubstateReloadEnd: {
        m_sub_state = eSubstateReloadBegin;
        SwitchState(eIdle);
    } break;
    };
}

void CWeaponAutomaticShotgun::Reload() {
    if (m_bTriStateReload) {
        TriStateReload();
    } else
        inherited::Reload();
}

void CWeaponAutomaticShotgun::TriStateReload() {
    if (UsesGunslingerTubeReload()) {
        if (IsPending() || GetState() == eReload || GetNextState() == eReload || !m_pInventory)
            return;

        if (!IsMisfire() && (iAmmoElapsed >= GetAmmoMagSize() || !HaveCartridgeInInventory(1))) {
            m_set_next_ammoType_on_reload = undefined_ammo_type;
            return;
        }

        ResetTubeReload();
        CWeapon::Reload();
        SetPending(TRUE);
        SwitchState(eReload);
        return;
    }

    if (m_magazine.size() == (u32)iMagazineSize || !HaveCartridgeInInventory(1))
        return;
    CWeapon::Reload();
    m_sub_state = iAmmoElapsed == 0 &&
                  pSettings->line_exist(HudSection().c_str(), "anm_add_cartridge_empty")
                      ? eSubstateReloadChamber : eSubstateReloadBegin;
    SwitchState(eReload);
}

void CWeaponAutomaticShotgun::OnStateSwitch(u32 S) {
    if (UsesGunslingerTubeReload()) {
        if (S == eReload) {
            CWeapon::OnStateSwitch(S);
            CWeapon::FireEnd();
            if (auto* owner = smart_cast<CInventoryOwner*>(H_Parent()))
                m_sounds_enabled = owner->CanPlayShHdRldSounds();
            StartTubeReloadPhase();
        } else {
            ResetTubeReload();
            inherited::OnStateSwitch(S);
        }
        return;
    }

    if (!m_bTriStateReload || S != eReload) {
        inherited::OnStateSwitch(S);
        return;
    }

    CWeapon::OnStateSwitch(S);

    if (m_magazine.size() == (u32)iMagazineSize || !HaveCartridgeInInventory(1)) {
        switch2_EndReload();
        m_sub_state = eSubstateReloadEnd;
        return;
    };

    switch (m_sub_state) {
    case eSubstateReloadChamber:
        if (pSettings->line_exist(cNameSect().c_str(), "snd_add_cartridge_empty"))
            PlaySound("sndChamber", get_LastFP());
        else
            PlaySound("sndAddCartridge", get_LastFP());
        PlayHUDMotion("anm_add_cartridge_empty", FALSE, this, GetState());
        SetPending(TRUE);
        break;
    case eSubstateReloadBegin:
        if (HaveCartridgeInInventory(1))
            switch2_StartReload();
        break;
    case eSubstateReloadInProcess:
        if (HaveCartridgeInInventory(1))
            switch2_AddCartgidge();
        break;
    case eSubstateReloadEnd:
        switch2_EndReload();
        break;
    };
}

bool CWeaponAutomaticShotgun::UsesGunslingerTubeReload() const {
    return m_bTriStateReload && HasChamberRound() && IsGameTypeSingle() &&
        (m_add_cartridge_in_open || m_empty_preload_mode);
}

void CWeaponAutomaticShotgun::ResetTubeReload() {
    m_sub_state = eSubstateReloadBegin;
    m_tube_preloaded = false;
    m_tube_insert_pending = false;
    m_tube_stop_requested = false;
}

void CWeaponAutomaticShotgun::StartTubeReloadPhase() {
    if (IsMisfire()) {
        PlayTubeReloadPhase(ReloadMotion(), ReloadSound(), false);
        return;
    }

    const bool empty = iAmmoElapsed == 0;
    const auto& section = cNameSect();
    switch (m_sub_state) {
    case eSubstateReloadBegin:
        m_tube_preloaded = empty && m_empty_preload_mode;
        PlayTubeReloadPhase(empty ? OptionalHudMotion("anm_open_empty", "anm_open") : "anm_open",
            empty && pSettings->line_exist(section, "snd_open_weapon_empty") ? "sndOpenEmpty" : "sndOpen",
            m_add_cartridge_in_open);
        break;
    case eSubstateReloadInProcess: {
        const LPCSTR preloaded = m_tube_preloaded
            ? OptionalHudMotion(empty ? "anm_add_cartridge_empty_preloaded" : "anm_add_cartridge_preloaded", nullptr)
            : nullptr;
        const LPCSTR motion = preloaded ? preloaded
            : empty ? OptionalHudMotion("anm_add_cartridge_empty", "anm_add_cartridge") : "anm_add_cartridge";
        LPCSTR sound = empty && pSettings->line_exist(section, "snd_add_cartridge_empty")
            ? "sndChamber" : "sndAddCartridge";
        if (preloaded && pSettings->line_exist(section, "snd_add_cartridge_preloaded"))
            sound = "sndAddPreloaded";
        PlayTubeReloadPhase(motion, sound, true);
        break;
    }
    case eSubstateReloadEnd: {
        const LPCSTR preloaded = m_tube_preloaded
            ? OptionalHudMotion(empty ? "anm_close_empty_preloaded" : "anm_close_preloaded", nullptr)
            : nullptr;
        const LPCSTR motion = preloaded ? preloaded
            : empty ? OptionalHudMotion("anm_close_empty", "anm_close") : "anm_close";
        LPCSTR sound = empty && pSettings->line_exist(section, "snd_close_weapon_empty")
            ? "sndCloseEmpty" : "sndClose";
        if (preloaded && pSettings->line_exist(section, "snd_close_weapon_preloaded"))
            sound = "sndClosePreloaded";
        PlayTubeReloadPhase(motion, sound, false);
        break;
    }
    }
}

void CWeaponAutomaticShotgun::PlayTubeReloadPhase(LPCSTR motion, LPCSTR sound, bool inserts_shell) {
    SetPending(TRUE);
    m_tube_insert_pending = inserts_shell;

    const auto& hud = HudSection();
    const bool has_motion = pSettings->line_exist(hud, motion) &&
        (!GetHUDmode() || g_player_hud->has_hud_motion(hud, motion));
    const u32 duration = has_motion ? PlayHUDMotion(motion, FALSE, this, eReload) : 0;
    if (m_sounds_enabled)
        PlaySound(sound, get_LastFP());

    if (!duration || !m_current_motion_def) {
        StopCurrentAnimWithoutCallback();
        FinishTubeReloadPhase();
        return;
    }

    m_tube_insert_at_ms = duration;
    m_tube_interrupt_at_ms = duration;
    if (!GetHUDmode())
        return;

    string256 key;
    xr_sprintf(key, "lock_time_start_%s", motion);
    const float start = READ_IF_EXISTS(pSettings, r_float, hud, key, float(duration) * 0.001f);
    m_tube_insert_at_ms = static_cast<u32>(clampr(start * 1000.f, 0.f, float(duration)));

    xr_sprintf(key, "lock_time_end_%s", motion);
    if (pSettings->line_exist(hud, key)) {
        const float end = pSettings->r_float(hud, key);
        m_tube_interrupt_at_ms = static_cast<u32>(
            clampr(float(m_tube_insert_at_ms) + end * 1000.f, float(m_tube_insert_at_ms), float(duration)));
    }
}

void CWeaponAutomaticShotgun::CommitTubeReloadShell() {
    if (!m_tube_insert_pending)
        return;

    m_tube_insert_pending = false;
    if (iAmmoElapsed >= GetAmmoMagSize() || AddCartridge(1) != 0) {
        m_tube_stop_requested = true;
        return;
    }

    // FireTrace takes the last cartridge. Keep the chambered shell ahead of the newly filled tube.
    if (m_keep_chambered_cartridge && m_magazine.size() > 1)
        std::iter_swap(m_magazine.end() - 2, m_magazine.end() - 1);

    m_BriefInfo_CalcFrame = 0;
    if (m_sub_state == eSubstateReloadInProcess)
        m_tube_preloaded = false;
}

void CWeaponAutomaticShotgun::FinishTubeReloadPhase() {
    if (GetState() != eReload || GetNextState() != eReload)
        return;

    if (IsMisfire()) {
        ResetTubeReload();
        inherited::OnAnimationEnd(eReload);
        return;
    }
    if (m_sub_state == eSubstateReloadEnd) {
        ResetTubeReload();
        m_set_next_ammoType_on_reload = undefined_ammo_type;
        SwitchState(eIdle);
        return;
    }

    CommitTubeReloadShell();
    m_sub_state = !m_tube_stop_requested && iAmmoElapsed < GetAmmoMagSize() && HaveCartridgeInInventory(1)
        ? eSubstateReloadInProcess : eSubstateReloadEnd;
    SwitchState(eReload);
}

void CWeaponAutomaticShotgun::UpdateCL() {
    if (UsesGunslingerTubeReload() && GetState() == eReload && GetNextState() == eReload &&
        m_sub_state != eSubstateReloadEnd && !IsMisfire() && m_pInventory &&
        m_bStopAtEndAnimIsRunning && m_startedMotionState == eReload) {
        const u32 elapsed = Device.dwTimeGlobal - m_dwMotionStartTm;
        if (elapsed >= m_tube_insert_at_ms)
            CommitTubeReloadShell();
        if (m_tube_stop_requested && elapsed >= m_tube_interrupt_at_ms) {
            StopCurrentAnimWithoutCallback();
            m_sub_state = eSubstateReloadEnd;
            SwitchState(eReload);
        }
    }

    inherited::UpdateCL();
}

void CWeaponAutomaticShotgun::switch2_StartReload() {
    if (!iAmmoElapsed && pSettings->line_exist(cNameSect().c_str(), "snd_open_weapon_empty"))
        PlaySound("sndOpenEmpty", get_LastFP());
    else
        PlaySound("sndOpen", get_LastFP());
    PlayAnimOpenWeapon();
    SetPending(TRUE);
}

void CWeaponAutomaticShotgun::switch2_AddCartgidge() {
    PlaySound("sndAddCartridge", get_LastFP());
    PlayAnimAddOneCartridgeWeapon();
    SetPending(TRUE);
}

void CWeaponAutomaticShotgun::switch2_EndReload() {
    SetPending(FALSE);
    PlaySound("sndClose", get_LastFP());
    PlayAnimCloseWeapon();
}

void CWeaponAutomaticShotgun::PlayAnimOpenWeapon() {
    VERIFY(GetState() == eReload);
    LPCSTR key = !iAmmoElapsed && pSettings->line_exist(HudSection().c_str(), "anm_open_empty")
                     ? "anm_open_empty" : "anm_open";
    PlayHUDMotion(key, FALSE, this, GetState());
}
void CWeaponAutomaticShotgun::PlayAnimAddOneCartridgeWeapon() {
    VERIFY(GetState() == eReload);
    PlayHUDMotion("anm_add_cartridge", FALSE, this, GetState());
}
void CWeaponAutomaticShotgun::PlayAnimCloseWeapon() {
    VERIFY(GetState() == eReload);

    PlayHUDMotion("anm_close", FALSE, this, GetState());
}

bool CWeaponAutomaticShotgun::HaveCartridgeInInventory(u8 cnt) {
    if (unlimited_ammo())
        return true;
    if (!m_pInventory)
        return false;

    u32 ac = GetAmmoCount(m_ammoType);
    if (ac < cnt) {
        for (u8 i = 0; i < u8(m_ammoTypes.size()); ++i) {
            if (m_ammoType == i)
                continue;
            ac += GetAmmoCount(i);
            if (ac >= cnt) {
                m_ammoType = i;
                break;
            }
        }
    }
    return ac >= cnt;
}

u8 CWeaponAutomaticShotgun::AddCartridge(u8 cnt) {
    if (IsMisfire())
        bMisfire = false;

    if (m_set_next_ammoType_on_reload != undefined_ammo_type) {
        m_ammoType = m_set_next_ammoType_on_reload;
        m_set_next_ammoType_on_reload = undefined_ammo_type;
    }

    if (!m_pInventory || !HaveCartridgeInInventory(1))
        return cnt;

    m_pCurrentAmmo =
        smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(m_ammoTypes[m_ammoType].c_str()));
    if (!m_pCurrentAmmo && !unlimited_ammo())
        return cnt;

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
        m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
    CCartridge l_cartridge = m_DefaultCartridge;
    while (cnt) {
        if (!unlimited_ammo()) {
            if (!m_pCurrentAmmo->Get(l_cartridge))
                break;
        }
        --cnt;
        ++iAmmoElapsed;
        l_cartridge.m_LocalAmmoType = m_ammoType;
        m_magazine.push_back(l_cartridge);
        //		m_fCurrentCartirdgeDisp = l_cartridge.m_kDisp;
    }

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    //выкинуть коробку патронов, если она пустая
    if (m_pCurrentAmmo && !m_pCurrentAmmo->m_boxCurr && OnServer())
        m_pCurrentAmmo->SetDropManual(TRUE);

    return cnt;
}

void CWeaponAutomaticShotgun::net_Export(NET_Packet& P) {
    inherited::net_Export(P);
    P.w_u8(u8(m_magazine.size()));
    for (u32 i = 0; i < m_magazine.size(); i++) {
        CCartridge& l_cartridge = *(m_magazine.begin() + i);
        P.w_u8(l_cartridge.m_LocalAmmoType);
    }
}

void CWeaponAutomaticShotgun::net_Import(NET_Packet& P) {
    inherited::net_Import(P);
    u8 AmmoCount = P.r_u8();
    for (u32 i = 0; i < AmmoCount; i++) {
        u8 LocalAmmoType = P.r_u8();
        if (i >= m_magazine.size())
            continue;
        CCartridge& l_cartridge = *(m_magazine.begin() + i);
        if (LocalAmmoType == l_cartridge.m_LocalAmmoType)
            continue;
#ifdef DEBUG
        Msg("! %s reload to %s", *l_cartridge.m_ammoSect, m_ammoTypes[LocalAmmoType].c_str());
#endif
        l_cartridge.Load(m_ammoTypes[LocalAmmoType].c_str(), LocalAmmoType);
    }
}
