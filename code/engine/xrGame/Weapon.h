#pragma once

#include "../xrphysics/PhysicsShell.h"
#include "weaponammo.h"
#include "PHShellCreator.h"

#include "ShootingObject.h"
#include "hud_item_object.h"
#include "Actor_Flags.h"
#include "xrRender/KinematicsAnimated.h"
#include "firedeps.h"
#include "game_cl_single.h"
#include "first_bullet_controller.h"

#include "CameraRecoil.h"

class CActor;
class CEntity;
class ENGINE_API CMotionDef;
class CSE_ALifeItemWeapon;
class CSE_ALifeItemWeaponAmmo;
class CWeaponMagazined;
class CParticlesObject;
class CUIWindow;
class CBinocularsVision;
class CNightVisionEffector;

class CWeapon : public CHudItemObject, public CShootingObject {
private:
    typedef CHudItemObject inherited;

public:
    CWeapon();
    virtual ~CWeapon();

    // Generic
    virtual void Load(LPCSTR section);

    virtual BOOL net_Spawn(CSE_Abstract* DC);
    virtual void net_Destroy();
    virtual void net_Export(NET_Packet& P);
    virtual void net_Import(NET_Packet& P);

    virtual CWeapon* cast_weapon() { return this; }
    virtual CWeaponMagazined* cast_weapon_magazined() { return 0; }

    // serialization
    virtual void save(NET_Packet& output_packet);
    virtual void load(IReader& input_packet);
    virtual BOOL net_SaveRelevant() { return inherited::net_SaveRelevant(); }

    virtual void UpdateCL();
    virtual void shedule_Update(u32 dt);

    virtual void renderable_Render();
    virtual void render_hud_mode();
    virtual bool need_renderable();

    virtual void render_item_ui();
    virtual bool render_item_ui_query();

    virtual void OnH_B_Chield();
    virtual void OnH_A_Chield();
    virtual void OnH_B_Independent(bool just_before_destroy);
    virtual void OnH_A_Independent();
    virtual void on_a_hud_attach() override;
    virtual void OnEvent(NET_Packet& P, u16 type); // {inherited::OnEvent(P,type);}

    virtual void Hit(SHit* pHDS);

    virtual void reinit();
    virtual void reload(LPCSTR section);
    virtual void create_physic_shell();
    void OnChangeVisual() override;
    virtual void activate_physic_shell();
    virtual void setup_physic_shell();

    virtual void SwitchState(u32 S);

    virtual void OnActiveItem();
    virtual void OnHiddenItem();
    virtual void
    SendHiddenItem(); // same as OnHiddenItem but for client... (sends message to a server)...

public:
    virtual bool can_kill() const;
    virtual CInventoryItem* can_kill(CInventory* inventory) const;
    virtual const CInventoryItem* can_kill(const xr_vector<const CGameObject*>& items) const;
    virtual bool ready_to_kill() const;
    virtual bool NeedToDestroyObject() const;
    virtual ALife::_TIME_ID TimePassedAfterIndependant() const;

protected:
    //âðåìÿ óäàëåíèÿ îðóæèÿ
    ALife::_TIME_ID m_dwWeaponRemoveTime;
    ALife::_TIME_ID m_dwWeaponIndependencyTime;

    virtual bool IsHudModeNow();

public:
    void signal_HideComplete();
    virtual bool Action(u16 cmd, u32 flags);

    enum EWeaponStates {
        eFire = eLastBaseState + 1,
        eFire2,
        eReload,
        eMisfire,
        eMagEmpty,
        eSwitch,
        eInspect,
        eTacticalTorch,
        eBayonetStrike,
        eLaserSwitch,
        eFireModeSwitch,
    };
    enum EWeaponSubStates {
        eSubstateReloadBegin = 0,
        eSubstateReloadInProcess,
        eSubstateReloadEnd,
        eSubstateReloadChamber,
    };
    enum { undefined_ammo_type = u8(-1) };

    IC BOOL IsValid() const { return iAmmoElapsed; }
    // Does weapon need's update?
    BOOL IsUpdating();

    virtual BOOL IsMisfire() const;
    BOOL CheckForMisfire();

    BOOL AutoSpawnAmmo() const { return m_bAutoSpawnAmmo; };
    bool IsTriStateReload() const { return m_bTriStateReload; }
    EWeaponSubStates GetReloadState() const { return (EWeaponSubStates)m_sub_state; }

protected:
    bool m_bTriStateReload;
    u8 m_sub_state;
    // a misfire happens, you'll need to rearm weapon
    bool bMisfire;

    BOOL m_bAutoSpawnAmmo;
    virtual bool AllowBore();

public:
    bool IsGrenadeLauncherAttached() const;
    bool IsScopeAttached() const;
    bool TraceHudLaserImpact(Fvector& position, bool& green);
    bool HasActiveHudLaser();
    shared_str EquipmentToggleMotion(bool night_vision, bool switch_on);
    void StopEquipmentToggleMotion(const shared_str& motion);
    virtual bool Is3DScopeEnabled() const;
    float ScopeLensFov() const;
    float ScopeLensBlend() const;
    bool ScopeLensShouldRender() const;
    u8 ScopeLensMode() const;
    void ScopeLensGlass(float* params) const;
    void ScopeLensElectronics(float* params) const;
    bool HasScopeDetector() const;
    shared_str ScopeSettingSection(LPCSTR key) const;
    shared_str AlternativeAimSettingSection(LPCSTR key) const;
    bool IsAlternativeAimAllowed() const;
    bool IsAlternativeAimActive() const { return m_bAlternativeAimActive; }
    float AlternativeHudFovFactor() const;
    bool IsSilencerAttached() const;

    virtual bool GrenadeLauncherAttachable();
    virtual bool ScopeAttachable();
    virtual bool SilencerAttachable();

    ALife::EWeaponAddonStatus get_GrenadeLauncherStatus() const { return m_eGrenadeLauncherStatus; }
    ALife::EWeaponAddonStatus get_ScopeStatus() const { return m_eScopeStatus; }
    ALife::EWeaponAddonStatus get_SilencerStatus() const { return m_eSilencerStatus; }

    virtual bool UseScopeTexture();

    //îáíîâëåíèå âèäèìîñòè äëÿ êîñòî÷åê àääîíîâ
    void UpdateAddonsVisibility();
    void UpdateHUDAddonsVisibility();
    void ApplyConfiguredBoneVisibility(IKinematics* model);
    //èíèöèàëèçàöèÿ ñâîéñòâ ïðèñîåäèíåííûõ àääîíîâ
    virtual void InitAddons();

    //äëÿ îòîáðîàæåíèÿ èêîíîê àïãðåéäîâ â èíòåðôåéñå
    int GetScopeX() { return pSettings->r_s32(m_scopes[m_cur_scope], "scope_x"); }
    int GetScopeY() { return pSettings->r_s32(m_scopes[m_cur_scope], "scope_y"); }
    int GetSilencerX() { return m_iSilencerX; }
    int GetSilencerY() { return m_iSilencerY; }
    int GetGrenadeLauncherX() { return m_iGrenadeLauncherX; }
    int GetGrenadeLauncherY() { return m_iGrenadeLauncherY; }

    const shared_str& GetGrenadeLauncherName() const { return m_sGrenadeLauncherName; }
    const shared_str GetScopeName() const {
        return pSettings->r_string(m_scopes[m_cur_scope], "scope_name");
    }
    const shared_str& GetSilencerName() const { return m_sSilencerName; }

    IC void ForceUpdateAmmo() { m_BriefInfo_CalcFrame = 0; }

    u8 GetAddonsState() const { return m_flagsAddOnState; };
    void SetAddonsState(u8 st) { m_flagsAddOnState = st; } // dont use!!! for buy menu only!!!
protected:
    //ñîñòîÿíèå ïîäêëþ÷åííûõ àääîíîâ
    u8 m_flagsAddOnState;

    //âîçìîæíîñòü ïîäêëþ÷åíèÿ ðàçëè÷íûõ àääîíîâ
    ALife::EWeaponAddonStatus m_eScopeStatus;
    ALife::EWeaponAddonStatus m_eSilencerStatus;
    ALife::EWeaponAddonStatus m_eGrenadeLauncherStatus;
    struct UpgradeBoneMask {
        IKinematics* model = nullptr;
        xr_vector<u16> hidden;
        void Restore(IKinematics* current);
        void Hide(IKinematics* current, LPCSTR section, LPCSTR key);
    };
    UpgradeBoneMask m_hud_upgrade_bones;
    UpgradeBoneMask m_world_upgrade_bones;
    xr_vector<shared_str> m_bone_upgrade_sections;
    xr_vector<shared_str> m_upgrade_bone_defaults;
    void LoadUpgradeBoneDefaults(LPCSTR section);
    xr_vector<shared_str> m_icon_upgrade_sections;
    float m_actor_camera_speed_factor = 1.f;
    xr_vector<shared_str> m_hud_upgrade_sections;
    xr_vector<shared_str> m_scope_upgrade_sections;
    shared_str m_base_upgrade_hud;
    shared_str m_pending_upgrade_hud;
    shared_str m_upgrade_world_visual;
    LPCSTR visual_name(CSE_Abstract* server_entity) override;
    bool SetUpgradeWorldVisual(LPCSTR visual);
    bool ApplyUpgradeWorldVisual();

    struct TacticalTorch {
        shared_str section;
        shared_str bone;
        shared_str direction_bone;
        Fvector hud_offset{};
        Fvector world_offset{};
        Fvector omni_hud_offset{};
        Fvector omni_world_offset{};
        Fvector breaking{};
        ref_light spot;
        ref_light omni;
        ref_glow glow;
        bool enabled = false;
        bool active = false;
        bool damaged = false;
        bool switch_pending = false;
        bool has_switch_sound[2]{};
        u32 action_started = 0;
        u32 switch_delay = 0;
        u32 action_duration = 0;
        u32 damage_checked_at = 0;
    } m_tactical_torch;
    void LoadTacticalTorch(LPCSTR section);
    void CreateTacticalTorchLights();
    void StartTacticalTorchAction();
    void UpdateTacticalTorch();
    void SetTacticalTorchEffects(bool active);

    struct LaserDot {
        shared_str effect;
        float distance;
    };
    struct WeaponLaser {
        shared_str section;
        shared_str bone;
        xr_vector<LaserDot> dots;
        Fvector hud_offset{};
        Fvector world_offset{};
        Fvector breaking{};
        bool draw_hud_impact = false;
        bool green = false;
        CParticlesObject* dot = nullptr;
        shared_str current_effect;
        bool checked_effects = false;
        bool enabled = false;
        bool beam_visible = false;
        bool damaged = false;
        bool switch_pending = false;
        bool has_switch_sound[2]{};
        u32 action_started = 0;
        u32 switch_delay = 0;
        u32 action_duration = 0;
        u32 damage_checked_at = 0;
    } m_weapon_laser;
    void LoadWeaponLaser(LPCSTR section);
    void StartWeaponLaserAction();
    void UpdateWeaponLaser();
    void SetLaserBeamVisible(bool visible);
    void StopLaserDot();
    void StopWeaponLaserEffects();
    bool AllowsWeaponLaserEffects() const;

    shared_str m_bayonet_section;
    bool m_bayonet_has_sound = false;
    bool m_bayonet_hit_pending = false;
    u32 m_bayonet_start_time = 0;
    u32 m_bayonet_hit_time = 0;
    u32 m_bayonet_duration = 0;

    void LoadBayonet(LPCSTR section);
    LPCSTR BayonetSettingSection(LPCSTR key) const;
    void StartBayonetStrike();
    void UpdateBayonetStrike();
    void PerformBayonetHit();
    bool HasWeaponActionMotion(const shared_str& motion);
    void RefreshUpgradeHud();
    void ApplyPendingUpgradeHud();

    //íàçâàíèÿ ñåêöèé ïîäêëþ÷àåìûõ àääîíîâ
    shared_str m_sScopeName;
    shared_str m_sSilencerName;
    shared_str m_sGrenadeLauncherName;

    //ñìåùåíèå èêîíîâ àïãðåéäîâ â èíâåíòàðå
    int m_iScopeX, m_iScopeY;
    int m_iSilencerX, m_iSilencerY;
    int m_iGrenadeLauncherX, m_iGrenadeLauncherY;

protected:
    struct SZoomParams {
        bool m_bZoomEnabled; //ðàçðåøåíèå ðåæèìà ïðèáëèæåíèÿ
        bool m_bHideCrosshairInZoom;
        //		bool			m_bZoomDofEnabled;

        bool m_bIsZoomModeNow;      //êîãäà ðåæèì ïðèáëèæåíèÿ âêëþ÷åí
        float m_fCurrentZoomFactor; //òåêóùèé ôàêòîð ïðèáëèæåíèÿ
        float m_fZoomRotateTime;    //âðåìÿ ïðèáëèæåíèÿ

        float m_fIronSightZoomFactor; //êîýôôèöèåíò óâåëè÷åíèÿ ïðèöåëèâàíèÿ
        float m_fScopeZoomFactor;     //êîýôôèöèåíò óâåëè÷åíèÿ ïðèöåëà

        float m_fZoomRotationFactor;

        //		Fvector			m_ZoomDof;
        //		Fvector4		m_ReloadDof;
        BOOL m_bUseDynamicZoom;
        shared_str m_sUseZoomPostprocess;
        shared_str m_sUseBinocularVision;
        CBinocularsVision* m_pVision;
        CNightVisionEffector* m_pNight_vision;

    } m_zoom_params;

    bool m_bAlternativeAimActive;
    bool m_bAlternativeAimOwnsZoom;
    float m_fAlternativeAimFactor;
    float m_fZoomFactorBeforeAlternativeAim;

    bool m_gunslinger_zoom = false;
    bool UsesGunslingerZoom() const;
    virtual bool UsesScopeZoom() const;
    void ChangeZoomStep(bool increase);
    bool IsScopeCollimator() const;
    bool UsesGunslingerScopeLens() const;
    bool GunslingerScopeLensRange(float& minimum, float& maximum) const;
    shared_str m_gunslinger_lens_scope;
    float m_gunslinger_lens_position = 0.f;
    struct ScopeBrightness {
        float minimum = 1.f / 3.f;
        float maximum = 1.f / 3.f;
        float noise = 1.f;
        int steps = 0;
        int step = 0;
        bool configured = false;
    } m_scope_brightness;
    bool m_scope_brightness_sound[2]{};
    void LoadScopeBrightness();
    void ChangeScopeBrightness(bool increase);

    float m_fRTZoomFactor; // run-time zoom factor
    CUIWindow* m_UIScope;

public:
    IC bool IsZoomEnabled() const { return m_zoom_params.m_bZoomEnabled; }
    virtual void ZoomInc();
    virtual void ZoomDec();
    virtual void OnZoomIn();
    virtual void OnZoomOut();
    IC bool IsZoomed() const { return m_zoom_params.m_bIsZoomModeNow; };
    CUIWindow* ZoomTexture();

    bool ZoomHideCrosshair() { return m_zoom_params.m_bHideCrosshairInZoom || ZoomTexture(); }

    IC float GetZoomFactor() const { return m_zoom_params.m_fCurrentZoomFactor; }
    IC void SetZoomFactor(float f) { m_zoom_params.m_fCurrentZoomFactor = f; }

    virtual float CurrentZoomFactor();
    float ZoomCameraFov(float base_fov) const;
    //ïîêàçûâàåò, ÷òî îðóæèå íàõîäèòñÿ â ñîîñòîÿíèè ïîâîðîòà äëÿ ïðèáëèæåííîãî ïðèöåëèâàíèÿ
    bool IsRotatingToZoom() const { return (m_zoom_params.m_fZoomRotationFactor < 1.f); }
    float GetZoomRotationFactor() const { return m_zoom_params.m_fZoomRotationFactor; }

    virtual u8 GetCurrentHudOffsetIdx();

    virtual float Weight() const;
    virtual u32 Cost() const;

public:
    virtual EHandDependence HandDependence() const { return eHandDependence; }
    bool IsSingleHanded() const { return m_bIsSingleHanded; }

public:
    IC LPCSTR strap_bone0() const { return m_strap_bone0; }
    IC LPCSTR strap_bone1() const { return m_strap_bone1; }
    IC void strapped_mode(bool value) { m_strapped_mode = value; }
    IC bool strapped_mode() const { return m_strapped_mode; }

protected:
    LPCSTR m_strap_bone0;
    LPCSTR m_strap_bone1;
    Fmatrix m_StrapOffset;
    bool m_strapped_mode;
    bool m_can_be_strapped;

    Fmatrix m_Offset;
    bool m_use_world_anims = false;
    shared_str m_world_animation;
    // 0-èñïîëüçóåòñÿ áåç ó÷àñòèÿ ðóê, 1-îäíà ðóêà, 2-äâå ðóêè
    EHandDependence eHandDependence;
    bool m_bIsSingleHanded;

public:
    //çàãðóæàåìûå ïàðàìåòðû
    Fvector vLoadedFirePoint;
    Fvector vLoadedFirePoint2;

private:
    firedeps m_current_firedeps;

    void UpdateGunslingerHudMovement(Fmatrix& trans, const CActor& actor);
    bool m_gunslinger_hud_movement = false;
    Fvector m_hud_movement_pos{};
    Fvector m_hud_movement_rot{};
    int m_hud_movement_stance = -1;
    u32 m_hud_movement_frame = u32(-1);
    LPCSTR m_hud_stance_transition = nullptr;
    float m_hud_stance_time = 0.f;
    Fvector m_hud_lookout_pos{};
    Fvector m_hud_lookout_rot{};
    u32 m_hud_lookout = 0;
    LPCSTR m_hud_lookout_transition = nullptr;
    float m_hud_lookout_time = 0.f;

    bool m_advanced_ammo_bones = false;
    shared_str m_last_shot_ammo_section;
    void UpdateHudAmmoBones(const CCartridge* fired = nullptr);

protected:
    void LoadHudFeatureFlags();
    void UpdateWorldAnimation(u32 state, bool shot = false);
    void UpdateWorldFireModeBones();
    virtual void OnUpgradeHudChanged() {}
    virtual shared_str WeaponActionMotion(LPCSTR base) const;
    virtual bool UpgradeGrenadeMode() const { return false; }

public:
    bool IsHudActionLocked() const;
    bool TryBayonetStrike();

protected:
    virtual int FireModeForHudBones() const { return 1; }
    virtual bool UpgradeHudAllowed(const shared_str&) const { return true; }
    virtual void AdjustHudAmmoState(u8&, int&, const shared_str&) const {}
    void on_b_hud_detach() override;
    virtual void UpdateFireDependencies_internal();
    virtual void UpdatePosition(const Fmatrix& transform); //.
    virtual void UpdateXForm();
    virtual void UpdateHudAdditonal(Fmatrix&);
    IC void UpdateFireDependencies() {
        if (dwFP_Frame == Device.dwFrame)
            return;
        UpdateFireDependencies_internal();
    };

    virtual void LoadFireParams(LPCSTR section);

public:
    IC const Fvector& get_LastFP() {
        UpdateFireDependencies();
        return m_current_firedeps.vLastFP;
    }
    IC const Fvector& get_LastFP2() {
        UpdateFireDependencies();
        return m_current_firedeps.vLastFP2;
    }
    IC const Fvector& get_LastFD() {
        UpdateFireDependencies();
        return m_current_firedeps.vLastFD;
    }
    IC const Fvector& get_LastSP() {
        UpdateFireDependencies();
        return m_current_firedeps.vLastSP;
    }

    virtual const Fvector& get_CurrentFirePoint() { return get_LastFP(); }
    virtual const Fvector& get_CurrentFirePoint2() { return get_LastFP2(); }
    virtual const Fmatrix& get_ParticlesXFORM() {
        UpdateFireDependencies();
        return m_current_firedeps.m_FireParticlesXForm;
    }
    virtual void ForceUpdateFireParticles();
    virtual void debug_draw_firedeps();

protected:
    virtual void SetDefaults();

    virtual bool MovingAnimAllowedNow();
    virtual void OnStateSwitch(u32 S);
    virtual void OnAnimationEnd(u32 state);
    virtual shared_str InspectMotion() const;
    virtual bool RollForMisfire();

    //òðàññèðîâàíèå ïîëåòà ïóëè
    virtual void FireTrace(const Fvector& P, const Fvector& D);
    virtual float GetWeaponDeterioration();

    virtual void FireStart() { CShootingObject::FireStart(); }
    virtual void FireEnd();

    virtual void Reload();
    void StopShooting();

    // îáðàáîòêà âèçóàëèçàöèè âûñòðåëà
    virtual void OnShot(){};
    virtual void AddShotEffector();
    virtual void RemoveShotEffector();
    virtual void ClearShotEffector();
    virtual void StopShotEffector();

public:
    float GetBaseDispersion(float cartridge_k);
    float GetFireDispersion(bool with_cartridge, bool for_crosshair = false);
    virtual float GetFireDispersion(float cartridge_k, bool for_crosshair = false);
    virtual int ShotsFired() { return 0; }
    virtual int GetCurrentFireMode() { return 1; }

    //ïàðàìåòû îðóæèÿ â çàâèñèìîòè îò åãî ñîñòîÿíèÿ èñïðàâíîñòè
    float GetConditionDispersionFactor() const;
    float GetConditionMisfireProbability() const;
    virtual float GetConditionToShow() const;

public:
    CameraRecoil cam_recoil;      // simple mode (walk, run)
    CameraRecoil zoom_cam_recoil; // using zoom =(ironsight or scope)

protected:
    //ôàêòîð óâåëè÷åíèÿ äèñïåðñèè ïðè ìàêñèìàëüíîé èçíîøåíîñòè
    //(íà ñêîëüêî ïðîöåíòîâ óâåëè÷èòñÿ äèñïåðñèÿ)
    float fireDispersionConditionFactor;
    //âåðîÿòíîñòü îñå÷êè ïðè ìàêñèìàëüíîé èçíîøåíîñòè

    // modified by Peacemaker [17.10.08]
    //	float					misfireProbability;
    //	float					misfireConditionK;
    float misfireStartCondition;   //èçíîøåííîñòü, ïðè êîòîðîé ïîÿâëÿåòñÿ øàíñ îñå÷êè
    float misfireEndCondition;     //èçíîøåíîñòü ïðè êîòîðîé øàíñ îñå÷êè ñòàíîâèòñÿ êîíñòàíòíûì
    float misfireStartProbability; //øàíñ îñå÷êè ïðè èçíîøåíîñòè áîëüøå ÷åì misfireStartCondition
    float misfireEndProbability;   //øàíñ îñå÷êè ïðè èçíîøåíîñòè áîëüøå ÷åì misfireEndCondition
    float conditionDecreasePerQueueShot; //óâåëè÷åíèå èçíîøåíîñòè ïðè âûñòðåëå î÷åðåäüþ
    float conditionDecreasePerShot;      //óâåëè÷åíèå èçíîøåíîñòè ïðè îäèíî÷íîì âûñòðåëå

public:
    float GetMisfireStartCondition() const { return misfireStartCondition; };
    float GetMisfireEndCondition() const { return misfireEndCondition; };

protected:
    struct SPDM {
        float m_fPDM_disp_base;
        float m_fPDM_disp_vel_factor;
        float m_fPDM_disp_accel_factor;
        float m_fPDM_disp_crouch;
        float m_fPDM_disp_crouch_no_acc;
    };
    SPDM m_pdm;

    float m_crosshair_inertion;
    first_bullet_controller m_first_bullet_controller;

protected:
    //äëÿ îòäà÷è îðóæèÿ
    Fvector m_vRecoilDeltaAngle;

    //äëÿ ñòàëêåðîâ, ÷òîá îíè çíàëè ýôôåêòèâíûå ãðàíèöû èñïîëüçîâàíèÿ
    //îðóæèÿ
    float m_fMinRadius;
    float m_fMaxRadius;

protected:
    //äëÿ âòîðîãî ñòâîëà
    void StartFlameParticles2();
    void StopFlameParticles2();
    void UpdateFlameParticles2();

protected:
    shared_str m_sFlameParticles2;
    //îáúåêò ïàðòèêëîâ äëÿ ñòðåëüáû èç 2-ãî ñòâîëà
    CParticlesObject* m_pFlameParticles2;

protected:
    int GetAmmoCount_forType(shared_str const& ammo_type) const;
    int GetAmmoCount(u8 ammo_type) const;

public:
    IC int GetAmmoElapsed() const { return /*int(m_magazine.size())*/ iAmmoElapsed; }
    virtual int GetAmmoMagSize() const { return iMagazineSize; }
    int GetSuitableAmmoTotal(bool use_item_to_spawn = false) const;

    void SetAmmoElapsed(int ammo_count);

    virtual void OnMagazineEmpty();
    void SpawnAmmo(u32 boxCurr = 0xffffffff, LPCSTR ammoSect = NULL, u32 ParentID = 0xffffffff);
    bool SwitchAmmoType(u32 flags);

    virtual float Get_PDM_Base() const { return m_pdm.m_fPDM_disp_base; };
    virtual float Get_PDM_Vel_F() const { return m_pdm.m_fPDM_disp_vel_factor; };
    virtual float Get_PDM_Accel_F() const { return m_pdm.m_fPDM_disp_accel_factor; };
    virtual float Get_PDM_Crouch() const { return m_pdm.m_fPDM_disp_crouch; };
    virtual float Get_PDM_Crouch_NA() const { return m_pdm.m_fPDM_disp_crouch_no_acc; };
    Irect GetInvGridRect() const override;
    float GetControlInertionFactor() const override;
    const xr_vector<shared_str>& UpgradeIconSections() const { return m_icon_upgrade_sections; }
    virtual float GetCrosshairInertion() const { return m_crosshair_inertion; };
    float GetFirstBulletDisp() const { return m_first_bullet_controller.get_fire_dispertion(); };

protected:
    int iAmmoElapsed;  // ammo in magazine, currently
    int iMagazineSize; // size (in bullets) of magazine

    //äëÿ ïîäñ÷åòà â GetSuitableAmmoTotal
    mutable int m_iAmmoCurrentTotal;
    mutable u32 m_BriefInfo_CalcFrame; //êàäð íà êîòîðîì ïðîñ÷èòàëè êîë-âî ïàòðîíîâ
    bool m_bAmmoWasSpawned;

    virtual bool IsNecessaryItem(const shared_str& item_sect);

public:
    xr_vector<shared_str> m_ammoTypes;
    /*
            struct SScopes
            {
                    shared_str			m_sScopeName;
                    int					m_iScopeX;
                    int					m_iScopeY;
            };
            DEFINE_VECTOR(SScopes*, SCOPES_VECTOR, SCOPES_VECTOR_IT);
            SCOPES_VECTOR			m_scopes;

            u8						cur_scope;
    */

    using SCOPES_VECTOR = xr_vector<shared_str>;
    SCOPES_VECTOR m_scopes;
    u8 m_cur_scope;

    CWeaponAmmo* m_pCurrentAmmo;
    u8 m_ammoType;
    //-	shared_str				m_ammoName; <== deleted
    bool m_bHasTracers;
    u8 m_u8TracerColorID;
    u8 m_set_next_ammoType_on_reload;
    // Multitype ammo support
    xr_vector<CCartridge> m_magazine;
    CCartridge m_DefaultCartridge;
    float m_fCurrentCartirdgeDisp;

    bool unlimited_ammo();
    IC bool can_be_strapped() const { return m_can_be_strapped; };

protected:
    u32 m_ef_main_weapon_type;
    u32 m_ef_weapon_type;

public:
    virtual u32 ef_main_weapon_type() const;
    virtual u32 ef_weapon_type() const;

protected:
    // This is because when scope is attached we can't ask scope for these params
    // therefore we should hold them by ourself :-((
    float m_addon_holder_range_modifier;
    float m_addon_holder_fov_modifier;

public:
    virtual void modify_holder_params(float& range, float& fov) const;
    virtual bool use_crosshair() const { return true; }
    bool show_crosshair();
    bool show_indicators();
    virtual BOOL ParentMayHaveAimBullet();
    virtual BOOL ParentIsActor();

private:
    virtual bool install_upgrade_ammo_class(LPCSTR section, bool test);
    bool install_upgrade_disp(LPCSTR section, bool test);
    bool install_upgrade_hit(LPCSTR section, bool test);
    bool install_upgrade_addon(LPCSTR section, bool test);

protected:
    virtual bool install_upgrade_impl(LPCSTR section, bool test);

private:
    float m_hit_probability[egdCount];

public:
    const float& hit_probability() const;

private:
    Fvector m_overriden_activation_speed;
    bool m_activation_speed_is_overriden;
    virtual bool ActivationSpeedOverriden(Fvector& dest, bool clear_override);

    bool m_bRememberActorNVisnStatus;

public:
    virtual void SetActivationSpeedOverride(Fvector const& speed);
    bool GetRememberActorNVisnStatus() { return m_bRememberActorNVisnStatus; };
    virtual void EnableActorNVisnAfterZoom();

    virtual shared_str const GetAnticheatSectionName() const { return cNameSect(); };
};
