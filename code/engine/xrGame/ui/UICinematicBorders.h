#pragma once
#include "UIWindow.h"
#include "UIStatic.h"

class CCinematicBorders : public CUIWindow {
public:
    enum AnimType { eFastFade = 0, eVertical = 1, eHorizontal = 2 };
    enum State { eHidden, eAppearing, eVisible, eDisappearing };

private:
    CUIStatic* m_top_bar;
    CUIStatic* m_bottom_bar;
    State      m_state;
    AnimType   m_appear_type;
    AnimType   m_disappear_type;
    u32        m_anim_start_time;
    u32        m_anim_duration;
    float      m_bar_height;

    void UpdateTransforms();

public:
    CCinematicBorders();
    virtual ~CCinematicBorders() = default;

    virtual void Update() override;
    virtual void Draw() override;

    void Show(int appear_type, u32 duration_ms);
    void Hide(int disappear_type, u32 duration_ms);
};