#pragma once

#include "UIWindow.h"
#include "UIStatic.h"

class CUITacticalCompass : public CUIWindow {
    using inherited = CUIWindow;

    enum EMarkerKind { mkQuest, mkStash, mkTransition };
    struct Marker {
        u16 object_id;
        Fvector position;
        EMarkerKind kind;
        float distance_sqr;
    };

    static constexpr u32 MAX_MARKERS = 16;
    CUIStatic m_strip;
    CUIStatic m_frame;
    CUIStatic* m_icons[MAX_MARKERS] = {};
    EMarkerKind m_icon_kinds[MAX_MARKERS] = {};
    xr_vector<Marker> m_candidates;
    u32 m_next_spot_update = 0;
    float m_last_texture_x = -1.f;
    float m_max_distance = 500.f;
    bool m_show_markers = true;

    void RefreshSpots();
    void UpdateMarkers(float heading);

public:
    bool Init();
    void Update() override;
};
