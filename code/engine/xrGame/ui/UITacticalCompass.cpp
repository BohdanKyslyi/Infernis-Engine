#include "stdafx.h"
#include "UITacticalCompass.h"
#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "../Actor.h"
#include "../level.h"
#include "../map_manager.h"
#include "../map_location.h"
#include "../map_location_defs.h"

namespace {
constexpr float TEXTURE_PIXELS_PER_DEGREE = 4096.f / 600.f;
constexpr float VISIBLE_DEGREES = 240.f;
constexpr float STRIP_WIDTH = 360.f;
constexpr float ICON_WIDTH = 18.f;
constexpr float ICON_Y = 8.f;

LPCSTR icon_name(u8 kind) {
    switch (kind) {
    case 0: return "ie_tc_quest_prim";
    case 1: return "ie_tc_stash";
    default: return "ie_tc_transition_blue";
    }
}
} // namespace

bool CUITacticalCompass::Init() {
    string_path found;
    if (!FS.exist(found, "$game_config$", "ui\\", "ui_tactical_compass.xml")) {
        Msg("! Tactical compass: ui_tactical_compass.xml is missing");
        return false;
    }
    CUIXml xml;
    xml.Load(CONFIG_PATH, UI_PATH, "ui_tactical_compass.xml");

    CUIXmlInit init;
    init.InitWindow(xml, "tactical_compass", 0, this);
    init.InitStatic(xml, "tactical_compass:strip", 0, &m_strip);
    AttachChild(&m_strip);
    init.InitStatic(xml, "tactical_compass:frame", 0, &m_frame);
    AttachChild(&m_frame);

    m_max_distance = xml.ReadAttribFlt("tactical_compass", 0, "marker_max_distance", 500.f);
    m_show_markers = xml.ReadAttribInt("tactical_compass", 0, "show_markers", 1) != 0;

    if (m_show_markers) {
        for (u32 i = 0; i < MAX_MARKERS; ++i) {
            CUIStatic* icon = xr_new<CUIStatic>();
            icon->SetAutoDelete(true);
            icon->SetWndSize(Fvector2().set(ICON_WIDTH, ICON_WIDTH));
            icon->SetStretchTexture(true);
            icon->InitTexture(icon_name(mkQuest));
            icon->Show(false);
            AttachChild(icon);
            m_icons[i] = icon;
            m_icon_kinds[i] = mkQuest;
        }
    }
    return true;
}

void CUITacticalCompass::RefreshSpots() {
    m_candidates.clear();
    if (!m_show_markers)
        return;

    for (const auto& key : Level().MapManager().Locations()) {
        CMapLocation* location = key.location;
        if (!location || !location->SpotEnabled())
            continue;

        LPCSTR type = key.spot_type.c_str();
        if (!type)
            continue;
        EMarkerKind kind;
        if (strstr(type, "task_location") || strstr(type, "quest_location"))
            kind = mkQuest;
        else if (strstr(type, "treasure") || strstr(type, "stash"))
            kind = mkStash;
        else if (strstr(type, "level_changer") || strstr(type, "transition"))
            kind = mkTransition;
        else
            continue;

        CObject* object = Level().Objects.net_Find(key.object_id);
        if (!object && location->GetLevelName() != Level().name().c_str())
            continue;

        Marker marker;
        marker.object_id = object ? key.object_id : u16(-1);
        if (object)
            marker.position = object->Position();
        else {
            // Map locations resolve offline quest targets through their server objects.
            location->CalcPosition();
            marker.position = location->GetLastPosition();
            if (marker.position.square_magnitude() < EPS_S)
                continue;
        }
        marker.kind = kind;
        marker.distance_sqr = marker.position.distance_to_sqr(Device.vCameraPosition);
        if (marker.distance_sqr > m_max_distance * m_max_distance)
            continue;

        if (m_candidates.size() < MAX_MARKERS) {
            m_candidates.push_back(marker);
        } else {
            auto farthest = std::max_element(m_candidates.begin(), m_candidates.end(),
                [](const Marker& a, const Marker& b) { return a.distance_sqr < b.distance_sqr; });
            if (marker.distance_sqr < farthest->distance_sqr)
                *farthest = marker;
        }
    }
}

void CUITacticalCompass::UpdateMarkers(float heading) {
    u32 visible = 0;
    for (const Marker& marker : m_candidates) {
        Fvector position = marker.position;
        if (marker.object_id != u16(-1)) {
            CObject* object = Level().Objects.net_Find(marker.object_id);
            if (!object)
                continue;
            position = object->Position();
        }

        Fvector direction;
        direction.sub(position, Device.vCameraPosition);
        if (direction.square_magnitude() > m_max_distance * m_max_distance)
            continue;

        float angle = atan2f(direction.x, direction.z) - heading;
        while (angle > PI) angle -= 2.f * PI;
        while (angle < -PI) angle += 2.f * PI;

        if (angle < -deg2rad(VISIBLE_DEGREES * 0.5f) ||
            angle > deg2rad(VISIBLE_DEGREES * 0.5f))
            continue;

        CUIStatic* icon = m_icons[visible];
        if (m_icon_kinds[visible] != marker.kind) {
            icon->InitTexture(icon_name(marker.kind));
            m_icon_kinds[visible] = marker.kind;
        }

        float x = STRIP_WIDTH * 0.5f + rad2deg(angle) * STRIP_WIDTH / VISIBLE_DEGREES;
        icon->SetWndPos(Fvector2().set(clampr(x - ICON_WIDTH * 0.5f, 0.f, STRIP_WIDTH - ICON_WIDTH), ICON_Y));
        icon->Show(true);
        ++visible;
    }
    for (; visible < MAX_MARKERS; ++visible)
        m_icons[visible]->Show(false);
}

void CUITacticalCompass::Update() {
    CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
    if (!actor || !actor->g_Alive()) {
        Show(false);
        return;
    }
    Show(true);

    const float heading = atan2f(Device.vCameraDirection.x, Device.vCameraDirection.z);
    float degrees = rad2deg(heading);
    if (degrees < 0.f)
        degrees += 360.f;

    float px = degrees * TEXTURE_PIXELS_PER_DEGREE;
    if (fabsf(px - m_last_texture_x) >= 0.5f) {
        Frect rect;
        rect.set(px, 0.f, px + VISIBLE_DEGREES * TEXTURE_PIXELS_PER_DEGREE, 100.f);
        m_strip.SetTextureRect(rect);
        m_last_texture_x = px;
    }

    if (m_show_markers) {
        if (Device.dwTimeGlobal >= m_next_spot_update) {
            m_next_spot_update = Device.dwTimeGlobal + 200;
            RefreshSpots();
        }
        UpdateMarkers(heading);
    }
    inherited::Update();
}
