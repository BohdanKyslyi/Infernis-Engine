#pragma once
#include "weaponmagazined.h"
#include "rocketlauncher.h"

class CWeaponFakeGrenade;

class CWeaponMagazinedWGrenade : public CWeaponMagazined, public CRocketLauncher {
    typedef CWeaponMagazined inherited;

public:
    CWeaponMagazinedWGrenade(ESoundTypes eSoundType = SOUND_TYPE_WEAPON_SUBMACHINEGUN);
    virtual ~CWeaponMagazinedWGrenade();

    virtual void Load(LPCSTR section);

    virtual BOOL net_Spawn(CSE_Abstract* DC);
    virtual void net_Destroy();
    virtual void net_Export(NET_Packet& P);
    virtual void net_Import(NET_Packet& P);

    virtual void OnH_B_Independent(bool just_before_destroy);

    virtual void save(NET_Packet& output_packet);
    virtual void load(IReader& input_packet);

    virtual bool Attach(PIItem pIItem, bool b_send_event);
    virtual bool Detach(const char* item_section_name, bool b_spawn_item);
    virtual bool CanAttach(PIItem pIItem);
    virtual bool CanDetach(const char* item_section_name);
    virtual void InitAddons();
    bool Is3DScopeEnabled() const override;
    virtual bool UseScopeTexture();
    virtual float CurrentZoomFactor();
    virtual u8 GetCurrentHudOffsetIdx();
    virtual void FireEnd();
    void LaunchGrenade();

    virtual void OnStateSwitch(u32 S);

    virtual void switch2_Reload();
    virtual void state_Fire(float dt);
    virtual void OnShot();
    virtual void OnEvent(NET_Packet& P, u16 type);
    virtual void ReloadMagazine();
    void UnloadMagazine(bool spawn_ammo = true) override;
    bool UpgradeGrenadeMode() const override { return m_bGrenadeMode; }
    BOOL IsMisfire() const override { return !m_bGrenadeMode && inherited::IsMisfire(); }
    bool HasChamberRound() const override { return !m_bGrenadeMode && inherited::HasChamberRound(); }

    virtual bool Action(u16 cmd, u32 flags);

    virtual void UpdateSounds();

    //ïåðåêëþ÷åíèå â ðåæèì ïîäñòâîëüíèêà
    virtual bool SwitchMode();
    void PerformSwitchGL();
    void OnAnimationEnd(u32 state);
    virtual void OnMagazineEmpty();
    virtual bool GetBriefInfo(II_BriefInfo& info);

    virtual bool IsNecessaryItem(const shared_str& item_sect);

    //âèðòóàëüíûå ôóíêöèè äëÿ ïðîèãðûâàíèÿ àíèìàöèè HUD
    virtual void PlayAnimShow();
    virtual void PlayAnimHide();
    virtual void PlayAnimReload();
    virtual void PlayAnimIdle();
    virtual void PlayAnimShoot();
    virtual void PlayAnimModeSwitch();
    virtual void PlayAnimBore();

protected:
    bool UsesScopeZoom() const override;
    shared_str InspectMotion() const override;
    shared_str SelectDryFireMotion(LPCSTR normal, LPCSTR aimed) const override;
    shared_str HudStateVariant(LPCSTR base, LPCSTR fallback) const override;

private:
    LPCSTR RifleHudState() const;
    shared_str FindLauncherMotion(LPCSTR base, LPCSTR variant) const;
    shared_str LauncherReloadMotion() const;
    void FinishGrenadeShot();
    bool m_has_switch_sound = false;
    shared_str m_current_switch_sound;
    virtual void net_Spawn_install_upgrades(Upgrades_type saved_upgrades);
    virtual bool install_upgrade_impl(LPCSTR section, bool test);
    virtual bool install_upgrade_ammo_class(LPCSTR section, bool test);

    int GetAmmoCount2(u8 ammo2_type) const;

public:
    //äîïîëíèòåëüíûå ïàðàìåòðû ïàòðîíîâ
    //äëÿ ïîäñòâîëüíèêà
    //-	CWeaponAmmo*			m_pAmmo2;
    xr_vector<shared_str> m_ammoTypes2;
    u8 m_ammoType2;

    int iMagazineSize2;
    xr_vector<CCartridge> m_magazine2;

    bool m_bGrenadeMode;

    CCartridge m_DefaultCartridge2;
    u8 iAmmoElapsed2;

    virtual void UpdateGrenadeVisibility(bool visibility);
};