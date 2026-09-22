#pragma once

#include "UIDialogWnd.h"
#include "UIWndCallback.h"

class CUI3tButton;
class CUICheckButton;
class CUIComboBox;
class CUIEditBox;
class CUITextWnd;
class CUITrackBar;
class CWeapon;
struct attachable_hud_item;

class CUIHudEditor : public CUIDialogWnd, public CUIWndCallback {
    typedef CUIDialogWnd inherited;

public:
    struct SProfile {
        Fvector hands_position;
        Fvector hands_orientation;
        Fvector aim_position;
        Fvector aim_rotation;
    };

    CUIHudEditor();
    virtual ~CUIHudEditor();

    void Init();
    virtual void Show(bool status);
    virtual void Update();
    virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData = nullptr);
    virtual bool OnKeyboardAction(int dik, EUIMessages keyboard_action);

    void EnterPreview();
    void CloseEditorSession();
    bool IsSessionActive() const { return m_session_active; }
    bool IsPreviewing() const { return m_previewing; }

private:
    struct SControl {
        u32 value_index;
        CUITrackBar* slider;
        CUIEditBox* edit;
        CUI3tButton* decrease;
        CUI3tButton* increase;
    };

    bool SelectActiveWeapon(bool force);
    void CaptureProfiles();
    void ApplySelectedProfile();
    void SyncControls();
    void SyncControl(SControl& control);
    void RestoreSnapshot();
    void SetForcedAim(bool enabled);
    void FireOnce();
    void ClearLearningHint();
    void UpdateLearningHint();
    void SetStatus(LPCSTR string_id, ...);

    float GetValue(u32 index) const;
    void SetValue(u32 index, float value);

    void OnProfileChanged(CUIWindow*, void*);
    void OnValueChanged(CUIWindow*, void*);
    void OnValueCommitted(CUIWindow*, void*);
    void OnValueStep(CUIWindow*, void*);
    void OnForceAimChanged(CUIWindow*, void*);
    void OnHideCrosshairChanged(CUIWindow*, void*);
    void OnLearningModeChanged(CUIWindow*, void*);
    void OnPreview(CUIWindow*, void*);
    void OnFire(CUIWindow*, void*);
    void OnSave(CUIWindow*, void*);
    void OnRevert(CUIWindow*, void*);
    void OnClose(CUIWindow*, void*);

private:
    CUITextWnd* m_title;
    CUITextWnd* m_weapon_name;
    CUITextWnd* m_hud_section;
    CUITextWnd* m_status;
    CUITextWnd* m_group_headers[4];
    CUIComboBox* m_profile;
    CUICheckButton* m_force_aim;
    CUICheckButton* m_hide_crosshair;
    CUICheckButton* m_learning_mode;
    CUITextWnd* m_learning_mode_label;
    CUI3tButton* m_preview;
    CUI3tButton* m_fire;
    CUI3tButton* m_save;
    CUI3tButton* m_revert;
    CUI3tButton* m_close;
    xr_vector<SControl> m_controls;

    attachable_hud_item* m_hud_item;
    CWeapon* m_weapon;
    shared_str m_section;
    SProfile m_profiles[2];
    SProfile m_snapshot_profiles[2];
    bool m_snapshot_hide_crosshair;
    bool m_was_zoomed;
    bool m_synchronizing;
    bool m_session_active;
    bool m_previewing;
    bool m_close_requested;
    u32 m_fire_release_frame;
    CUIWindow* m_learning_hint_owner;
    u32 m_learning_hint_start;
};

void ToggleHudEditor(bool force_show = false, bool force_hide = false);
void PreviewHudEditor();
void DestroyHudEditor();
bool HudEditorShown();
bool HudEditorCrosshairOverride(const CWeapon* weapon, bool& visible);
