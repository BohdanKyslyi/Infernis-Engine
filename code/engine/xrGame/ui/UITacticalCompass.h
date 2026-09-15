#pragma once

#include "UIWindow.h"
#include "UIStatic.h"

class CUITacticalCompass : public CUIWindow {
    using inherited = CUIWindow;

    struct SpotStyle {
        shared_str texture;
        Frect rect;
        float size = 18.f;
        float offset_y = 0.f;
        float max_distance = 0.f;
    };
    struct Marker {
        u16 object_id;
        Fvector position;
        shared_str title;
        u32 style_index;
        bool title_is_key;
        bool active_task;
        float distance_sqr;
        float max_distance;
        float rank;
    };

    static constexpr u32 MAX_MARKERS = 16;
    CUIStatic m_strip;
    CUIStatic m_frame;
    CUITextWnd m_title;
    CUITextWnd m_distance;
    CUIStatic* m_icons[MAX_MARKERS] = {};
    u32 m_icon_styles[MAX_MARKERS] = {};
    xr_vector<SpotStyle> m_styles;
    xr_map<shared_str, u32> m_style_by_type;
    xr_vector<Marker> m_candidates;
    u32 m_next_spot_update = 0;
    float m_last_texture_x = -1.f;
    float m_max_distance = 500.f;
    float m_enemy_max_distance = 120.f;
    float m_focus_angle = 20.f;
    float m_icon_y = -12.f;
    float m_last_ui_kx = -1.f;
    shared_str m_distance_unit;
    shared_str m_last_title;
    bool m_last_title_is_key = false;
    u32 m_last_distance = u32(-1);
    bool m_show_markers = true;
    bool m_has_labels = false;

    void LoadSpotStyles();
    u32 StyleFor(LPCSTR type) const;
    void RefreshSpots();
    void UpdateMarkers(float heading);

public:
    bool Init();
    void Update() override;
};
