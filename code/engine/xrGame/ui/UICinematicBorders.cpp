#include "stdafx.h"
#include "UICinematicBorders.h"
#include <algorithm>

CCinematicBorders::CCinematicBorders() : 
    m_state(eHidden), m_anim_start_time(0), m_anim_duration(500), m_bar_height(60.0f) 
{
    SetWndRect(Frect().set(0.0f, 0.0f, 1024.0f, 768.0f));

    m_top_bar = xr_new<CUIStatic>();
    m_top_bar->SetAutoDelete(true);
    AttachChild(m_top_bar);
    m_top_bar->InitTexture("ui\\ui_noise"); 
    m_top_bar->SetTextureColor(0xFF000000);
    m_top_bar->SetStretchTexture(true);

    m_bottom_bar = xr_new<CUIStatic>();
    m_bottom_bar->SetAutoDelete(true);
    AttachChild(m_bottom_bar);
    m_bottom_bar->InitTexture("ui\\ui_noise");
    m_bottom_bar->SetTextureColor(0xFF000000);
    m_bottom_bar->SetStretchTexture(true);
}

void CCinematicBorders::Show(int appear_type, u32 duration_ms) {
    if (m_state == eVisible || m_state == eAppearing) return;
    m_appear_type = static_cast<AnimType>(appear_type);
    m_anim_duration = duration_ms;
    m_anim_start_time = Device.dwTimeGlobal;
    m_state = eAppearing;
    CUIWindow::Show(true);
}

void CCinematicBorders::Hide(int disappear_type, u32 duration_ms) {
    if (m_state == eHidden || m_state == eDisappearing) return;
    m_disappear_type = static_cast<AnimType>(disappear_type);
    m_anim_duration = duration_ms;
    m_anim_start_time = Device.dwTimeGlobal;
    m_state = eDisappearing;
}

void CCinematicBorders::Update() {
    CUIWindow::Update();
    if (m_state == eAppearing || m_state == eDisappearing) UpdateTransforms();
}

void CCinematicBorders::Draw() {
    if (m_state != eHidden) CUIWindow::Draw();
}

void CCinematicBorders::UpdateTransforms() {
    float t = std::clamp(float(Device.dwTimeGlobal - m_anim_start_time) / m_anim_duration, 0.0f, 1.0f);
    //float progress = t * t * (3.0f - 2.0f * t);

    // SmootherStep від Кена Перліна
    float progress = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

    bool is_hide = (m_state == eDisappearing);
    if (is_hide) progress = 1.0f - progress;

    AnimType cur_anim = is_hide ? m_disappear_type : m_appear_type;
    Frect t_rect, b_rect;
    u8 alpha = 255;

	switch (cur_anim) {
        case eFastFade:
            t_rect.set(0.0f, -2.0f, 1024.0f, m_bar_height);
            b_rect.set(0.0f, 768.0f - m_bar_height, 1024.0f, 770.0f);
            alpha = (u8)(255.0f * progress);
            break;

        case eVertical:
            t_rect.set(0.0f, (progress - 1.0f) * m_bar_height - 2.0f, 1024.0f, progress * m_bar_height);
            b_rect.set(0.0f, 768.0f - (progress * m_bar_height), 1024.0f, 770.0f + (1.0f - progress) * m_bar_height);
            break;

        case eHorizontal:
            t_rect.set((progress - 1.0f) * 1024.0f, -2.0f, progress * 1024.0f, m_bar_height);
            b_rect.set((1.0f - progress) * 1024.0f, 768.0f - m_bar_height, (2.0f - progress) * 1024.0f, 770.0f);
            break;
    }

    m_top_bar->SetWndRect(t_rect);
    m_bottom_bar->SetWndRect(b_rect);
    m_top_bar->SetTextureColor(color_rgba(0, 0, 0, alpha));
    m_bottom_bar->SetTextureColor(color_rgba(0, 0, 0, alpha));

    // Перевіряємо завершення анімації по лінійному часу (t), щоб уникнути багів з float
    if (!is_hide && t >= 1.0f) m_state = eVisible;
    if (is_hide && t >= 1.0f) { m_state = eHidden; CUIWindow::Show(false); }
}