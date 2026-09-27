#include "stdafx.h"
#include "SoundRender_Core.h"
#include <algorithm>

float CSoundRender_Core::dynamic_transmission(const Fvector& source, const CObject* owner) {
    if (!psSoundAcoustics) return 1.f;
    const Fvector& listener = listener_position();
    Fvector segment;
    segment.sub(source, listener);
    const float length2 = segment.square_magnitude();
    if (length2 < 0.25f) return 1.f;

    float transmission = 1.f;
    std::lock_guard<std::mutex> lock(acoustic_mutex);
    for (const auto& obstacle : acoustic_obstacles) {
        if (obstacle.object == owner) continue;
        Fvector offset;
        offset.sub(obstacle.position, listener);
        const float t = offset.dotproduct(segment) / length2;
        if (t <= 0.05f || t >= 0.95f) continue;
        Fvector closest;
        closest.mad(listener, segment, t);
        if (closest.distance_to_sqr(obstacle.position) < obstacle.radius * obstacle.radius) {
            transmission *= 0.68f; // sphere proxy: partial obstruction, not a sealed wall
            if (transmission < 0.35f) return 0.35f;
        }
    }
    return transmission;
}

void CSoundRender_Core::update_acoustic_room(const Fvector& listener) {
    if (!geom_MODEL || !psSoundAcoustics ||
        (room_probe_time >= 0.f && fTimer_Value - room_probe_time < 0.35f)) return;
    room_probe_time = fTimer_Value;
    const Fvector directions[] = {
        { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f },
        { 0.f, 1.f, 0.f }, { 0.f, -1.f, 0.f },
        { 0.f, 0.f, 1.f }, { 0.f, 0.f, -1.f }
    };
    const float max_range = 18.f;
    float sum = 0.f;
    unsigned hits = 0;
    for (const auto& dir : directions) {
#ifdef _EDITOR
        ETOOLS::ray_options(CDB::OPT_ONLYNEAREST);
        ETOOLS::ray_query(geom_MODEL, listener, dir, max_range);
        if (ETOOLS::r_count()) { sum += ETOOLS::r_begin()->range; ++hits; }
#else
        geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
        geom_DB.ray_query(geom_MODEL, listener, dir, max_range);
        if (geom_DB.r_count()) { sum += geom_DB.r_begin()->range; ++hits; }
#endif
    }
    // Preserve transitions when crossing a doorway. An open ceiling keeps
    // outdoor spaces from being classified as a room by nearby walls.
    const float openness = 1.f - float(hits) / 6.f;
    room_openness += (openness - room_openness) * 0.3f;
    const float extent = hits ? sum / hits : max_range;
    room_extent += (extent - room_extent) * 0.3f;
}
