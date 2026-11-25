#pragma once

#include "UIOptionsItem.h"
#include "UI_IB_Static.h"

class CUI3tButton;
class CUITrackButton;

class CUITrackBar : public CUI_IB_FrameLineWnd, public CUIOptionsItem {
    friend class CUITrackButton;

public:
    CUITrackBar();
    // CUIOptionsItem
    virtual void SetCurrentOptValue();      // opt->current
    virtual void SaveBackUpOptValue();      // current->backup
    virtual void SaveOptValue();            // current->opt
    virtual void UndoOptValue();            // backup->current
    virtual bool IsChangedOptValue() const; // backup!=current

    virtual void Draw();
    virtual void Update();
    virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);
    virtual void OnMessage(LPCSTR message);
    // CUIWindow
    void InitTrackBar(Fvector2 pos, Fvector2 size);
    virtual void Enable(bool status);
    void SetInvert(bool v) { m_b_invert = v; }
    bool GetInvert() const { return m_b_invert; };
    void SetStep(float step);
    void SetType(bool b_float) { m_b_is_float = b_float; };
    bool GetCheck();
    void SetCheck(bool b);
    int GetIValue() { return m_i_val; }
    float GetFValue() { return m_f_val; }
    // Ported by Ivan Danko, 2026-11-18, from Monolith (author themrdemonized): item 13 "Mods" - a
    // script sets the value of a slider ( the "Mods" window, ui_mods_menu.script )
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-18: пункт 13 «Моди» - скрипт
    // задає значення повзунка ( вікно «Моди», ui_mods_menu.script )
    void SetIValue(int i);
    void SetFValue(float f);
    void SetOptIBounds(int imin, int imax);
    void SetOptFBounds(float fmin, float fmax);

protected:
    void UpdatePos();
    void UpdatePosRelativeToMouse();

    CUI3tButton* m_pSlider;
    bool m_b_invert;
    bool m_b_is_float;
    bool m_b_mouse_capturer;

    union {
        struct {
            float m_f_val;
            float m_f_max;
            float m_f_min;
            float m_f_step;
            float m_f_opt_backup_value;
        };
        struct {
            int m_i_val;
            int m_i_max;
            int m_i_min;
            int m_i_step;
            int m_i_opt_backup_value;
        };
    };
};