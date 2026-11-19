#pragma once

#include "weaponShotgun.h"
#include "script_export_space.h"

class CWeaponBM16 : public CWeaponShotgun {
    typedef CWeaponShotgun inherited;

public:
    virtual ~CWeaponBM16();
    virtual void Load(LPCSTR section);
    void UpdateCL() override;

protected:
    bool install_upgrade_impl(LPCSTR section, bool test) override;
    void switch2_Reload() override;
    void OnAnimationEnd(u32 state) override;
    void OnStateSwitch(u32 state) override;
    void AdjustHudAmmoState(u8& ammo_type, int& count, const shared_str& last_shot) const override;
    LPCSTR ShotSound() const override;
    bool JamPreventsShot() const override;
    bool HasChamberRound() const override;
    LPCSTR StateMotion(LPCSTR normal, LPCSTR empty, LPCSTR jammed) const override;
    shared_str HudStateVariant(LPCSTR base, LPCSTR fallback) const override;
    shared_str SelectDryFireMotion(LPCSTR normal, LPCSTR aimed) const override;
    shared_str InspectMotion() const override;
    virtual void PlayAnimShoot();
    virtual void PlayAnimReload();
    virtual void PlayReloadSound();
    virtual void PlayAnimIdle();
    virtual void PlayAnimIdleMoving();
    virtual void PlayAnimIdleSprint();
    virtual void PlayAnimShow();
    virtual void PlayAnimHide();
    virtual void PlayAnimBore();
    DECLARE_SCRIPT_REGISTER_FUNCTION

private:
    bool UsesGunslingerDoubleBarrel() const;
    bool UsesScopeAnimations() const;
    shared_str NumberedMotion(LPCSTR base) const;
    bool PrepareReload();
    void CommitReload();
    void LoadGunslingerSounds(LPCSTR section);
    void LoadHudReloadSounds();
    bool UpgradeHudAllowed(const shared_str& hud) const override;
    void OnUpgradeHudChanged() override;
    shared_str SelectReloadSound(LPCSTR weapon_key) const;

    bool m_gunslinger_double_barrel = false;
    bool m_no_jam_fire = false;
    bool m_has_one_round_shot_sound = false;
    bool m_has_legacy_reload_sound = false;
    mutable shared_str m_numbered_state_motion;
    xr_vector<shared_str> m_loaded_reload_sound_huds;
    shared_str m_reload_motion;
    shared_str m_reload_sound;
    bool m_reload_commit_pending = false;
    bool m_reload_unjam = false;
    bool m_reload_replace_rounds = false;
    u8 m_reload_ammo_type = 0;
    int m_reload_round_count = 0;
    u32 m_reload_started_ms = 0;
    u32 m_reload_visual_at_ms = 0;
    u32 m_reload_commit_at_ms = 0;
};
add_to_type_list(CWeaponBM16)
#undef script_type_list
#define script_type_list save_type_list(CWeaponBM16)
