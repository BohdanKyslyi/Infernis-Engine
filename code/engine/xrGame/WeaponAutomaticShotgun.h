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
    void StartTubeReloadPhase();
    void PlayTubeReloadPhase(LPCSTR motion, LPCSTR sound, bool inserts_shell);
    void CommitTubeReloadShell();
    void FinishTubeReloadPhase();
    void ResetTubeReload();

    bool m_add_cartridge_in_open = false;
    bool m_empty_preload_mode = false;
    bool m_keep_chambered_cartridge = true;
    bool m_tube_preloaded = false;
    bool m_tube_insert_pending = false;
    bool m_tube_stop_requested = false;
    u32 m_tube_insert_at_ms = 0;
    u32 m_tube_interrupt_at_ms = 0;
};
