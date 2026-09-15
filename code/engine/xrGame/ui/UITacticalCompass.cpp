#include "stdafx.h"
#include "UITacticalCompass.h"
#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "../Actor.h"
#include "../level.h"
#include "../map_manager.h"
#include "../map_location.h"
#include "../map_location_defs.h"
#include "../GametaskManager.h"
#include "../GameTask.h"
#include "../relation_registry.h"
#include "../actor_memory.h"
#include "../visual_memory_manager.h"
#include "../ActorHelmet.h"
#include "../Inventory.h"

namespace {
constexpr float TEXTURE_PIXELS_PER_DEGREE = 4096.f / 600.f;
constexpr float VISIBLE_DEGREES = 240.f;
constexpr float STRIP_WIDTH = 360.f;
constexpr LPCSTR MARKER_ATLAS = "ui\\tactic_compass\\markers";

bool relation_spot(LPCSTR type) {
    return !xr_strcmp(type, "friend_location") || !xr_strcmp(type, "neutral_location") ||
           !xr_strcmp(type, "enemy_location");
}
} // namespace

void CUITacticalCompass::LoadSpotStyles() {
    SpotStyle fallback;
    fallback.texture = MARKER_ATLAS;
    fallback.rect.set(210.f, 0.f, 240.f, 30.f);
    m_styles.push_back(fallback);

    string_path found;
    if (!FS.exist(found, "$game_config$", "ui\\", "ui_tactical_compass_spots.xml")) {
        Msg("! Tactical compass: no spot styles XML, using generic markers");
        return;
    }

    CUIXml xml;
    xml.Load(CONFIG_PATH, UI_PATH, "ui_tactical_compass_spots.xml");
    const int count = xml.GetNodesNum("", 0, "spot");
    for (int i = 0; i < count; ++i) {
        LPCSTR type = xml.ReadAttrib("spot", i, "type", "");
        if (!type || !type[0])
            continue;

        SpotStyle style;
        style.texture = xml.ReadAttrib("spot", i, "texture", MARKER_ATLAS);
        float x = xml.ReadAttribFlt("spot", i, "x", 210.f);
        float y = xml.ReadAttribFlt("spot", i, "y", 0.f);
        float w = xml.ReadAttribFlt("spot", i, "width", 30.f);
        float h = xml.ReadAttribFlt("spot", i, "height", 30.f);
        style.size = xml.ReadAttribFlt("spot", i, "size", 18.f);
        style.offset_y = xml.ReadAttribFlt("spot", i, "offset_y", 0.f);
        style.max_distance = xml.ReadAttribFlt("spot", i, "max_distance", 0.f);
        if (w <= 0.f || h <= 0.f || style.size <= 0.f || !style.texture.size()) {
            Msg("! Tactical compass: invalid spot style '%s'", type);
            continue;
        }
        style.rect.set(x, y, x + w, y + h);

        if (!xr_strcmp(type, "*")) {
            m_styles[0] = style;
        } else {
            const u32 index = static_cast<u32>(m_styles.size());
            m_styles.push_back(style);
            m_style_by_type[type] = index;
        }
    }
}

u32 CUITacticalCompass::StyleFor(LPCSTR type) const {
    const auto it = m_style_by_type.find(type);
    return it == m_style_by_type.end() ? 0 : it->second;
}

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
    m_has_labels = xml.NavigateToNode("tactical_compass:title", 0) &&
                   xml.NavigateToNode("tactical_compass:distance", 0);
    if (m_has_labels) {
        init.InitTextWnd(xml, "tactical_compass:title", 0, &m_title);
        AttachChild(&m_title);
        m_title.Show(false);
        init.InitTextWnd(xml, "tactical_compass:distance", 0, &m_distance);
        AttachChild(&m_distance);
        m_distance.Show(false);
    } else {
        Msg("! Tactical compass: update ui_tactical_compass.xml for quest labels");
    }

    m_max_distance = xml.ReadAttribFlt("tactical_compass", 0, "marker_max_distance", 500.f);
    m_enemy_max_distance = xml.ReadAttribFlt("tactical_compass", 0, "enemy_max_distance", 120.f);
    m_focus_angle = xml.ReadAttribFlt("tactical_compass", 0, "focus_angle", 20.f);
    m_icon_y = xml.ReadAttribFlt("tactical_compass", 0, "marker_y", -12.f);
    m_distance_unit = xml.ReadAttrib("tactical_compass", 0, "distance_unit", "m");
    m_show_markers = xml.ReadAttribInt("tactical_compass", 0, "show_markers", 1) != 0;
    LoadSpotStyles();

    if (m_show_markers) {
        for (u32 i = 0; i < MAX_MARKERS; ++i) {
            CUIStatic* icon = xr_new<CUIStatic>();
            icon->SetAutoDelete(true);
            icon->SetWndSize(Fvector2().set(m_styles[0].size * UI().get_current_kx(), m_styles[0].size));
            icon->SetStretchTexture(true);
            icon->InitTexture(m_styles[0].texture.c_str());
            icon->SetTextureRect(m_styles[0].rect);
            icon->Show(false);
            AttachChild(icon);
            m_icons[i] = icon;
            m_icon_styles[i] = 0;
        }
    }
    return true;
}

void CUITacticalCompass::RefreshSpots() {
    m_candidates.clear();
    if (!m_show_markers)
        return;

    CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
    if (!actor)
        return;
    CGameTask* active_task = Level().GameTaskManager().ActiveTask();

    for (const auto& key : Level().MapManager().Locations()) {
        CMapLocation* location = key.location;
        if (!location || !location->SpotEnabled() || key.object_id == actor->ID() ||
            (!location->LevelMapSpot() && !location->MiniMapSpot() && !location->complex_spot()))
            continue;

        LPCSTR type = key.spot_type.c_str();
        if (!type)
            continue;
        CObject* object = Level().Objects.net_Find(key.object_id);

        if (relation_spot(type)) {
            CInventoryOwner* owner = object ? smart_cast<CInventoryOwner*>(object) : nullptr;
            if (!owner)
                continue;
            CInventoryOwner* actor_owner = actor;
            ALife::ERelationType relation = RELATION_REGISTRY().GetRelationType(owner, actor_owner);
            type = RELATION_REGISTRY().GetSpotName(relation).c_str();
            if (relation == ALife::eRelationTypeEnemy ||
                relation == ALife::eRelationTypeWorstEnemy) {
                CEntityAlive* alive = smart_cast<CEntityAlive*>(object);
                const CGameObject* game_object = smart_cast<const CGameObject*>(object);
                if (!alive || !alive->g_Alive() || !game_object)
                    continue;
                CHelmet* helmet = smart_cast<CHelmet*>(actor->inventory().ItemFromSlot(HELMET_SLOT));
                bool detected = helmet && helmet->m_fShowNearestEnemiesDistance > 0.f &&
                    actor->Position().distance_to(object->Position()) <
                        helmet->m_fShowNearestEnemiesDistance;
                if (!detected && !actor->memory().visual().visible_now(game_object))
                    continue;
            }
        }

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

        CGameTask* task = location->m_owner_task_id.size()
                              ? Level().GameTaskManager().HasGameTask(location, true)
                              : nullptr;
        marker.active_task = task && task == active_task;
        marker.title_is_key = !!task;
        if (task)
            marker.title = task->m_Title;
        else {
            LPCSTR hint = location->GetHint();
            marker.title = hint && xr_strcmp(hint, "no hint") ? hint : "";
        }

        marker.style_index = StyleFor(type);
        if (task) {
            marker.style_index = StyleFor(task->GetTaskType() == eTaskTypeAdditional
                                              ? "@quest_additional" : "@quest_storyline");
        } else if (!xr_strcmp(type, "enemy_location")) {
            marker.style_index = StyleFor("@enemy");
        }

        const SpotStyle& style = m_styles[marker.style_index];
        marker.max_distance = style.max_distance > 0.f ? style.max_distance :
            (!xr_strcmp(type, "enemy_location") ? m_enemy_max_distance : m_max_distance);
        marker.distance_sqr = marker.position.distance_to_sqr(Device.vCameraPosition);
        if (marker.distance_sqr > marker.max_distance * marker.max_distance)
            continue;
        marker.rank = marker.active_task ? -1.f : marker.distance_sqr;

        if (m_candidates.size() < MAX_MARKERS) {
            m_candidates.push_back(marker);
        } else {
            auto farthest = std::max_element(m_candidates.begin(), m_candidates.end(),
                [](const Marker& a, const Marker& b) { return a.rank < b.rank; });
            if (marker.rank < farthest->rank)
                *farthest = marker;
        }
    }
}

void CUITacticalCompass::UpdateMarkers(float heading) {
    u32 visible = 0;
    const Marker* focused = nullptr;
    float focused_angle = deg2rad(m_focus_angle);
    u32 focused_distance = 0;
    const float kx = UI().get_current_kx();
    const bool scale_changed = fabsf(kx - m_last_ui_kx) > 0.001f;

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
        const float distance_sqr = direction.square_magnitude();
        if (distance_sqr > marker.max_distance * marker.max_distance)
            continue;

        float angle = atan2f(direction.x, direction.z) - heading;
        while (angle > PI) angle -= 2.f * PI;
        while (angle < -PI) angle += 2.f * PI;

        if (angle < -deg2rad(VISIBLE_DEGREES * 0.5f) ||
            angle > deg2rad(VISIBLE_DEGREES * 0.5f))
            continue;

        const SpotStyle& style = m_styles[marker.style_index];
        CUIStatic* icon = m_icons[visible];
        const bool style_changed = m_icon_styles[visible] != marker.style_index;
        if (style_changed) {
            icon->InitTexture(style.texture.c_str());
            icon->SetTextureRect(style.rect);
            m_icon_styles[visible] = marker.style_index;
        }
        if (style_changed || scale_changed) {
            icon->SetWndSize(Fvector2().set(
                style.size * style.rect.width() / style.rect.height() * kx, style.size));
        }

        float x = STRIP_WIDTH * 0.5f + rad2deg(angle) * STRIP_WIDTH / VISIBLE_DEGREES;
        const float width = icon->GetWidth();
        icon->SetWndPos(Fvector2().set(clampr(x - width * 0.5f, 0.f, STRIP_WIDTH - width),
                                      m_icon_y + style.offset_y));
        icon->Show(true);

        const float absolute_angle = fabsf(angle);
        if (absolute_angle <= deg2rad(m_focus_angle) &&
            (!focused || (marker.active_task && !focused->active_task) ||
             (marker.active_task == focused->active_task && absolute_angle < focused_angle))) {
            focused = &marker;
            focused_angle = absolute_angle;
            focused_distance = static_cast<u32>(sqrtf(distance_sqr) + 0.5f);
        }
        ++visible;
    }
    for (; visible < MAX_MARKERS; ++visible)
        m_icons[visible]->Show(false);
    m_last_ui_kx = kx;

    if (!m_has_labels)
        return;

    if (!focused) {
        m_title.Show(false);
        m_distance.Show(false);
        return;
    }

    m_title.Show(focused->title.size() != 0);
    if (focused->title != m_last_title || focused->title_is_key != m_last_title_is_key) {
        if (focused->title_is_key)
            m_title.SetTextST(focused->title.c_str());
        else
            m_title.SetText(focused->title.c_str());
        m_last_title = focused->title;
        m_last_title_is_key = focused->title_is_key;
    }
    m_distance.Show(true);
    if (focused_distance != m_last_distance) {
        string32 text;
        xr_sprintf(text, "%u %s", focused_distance, m_distance_unit.c_str());
        m_distance.SetText(text);
        m_last_distance = focused_distance;
    }
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
