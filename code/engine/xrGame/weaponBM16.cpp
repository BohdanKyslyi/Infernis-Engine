#include "stdafx.h"
#include "weaponBM16.h"

#include <algorithm>
#include <cstring>
#include "inventory.h"
#include "Level.h"
#include "player_hud.h"
#include "WeaponAmmo.h"

namespace {
bool IsDoubleBarrelHud(const shared_str& hud) {
    return pSettings->line_exist(hud, "anm_shoot_1") &&
        pSettings->line_exist(hud, "anm_shoot_2") &&
        pSettings->line_exist(hud, "anm_reload_0");
}

bool HasSound(LPCSTR section, LPCSTR key) {
    const LPCSTR value = READ_IF_EXISTS(pSettings, r_string, section, key, nullptr);
    if (!value || !value[0])
        return false;
    string256 path;
    _GetItem(value, 0, path);
    return path[0] && xr_strcmp(path, "$silent") != 0;
}
} // namespace

CWeaponBM16::~CWeaponBM16() {}

void CWeaponBM16::Load(LPCSTR section) {
    inherited::Load(section);
    const auto& hud = HudSection();
    m_gunslinger_double_barrel = IsDoubleBarrelHud(hud);
    m_no_jam_fire = READ_IF_EXISTS(pSettings, r_bool, hud, "no_jam_fire", false);
    m_has_legacy_reload_sound = HasSound(section, "snd_reload_1");
    if (m_has_legacy_reload_sound)
        m_sounds.LoadSound(section, "snd_reload_1", "sndReload1", true, m_eSoundShot);
    if (m_gunslinger_double_barrel && !m_bTriStateReload)
        LoadGunslingerSounds(section);
}

bool CWeaponBM16::UsesGunslingerDoubleBarrel() const {
    return m_gunslinger_double_barrel && !m_bTriStateReload && IsGameTypeSingle();
}

bool CWeaponBM16::UsesScopeAnimations() const {
    return IsScopeAttached() && !IsAlternativeAimActive() &&
        READ_IF_EXISTS(pSettings, r_bool, HudSection(), "aim_scope_anims", true) &&
        READ_IF_EXISTS(pSettings, r_bool, ScopeSettingSection("use_scope_anims"), "use_scope_anims", false);
}

shared_str CWeaponBM16::NumberedMotion(LPCSTR base) const {
    const int count = clampr(iAmmoElapsed, 0, 2);
    string256 key;
    if (IsMisfire()) {
        xr_sprintf(key, "%s_jammed_%d", base, count);
        if (pSettings->line_exist(HudSection(), key))
            return shared_str(key);
    }
    xr_sprintf(key, "%s_%d", base, count);
    return pSettings->line_exist(HudSection(), key) ? shared_str(key) : shared_str();
}

shared_str CWeaponBM16::HudStateVariant(LPCSTR base, LPCSTR fallback) const {
    if (UsesGunslingerDoubleBarrel()) {
        if (UsesScopeAnimations()) {
            LPCSTR scope_base = nullptr;
            if (xr_strcmp(base, "anm_idle_aim") == 0)
                scope_base = "anm_idle_aim_scope";
            else if (std::strncmp(base, "anm_idle_aim_moving", sizeof("anm_idle_aim_moving") - 1) == 0)
                scope_base = "anm_idle_aim_scope_moving";
            if (scope_base) {
                const shared_str scoped = NumberedMotion(scope_base);
                if (scoped.c_str())
                    return scoped;
            }
        }
        const shared_str numbered = NumberedMotion(base);
        if (numbered.c_str())
            return numbered;
    }
    return inherited::HudStateVariant(base, fallback);
}

LPCSTR CWeaponBM16::StateMotion(LPCSTR normal, LPCSTR empty, LPCSTR jammed) const {
    const LPCSTR fallback = inherited::StateMotion(normal, empty, jammed);
    if (!UsesGunslingerDoubleBarrel())
        return fallback;
    m_numbered_state_motion = HudStateVariant(normal, fallback);
    return m_numbered_state_motion.c_str();
}

shared_str CWeaponBM16::SelectDryFireMotion(LPCSTR normal, LPCSTR aimed) const {
    if (UsesGunslingerDoubleBarrel()) {
        if (IsZoomed() && !IsRotatingToZoom()) {
            const shared_str motion = NumberedMotion("anm_fakeshoot_aim");
            if (motion.c_str())
                return motion;
        }
        const shared_str motion = NumberedMotion("anm_fakeshoot");
        if (motion.c_str())
            return motion;
    }
    return inherited::SelectDryFireMotion(normal, aimed);
}

shared_str CWeaponBM16::InspectMotion() const {
    const shared_str fallback = inherited::InspectMotion();
    if (!UsesGunslingerDoubleBarrel())
        return fallback;
    const shared_str custom = NumberedMotion("anm_boring");
    if (custom.c_str())
        return custom;
    if (fallback.c_str() && std::strncmp(fallback.c_str(), "anm_boring", sizeof("anm_boring") - 1) == 0)
        return fallback;
    if (READ_IF_EXISTS(pSettings, r_bool, HudSection(), "disable_bore", false))
        return {};
    return HudStateVariant("anm_bore", fallback.c_str());
}

LPCSTR CWeaponBM16::ShotSound() const {
    if (UsesGunslingerDoubleBarrel() && iAmmoElapsed == 1 && m_has_one_round_shot_sound && !IsSilencerAttached())
        return "snd_shoot_1";
    return inherited::ShotSound();
}

bool CWeaponBM16::JamPreventsShot() const {
    return UsesGunslingerDoubleBarrel() && m_no_jam_fire;
}

bool CWeaponBM16::HasChamberRound() const {
    return !UsesGunslingerDoubleBarrel() && inherited::HasChamberRound();
}

void CWeaponBM16::LoadGunslingerSounds(LPCSTR section) {
    m_has_one_round_shot_sound = HasSound(section, "snd_shoot_1");
    if (m_has_one_round_shot_sound)
        m_sounds.LoadSound(section, "snd_shoot_1", "snd_shoot_1", false, m_eSoundShot);

    const LPCSTR extra_sounds[] = {"snd_reload_only", "snd_reload_only_ammochange",
        "snd_changecartridgetype_only", "snd_changecartridgetype_one", "snd_changecartridgetype_one_only"};
    for (const LPCSTR key : extra_sounds) {
        if (HasSound(section, key))
            m_sounds.LoadSound(section, key, key, true, m_eSoundReload);
    }

    LoadHudReloadSounds();
}

void CWeaponBM16::LoadHudReloadSounds() {
    const auto& hud = HudSection();
    if (std::find(m_loaded_reload_sound_huds.begin(), m_loaded_reload_sound_huds.end(), hud) !=
        m_loaded_reload_sound_huds.end())
        return;

    LPCSTR key, value;
    for (u32 i = 0; pSettings->r_line(hud.c_str(), i, &key, &value); ++i) {
        if (std::strncmp(key, "snd_anm_reload", sizeof("snd_anm_reload") - 1) != 0 || !HasSound(hud.c_str(), key))
            continue;
        string512 alias;
        xr_sprintf(alias, "%s.%s", hud.c_str(), key);
        m_sounds.LoadSound(hud.c_str(), key, alias, true, m_eSoundReload);
    }
    m_loaded_reload_sound_huds.push_back(hud);
}

shared_str CWeaponBM16::SelectReloadSound(LPCSTR weapon_key) const {
    if (m_reload_motion.c_str()) {
        string256 key;
        xr_sprintf(key, "snd_%s", m_reload_motion.c_str());
        const LPCSTR hud = HudSection().c_str();
        if (pSettings->line_exist(hud, key)) {
            if (!HasSound(hud, key))
                return {};
            string512 alias;
            xr_sprintf(alias, "%s.%s", hud, key);
            return shared_str(alias);
        }
    }

    const LPCSTR section = cNameSect().c_str();
    if (weapon_key && HasSound(section, weapon_key))
        return xr_strcmp(weapon_key, "snd_reload") == 0 ? "sndReload" : weapon_key;
    if (iAmmoElapsed == 1 && m_has_legacy_reload_sound)
        return "sndReload1";
    return HasSound(section, "snd_reload") ? shared_str("sndReload") : shared_str();
}

bool CWeaponBM16::install_upgrade_impl(LPCSTR section, bool test) {
    shared_str target;
    if (UsesGunslingerDoubleBarrel()) {
        const LPCSTR hud = READ_IF_EXISTS(pSettings, r_string, section, "hud", nullptr);
        if (hud && hud[0] && pSettings->section_exist(hud) &&
            pSettings->line_exist(hud, "item_visual") &&
            READ_IF_EXISTS(pSettings, r_u16, hud, "attach_place_idx", u16(-1)) == 0 &&
            IsDoubleBarrelHud(hud))
            target = hud;
    }

    const bool installed = inherited::install_upgrade_impl(section, test);
    if (test || !UsesGunslingerDoubleBarrel())
        return installed || target.c_str() != nullptr;

    if (pSettings->line_exist(section, "ammo_class") && m_ammoType < m_ammoTypes.size()) {
        m_DefaultCartridge.Load(m_ammoTypes[m_ammoType].c_str(), m_ammoType);
        m_pCurrentAmmo = nullptr;
        // A pending reload refers to indices from the previous ammo_class list.
        m_reload_commit_pending = false;
        m_set_next_ammoType_on_reload = undefined_ammo_type;
        m_BriefInfo_CalcFrame = 0;
    }

    if (!target.c_str())
        return installed;

    m_pending_hud_upgrade = target;
    ApplyPendingHudUpgrade();
    return true;
}

void CWeaponBM16::ApplyPendingHudUpgrade() {
    if (!m_pending_hud_upgrade.c_str())
        return;
    if (m_pending_hud_upgrade == HudSection()) {
        m_pending_hud_upgrade = shared_str();
        return;
    }

    const bool attached = HudItemData() != nullptr;
    if (m_bStopAtEndAnimIsRunning ||
        (attached && (GetState() != eIdle || GetNextState() != eIdle || IsPending())))
        return;

    if (attached && !g_player_hud->can_attach_controller_item(m_pending_hud_upgrade)) {
        Msg("! Cannot apply double-barrel HUD upgrade [%s]", m_pending_hud_upgrade.c_str());
        m_pending_hud_upgrade = shared_str();
        return;
    }

    if (attached) {
        StopCurrentAnimWithoutCallback();
        g_player_hud->detach_item(this);
    } else {
        m_sounds.StopAllSounds();
    }

    hud_sect = m_pending_hud_upgrade;
    m_pending_hud_upgrade = shared_str();
    m_no_jam_fire = READ_IF_EXISTS(pSettings, r_bool, HudSection(), "no_jam_fire", false);
    LoadHudFeatureFlags();
    LoadHudReloadSounds();

    if (attached) {
        g_player_hud->attach_item(this);
        UpdateHUDAddonsVisibility();
        PlayAnimIdle();
    }
}

bool CWeaponBM16::PrepareReload() {
    m_reload_commit_pending = false;
    m_reload_unjam = IsMisfire();
    m_reload_motion = shared_str();
    m_reload_sound = shared_str();
    m_reload_started_ms = Device.dwTimeGlobal;
    m_reload_visual_at_ms = 0;
    m_reload_commit_at_ms = 0;

    const int loaded = clampr(iAmmoElapsed, 0, 2);
    LPCSTR motion_base = "anm_reload";
    LPCSTR sound_key = "snd_reload";
    if (m_reload_unjam) {
        motion_base = "anm_reload_jammed";
        sound_key = loaded <= 1 ? "snd_reload_jammed_last" : "snd_reload_jammed";
    } else {
        if (!m_pInventory || m_ammoType >= m_ammoTypes.size())
            return false;

        const bool change_requested = IsReloadAmmoChange();
        m_reload_ammo_type = change_requested ? m_set_next_ammoType_on_reload : m_ammoType;
        const int reserve = unlimited_ammo() ? 2 : GetAmmoCount(m_reload_ammo_type);
        if (reserve <= 0)
            return false;

        const auto& ammo_section = m_ammoTypes[m_reload_ammo_type];
        m_reload_replace_rounds = loaded > 0 && std::any_of(m_magazine.begin(), m_magazine.end(),
            [&](const CCartridge& round) { return round.m_ammoSect != ammo_section; });
        const int kept = m_reload_replace_rounds ? 0 : loaded;
        const int capacity = (std::min)(iMagazineSize, 2);
        m_reload_round_count = kept + (std::min)(capacity - kept, reserve);
        if (m_reload_round_count <= kept)
            return false;

        const bool only_one = m_reload_round_count == 1;
        if (loaded == 0) {
            if (only_one) {
                motion_base = change_requested ? "anm_reload_only_ammochange" : "anm_reload_only";
                sound_key = change_requested ? "snd_reload_only_ammochange" : "snd_reload_only";
            } else {
                sound_key = "snd_reload_empty";
            }
        } else if (m_reload_replace_rounds) {
            motion_base = only_one ? "anm_reload_ammochange_only" : "anm_reload_ammochange";
            if (only_one)
                sound_key = loaded == 1 ? "snd_changecartridgetype_one_only" : "snd_changecartridgetype_only";
            else
                sound_key = loaded == 1 ? "snd_changecartridgetype_one" : "snd_changecartridgetype";
        }

        if (m_reload_replace_rounds) {
            // TryReload can select another ammo type when the old reserve is empty.
            // Keep the visible type of the loaded rounds until the insertion mark.
            const auto old_type = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), m_magazine.back().m_ammoSect);
            if (old_type != m_ammoTypes.end())
                m_ammoType = static_cast<u8>(old_type - m_ammoTypes.begin());
            m_set_next_ammoType_on_reload = m_reload_ammo_type;
        }
    }

    string256 key;
    xr_sprintf(key, "%s_%d", motion_base, loaded);
    m_reload_motion = OptionalHudMotion(key, nullptr);
    if (!m_reload_motion.c_str() && m_reload_unjam)
        m_reload_motion = OptionalHudMotion(loaded <= 1 ? "anm_reload_jammed_1" : "anm_reload_jammed_2",
            OptionalHudMotion(ReloadMotion(), nullptr));
    if (!m_reload_motion.c_str()) {
        xr_sprintf(key, "anm_reload_%d", loaded);
        m_reload_motion = OptionalHudMotion(key, OptionalHudMotion("anm_reload", nullptr));
    }
    m_reload_sound = SelectReloadSound(sound_key);
    m_reload_commit_pending = true;
    return true;
}

void CWeaponBM16::switch2_Reload() {
    if (UsesGunslingerDoubleBarrel() && !PrepareReload()) {
        m_set_next_ammoType_on_reload = undefined_ammo_type;
        CWeapon::FireEnd();
        SwitchState(eIdle);
        return;
    }
    inherited::switch2_Reload();
}

void CWeaponBM16::CommitReload() {
    if (!m_reload_commit_pending)
        return;
    m_reload_commit_pending = false;
    m_set_next_ammoType_on_reload = undefined_ammo_type;
    if (m_reload_unjam) {
        bMisfire = false;
        return;
    }

    if (!m_pInventory || m_reload_ammo_type >= m_ammoTypes.size())
        return;
    const auto& ammo_section = m_ammoTypes[m_reload_ammo_type];
    if (!unlimited_ammo() && !m_pInventory->GetAny(ammo_section.c_str()))
        return;

    if (m_reload_replace_rounds)
        UnloadMagazine();
    m_ammoType = m_reload_ammo_type;
    while (iAmmoElapsed < m_reload_round_count) {
        if (!unlimited_ammo() && !m_pInventory->GetAny(ammo_section.c_str()))
            break;
        if (AddCartridge(1) != 0)
            break;
    }
    m_BriefInfo_CalcFrame = 0;
}

void CWeaponBM16::AdjustHudAmmoState(u8& ammo_type, int& count, const shared_str& last_shot) const {
    if (!UsesGunslingerDoubleBarrel() || IsMisfire())
        return;

    const bool reloading = GetState() == eReload && m_reload_commit_pending && !m_reload_unjam;
    if (reloading && m_bStopAtEndAnimIsRunning && m_startedMotionState == eReload &&
        Device.dwTimeGlobal - m_reload_started_ms >= m_reload_visual_at_ms) {
        // Show the incoming rounds while the animation finishes closing the barrels.
        ammo_type = m_reload_ammo_type;
        count = m_reload_round_count;
        return;
    }

    if (!reloading && count != 0)
        return;

    // TryReload can select the reserve type before the spent cases are removed.
    const auto& old_ammo = m_magazine.empty() ? last_shot : m_magazine.back().m_ammoSect;
    const auto old_type = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), old_ammo);
    if (old_type != m_ammoTypes.end())
        ammo_type = static_cast<u8>(old_type - m_ammoTypes.begin());
}

void CWeaponBM16::UpdateCL() {
    if (m_reload_commit_pending && GetState() == eReload && GetNextState() == eReload &&
        m_bStopAtEndAnimIsRunning && m_startedMotionState == eReload &&
        Device.dwTimeGlobal - m_reload_started_ms >= m_reload_commit_at_ms)
        CommitReload();

    inherited::UpdateCL();
    ApplyPendingHudUpgrade();
    if (GetState() == eReload && m_reload_sound.c_str())
        m_sounds.SetPosition(m_reload_sound.c_str(), get_LastFP());
}

void CWeaponBM16::OnStateSwitch(u32 state) {
    if (state != eReload && m_reload_commit_pending) {
        if (!m_reload_unjam && GetState() == eReload &&
            Device.dwTimeGlobal - m_reload_started_ms >= m_reload_commit_at_ms)
            CommitReload();

        m_reload_commit_pending = false;
        m_set_next_ammoType_on_reload = undefined_ammo_type;
    }
    inherited::OnStateSwitch(state);
}

void CWeaponBM16::OnAnimationEnd(u32 state) {
    if (UsesGunslingerDoubleBarrel() && state == eReload) {
        if (GetState() == eReload && GetNextState() == eReload) {
            CommitReload();
            SwitchState(eIdle);
        }
        return;
    }
    inherited::OnAnimationEnd(state);
}

void CWeaponBM16::PlayReloadSound() {
    if (UsesGunslingerDoubleBarrel()) {
        if (m_sounds_enabled && m_reload_sound.c_str())
            PlaySound(m_reload_sound.c_str(), get_LastFP());
        return;
    }
    if (m_magazine.size() == 1 && m_has_legacy_reload_sound)
        PlaySound("sndReload1", get_LastFP());
    else
        PlaySound("sndReload", get_LastFP());
}

void CWeaponBM16::PlayAnimShoot() {
    if (UsesGunslingerDoubleBarrel()) {
        const bool aimed = IsZoomed() && !IsRotatingToZoom();
        shared_str motion;
        if (aimed && UsesScopeAnimations())
            motion = NumberedMotion("anm_shoot_aim_scope");
        if (!motion.c_str() && aimed)
            motion = NumberedMotion("anm_shoot_aim");
        if (!motion.c_str())
            motion = NumberedMotion("anm_shoot");
        if (motion.c_str())
            PlayHUDMotion(motion, MixShootAfterIdle(), this, GetState());
        return;
    }
    switch (m_magazine.size()) {
    case 1:
        PlayHUDMotion("anm_shot_1", FALSE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_shot_2", FALSE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimShow() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimShow();
        return;
    }
    switch (m_magazine.size()) {
    case 0:
        PlayHUDMotion("anm_show_0", TRUE, this, GetState());
        break;
    case 1:
        PlayHUDMotion("anm_show_1", TRUE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_show_2", TRUE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimHide() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimHide();
        return;
    }
    switch (m_magazine.size()) {
    case 0:
        PlayHUDMotion("anm_hide_0", TRUE, this, GetState());
        break;
    case 1:
        PlayHUDMotion("anm_hide_1", TRUE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_hide_2", TRUE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimBore() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimBore();
        return;
    }
    switch (m_magazine.size()) {
    case 0:
        PlayHUDMotion("anm_bore_0", TRUE, this, GetState());
        break;
    case 1:
        PlayHUDMotion("anm_bore_1", TRUE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_bore_2", TRUE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimReload() {
    if (UsesGunslingerDoubleBarrel()) {
        const LPCSTR motion = m_reload_motion.c_str();
        const bool has_motion = motion && (!GetHUDmode() || g_player_hud->has_hud_motion(HudSection(), motion));
        const u32 duration = has_motion ? PlayHUDMotion(motion, TRUE, this, eReload) : 0;
        if (!duration || !m_current_motion_def) {
            StopCurrentAnimWithoutCallback();
            OnAnimationEnd(eReload);
            return;
        }
        m_reload_started_ms = m_dwMotionStartTm;
        m_reload_visual_at_ms = duration;
        m_reload_commit_at_ms = duration;
        if (GetHUDmode() && !m_reload_unjam) {
            string256 key;
            xr_sprintf(key, "lock_time_start_%s", motion);
            const float time = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, float(duration) * 0.001f);
            if (time >= 0.f)
                m_reload_visual_at_ms = static_cast<u32>(clampr(time * 1000.f, 0.f, float(duration)));

            // Keep rounds already inserted when a state change cuts off the closing frames.
            constexpr u32 closing_window_ms = 150;
            const u32 closing_start_ms = duration - (std::min)(duration, closing_window_ms);
            m_reload_commit_at_ms = (std::max)(m_reload_visual_at_ms, closing_start_ms);
        }
        return;
    }
    bool b_both = HaveCartridgeInInventory(2);

    VERIFY(GetState() == eReload);

    if ((m_magazine.size() == 1 || !b_both) &&
        (m_set_next_ammoType_on_reload == undefined_ammo_type ||
         m_ammoType == m_set_next_ammoType_on_reload))
        PlayHUDMotion("anm_reload_1", TRUE, this, GetState());
    else
        PlayHUDMotion("anm_reload_2", TRUE, this, GetState());
}

void CWeaponBM16::PlayAnimIdleMoving() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimIdleMoving();
        return;
    }
    switch (m_magazine.size()) {
    case 0:
        PlayHUDMotion("anm_idle_moving_0", TRUE, this, GetState());
        break;
    case 1:
        PlayHUDMotion("anm_idle_moving_1", TRUE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_idle_moving_2", TRUE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimIdleSprint() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimIdleSprint();
        return;
    }
    switch (m_magazine.size()) {
    case 0:
        PlayHUDMotion("anm_idle_sprint_0", TRUE, this, GetState());
        break;
    case 1:
        PlayHUDMotion("anm_idle_sprint_1", TRUE, this, GetState());
        break;
    case 2:
        PlayHUDMotion("anm_idle_sprint_2", TRUE, this, GetState());
        break;
    }
}

void CWeaponBM16::PlayAnimIdle() {
    if (UsesGunslingerDoubleBarrel()) {
        inherited::PlayAnimIdle();
        return;
    }
    if (TryPlayAnimIdle())
        return;

    if (IsZoomed()) {
        switch (m_magazine.size()) {
        case 0: {
            PlayHUDMotion("anm_idle_aim_0", TRUE, NULL, GetState());
        } break;
        case 1: {
            PlayHUDMotion("anm_idle_aim_1", TRUE, NULL, GetState());
        } break;
        case 2: {
            PlayHUDMotion("anm_idle_aim_2", TRUE, NULL, GetState());
        } break;
        };
    } else {
        switch (m_magazine.size()) {
        case 0: {
            PlayHUDMotion("anm_idle_0", TRUE, NULL, GetState());
        } break;
        case 1: {
            PlayHUDMotion("anm_idle_1", TRUE, NULL, GetState());
        } break;
        case 2: {
            PlayHUDMotion("anm_idle_2", TRUE, NULL, GetState());
        } break;
        };
    }
}
