#pragma once
#include "WeaponMagazined.h"
#include "WeaponShotgun.h"
#include "script_export_space.h"

class CWeaponAutomaticShotgun : public CWeaponMagazined {
    typedef CWeaponMagazined inherited;

public:
    CWeaponAutomaticShotgun();
    virtual ~CWeaponAutomaticShotgun();

    virtual void Load(LPCSTR section);
    void reinit() override;
    BOOL net_Spawn(CSE_Abstract* DC) override;
    void save(NET_Packet& output_packet) override;
    void load(IReader& input_packet) override;

    virtual void net_Export(NET_Packet& P);
    virtual void net_Import(NET_Packet& P);

    virtual void Reload();
    void UpdateCL() override;
    void switch2_StartReload();
    void switch2_AddCartgidge();
    void switch2_EndReload();

    virtual void PlayAnimOpenWeapon();
    virtual void PlayAnimAddOneCartridgeWeapon();
    void PlayAnimCloseWeapon();

    virtual bool Action(u16 cmd, u32 flags);
    virtual int GetCurrentFireMode() { return m_aFireModes[m_iCurFireMode]; };

protected:
    virtual void OnAnimationEnd(u32 state);
    void OnShot() override;
    void PlayAnimShoot() override;
    bool RollForMisfire() override;
    LPCSTR StateMotion(LPCSTR normal, LPCSTR empty, LPCSTR jammed) const override;
    shared_str HudStateVariant(LPCSTR base, LPCSTR fallback) const override;
    shared_str InspectMotion() const override;
    void TriStateReload();
    virtual void OnStateSwitch(u32 S);

    bool HaveCartridgeInInventory(u8 cnt);
    virtual u8 AddCartridge(u8 cnt);

    ESoundTypes m_eSoundOpen;
    ESoundTypes m_eSoundAddCartridge;
    ESoundTypes m_eSoundClose;

    DECLARE_SCRIPT_REGISTER_FUNCTION

private:
    bool UsesGunslingerTubeReload() const;
    bool UsesFirstShotMotion() const;
    shared_str TubeReloadMotion(LPCSTR base, bool preloaded = false, bool final = false) const;
    void LoadTubeReloadSounds();
    void StartTubeReloadPhase();
    void PlayTubeReloadPhase(LPCSTR motion, LPCSTR sound, bool inserts_shell);
    void CommitTubeReloadShell();
    void FinishTubeReloadPhase();
    void ResetTubeReload();

    bool m_add_cartridge_in_open = false;
    bool m_empty_preload_mode = false;
    bool m_keep_chambered_cartridge = true;
    bool m_revolver_reload = false;
    bool m_need_final_close_anims = false;
    bool m_need_first_shoot_anims = false;
    bool m_just_after_reload = false;
    bool m_saved_reload_state = false;
    xr_vector<u8> m_saved_cartridge_types;
    mutable shared_str m_first_state_motion;
    xr_vector<shared_str> m_tube_sound_aliases;
    bool m_tube_preloaded = false;
    bool m_tube_insert_pending = false;
    bool m_tube_stop_requested = false;
    u32 m_tube_insert_at_ms = 0;
    u32 m_tube_interrupt_at_ms = 0;
};
