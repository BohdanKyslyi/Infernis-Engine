#include "stdafx.h"

#include <algorithm>
#include <cstring>
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
        m_revolver_reload = READ_IF_EXISTS(pSettings, r_bool, HudSection(), "revolver_reload", false);
        m_need_final_close_anims = READ_IF_EXISTS(pSettings, r_bool, section, "need_final_close_anims", false);
        m_need_first_shoot_anims = READ_IF_EXISTS(pSettings, r_bool, section, "need_first_shoot_anims", false);
        LoadTubeReloadSounds();

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
    return m_bTriStateReload && IsGameTypeSingle() &&
        (m_add_cartridge_in_open || m_empty_preload_mode || m_revolver_reload || m_need_final_close_anims);
}

void CWeaponAutomaticShotgun::reinit() {
    inherited::reinit();
    m_just_after_reload = false;
    m_saved_reload_state = false;
    m_saved_cartridge_types.clear();
    ResetTubeReload();
}

BOOL CWeaponAutomaticShotgun::net_Spawn(CSE_Abstract* DC) {
    const BOOL result = inherited::net_Spawn(DC);
    if (result && m_saved_reload_state) {
        if (m_saved_cartridge_types.size() == m_magazine.size()) {
            for (u32 i = 0; i < m_magazine.size(); ++i) {
                const u8 type = m_saved_cartridge_types[i];
                m_magazine[i].Load(m_ammoTypes[type].c_str(), type);
            }
        }
        // Resume from the saved cartridge count, never replay a pending insertion.
        if (GetState() == eReload) {
            StopCurrentAnimWithoutCallback();
            SetPending(FALSE);
            SetState(eIdle);
            SetNextState(eIdle);
        }
    }
    m_saved_reload_state = false;
    m_saved_cartridge_types.clear();
    return result;
}

void CWeaponAutomaticShotgun::save(NET_Packet& output_packet) {
    inherited::save(output_packet);
    if (!UsesGunslingerTubeReload())
        return;

    // Optional, versioned extension before the script binder payload.
    // Old saves have no marker and are read without consuming their script data.
    const u16 count = static_cast<u16>(m_magazine.size());
    output_packet.w_u32(0x31415347); // GSA1
    output_packet.w_u32(0x31444c52); // RLD1
    output_packet.w_u16(static_cast<u16>(4 + count));
    output_packet.w_u8(1); // version
    output_packet.w_u8((m_just_after_reload ? 1 : 0) | (IsMisfire() ? 2 : 0));
    output_packet.w_u16(count);
    for (const auto& cartridge : m_magazine)
        output_packet.w_u8(cartridge.m_LocalAmmoType);
}

void CWeaponAutomaticShotgun::load(IReader& input_packet) {
    m_just_after_reload = false;
    m_saved_reload_state = false;
    m_saved_cartridge_types.clear();
    ResetTubeReload();
    inherited::load(input_packet);

    if (input_packet.elapsed() < 10)
        return;
    const u32 start = input_packet.tell();
    if (input_packet.r_u32() != 0x31415347 || input_packet.r_u32() != 0x31444c52) {
        input_packet.seek(start);
        return;
    }
    const u16 size = input_packet.r_u16();
    if (size > input_packet.elapsed()) {
        input_packet.seek(start);
        return;
    }
    const u32 end = input_packet.tell() + size;
    if (size >= 4 && input_packet.r_u8() == 1) {
        const u8 flags = input_packet.r_u8();
        const u16 count = input_packet.r_u16();
        if (count <= size - 4 && count == iAmmoElapsed && UsesGunslingerTubeReload()) {
            m_saved_reload_state = true;
            m_just_after_reload = (flags & 1) != 0;
            bMisfire = (flags & 2) != 0;
            for (u16 i = 0; i < count; ++i) {
                const u8 type = input_packet.r_u8();
                m_saved_cartridge_types.push_back(type < m_ammoTypes.size() ? type : m_ammoType);
            }
        }
    }
    input_packet.seek(end);
}

bool CWeaponAutomaticShotgun::UsesFirstShotMotion() const {
    return UsesGunslingerTubeReload() && m_need_first_shoot_anims && m_just_after_reload && iAmmoElapsed > 0;
}

shared_str CWeaponAutomaticShotgun::HudStateVariant(LPCSTR base, LPCSTR fallback) const {
    if (UsesFirstShotMotion() && !IsMisfire()) {
        string256 variant;
        xr_sprintf(variant, "%s_first", base);
        if (pSettings->line_exist(HudSection(), variant))
            return shared_str(variant);
    }
    return inherited::HudStateVariant(base, fallback);
}

LPCSTR CWeaponAutomaticShotgun::StateMotion(LPCSTR normal, LPCSTR empty, LPCSTR jammed) const {
    m_first_state_motion = HudStateVariant(normal, inherited::StateMotion(normal, empty, jammed));
    return m_first_state_motion.c_str();
}

shared_str CWeaponAutomaticShotgun::InspectMotion() const {
    return HudStateVariant("anm_bore", inherited::InspectMotion().c_str());
}

void CWeaponAutomaticShotgun::PlayAnimShoot() {
    if (!UsesFirstShotMotion()) {
        inherited::PlayAnimShoot();
        return;
    }
    string256 variant;
    const bool aimed = IsZoomed() && !IsRotatingToZoom();
    xr_sprintf(variant, "anm_shoot%s%s_first%s", aimed ? "_aim" : "",
        IsMisfire() ? "_jammed" : iAmmoElapsed == 1 ? "_last" : "",
        IsSilencerAttached() ? "_sil" : "");
    LPCSTR motion = OptionalHudMotion(variant, nullptr);
    if (!motion && IsMisfire()) {
        inherited::PlayAnimShoot();
        return;
    }
    if (!motion && aimed) {
        xr_sprintf(variant, "anm_shoot_aim_first%s", IsSilencerAttached() ? "_sil" : "");
        motion = OptionalHudMotion(variant, nullptr);
    }
    if (!motion)
        motion = OptionalHudMotion("anm_shoot_first", nullptr);
    if (motion)
        PlayHUDMotion(motion, MixShootAfterIdle(), this, GetState());
    else
        inherited::PlayAnimShoot();
}

void CWeaponAutomaticShotgun::OnShot() {
    inherited::OnShot();
    m_just_after_reload = false;
}

bool CWeaponAutomaticShotgun::RollForMisfire() {
    if (UsesFirstShotMotion() && READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "no_jam_in_first_shot", false))
        return false;
    return inherited::RollForMisfire();
}

void CWeaponAutomaticShotgun::LoadTubeReloadSounds() {
    const shared_str hud = HudSection();
    LPCSTR key, value;
    for (u32 i = 0; pSettings->r_line(hud.c_str(), i, &key, &value); ++i) {
        if (std::strncmp(key, "snd_anm_open", 12) != 0 &&
            std::strncmp(key, "snd_anm_add_cartridge", 21) != 0 &&
            std::strncmp(key, "snd_anm_close", 13) != 0 &&
            std::strncmp(key, "snd_anm_reload_jammed", 21) != 0)
            continue;
        if (!value || !value[0] || xr_strcmp(value, "$silent") == 0)
            continue;
        m_sounds.LoadSound(hud.c_str(), key, key, true, m_eSoundReload);
        m_tube_sound_aliases.emplace_back(key);
    }
}

shared_str CWeaponAutomaticShotgun::TubeReloadMotion(LPCSTR base, bool preloaded, bool final) const {
    string256 variant;
    xr_sprintf(variant, "%s%s", base, iAmmoElapsed == 0 ? "_empty" : UsesFirstShotMotion() ? "_first" : "");
    shared_str motion = OptionalHudMotion(variant, base);
    if (preloaded) {
        xr_sprintf(variant, "%s_preloaded", motion.c_str());
        motion = OptionalHudMotion(variant, motion.c_str());
    }
    if (final) {
        xr_sprintf(variant, "%s_final", motion.c_str());
        motion = OptionalHudMotion(variant, motion.c_str());
    }
    return motion;
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
    case eSubstateReloadBegin: {
        m_tube_preloaded = empty && m_empty_preload_mode;
        const shared_str motion = TubeReloadMotion("anm_open");
        PlayTubeReloadPhase(motion.c_str(),
            empty && pSettings->line_exist(section, "snd_open_weapon_empty") ? "sndOpenEmpty" : "sndOpen",
            m_add_cartridge_in_open);
        break;
    }
    case eSubstateReloadInProcess: {
        const shared_str motion = TubeReloadMotion("anm_add_cartridge", m_tube_preloaded);
        LPCSTR sound = empty && pSettings->line_exist(section, "snd_add_cartridge_empty")
            ? "sndChamber" : "sndAddCartridge";
        if (m_tube_preloaded && pSettings->line_exist(section, "snd_add_cartridge_preloaded"))
            sound = "sndAddPreloaded";
        PlayTubeReloadPhase(motion.c_str(), sound, true);
        break;
    }
    case eSubstateReloadEnd: {
        const shared_str motion = TubeReloadMotion("anm_close", m_tube_preloaded,
            m_need_final_close_anims && iAmmoElapsed == GetAmmoMagSize());
        LPCSTR sound = empty && pSettings->line_exist(section, "snd_close_weapon_empty")
            ? "sndCloseEmpty" : "sndClose";
        if (m_tube_preloaded && pSettings->line_exist(section, "snd_close_weapon_preloaded"))
            sound = "sndClosePreloaded";
        PlayTubeReloadPhase(motion.c_str(), sound, false);
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
    if (m_sounds_enabled) {
        string256 sound_key;
        xr_sprintf(sound_key, "snd_%s", motion);
        if (pSettings->line_exist(hud, sound_key)) {
            const LPCSTR value = pSettings->r_string(hud, sound_key);
            if (value && value[0] && xr_strcmp(value, "$silent") != 0)
                PlaySound(sound_key, get_LastFP());
        } else {
            PlaySound(sound, get_LastFP());
        }
    }

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
    const bool keep_chamber = HasChamberRound() && m_keep_chambered_cartridge;
    if (keep_chamber && m_magazine.size() > 1)
        std::iter_swap(m_magazine.end() - 2, m_magazine.end() - 1);

    // Optional FIFO loading: FireTrace pops the back, so new drum rounds go at the front.
    if (m_revolver_reload) {
        auto inserted = m_magazine.end() - (keep_chamber && m_magazine.size() > 1 ? 2 : 1);
        std::rotate(m_magazine.begin(), inserted, inserted + 1);
    }
    m_just_after_reload = true;

    m_BriefInfo_CalcFrame = 0;
    if (m_sub_state == eSubstateReloadInProcess)
        m_tube_preloaded = false;
}

void CWeaponAutomaticShotgun::FinishTubeReloadPhase() {
    if (GetState() != eReload || GetNextState() != eReload)
        return;

    if (IsMisfire()) {
        ResetTubeReload();
        if (READ_IF_EXISTS(pSettings, r_bool, HudSection(), "first_state_after_jammed", true))
            m_just_after_reload = true;
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
    for (const auto& alias : m_tube_sound_aliases)
        m_sounds.SetPosition(alias.c_str(), get_LastFP());
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
