#include "stdafx.h"
#include "SoundRender_Core.h"
#include <algorithm>
#include <cmath>

namespace {
float segment_proxy(const Fvector& listener, const Fvector& segment, float length2,
                    const SAcousticObstacle& obstacle) {
    Fvector offset;
    offset.sub(obstacle.position, listener);
    const float t = offset.dotproduct(segment) / length2;
    if (t <= 0.05f || t >= 0.95f) return 1.f;
    Fvector closest;
    closest.mad(listener, segment, t);
    Fvector delta;
    delta.sub(closest, obstacle.position);
    if (obstacle.shape == 0)
        return delta.square_magnitude() < obstacle.radius * obstacle.radius ? 0.68f : 1.f;
    const Fvector& e = obstacle.half_extent;
    if (obstacle.shape == 1) { // vertical capsule
        const float axis = std::max(0.f, e.y - std::max(e.x, e.z));
        const float y = std::max(0.f, std::abs(delta.y) - axis);
        const float r = std::max(e.x, e.z);
        return delta.x * delta.x + delta.z * delta.z + y * y < r * r ? 0.60f : 1.f;
    }
    if (obstacle.shape == 2)
        return delta.x * delta.x / (e.x * e.x) + delta.y * delta.y / (e.y * e.y) +
                   delta.z * delta.z / (e.z * e.z) < 1.f ? 0.60f : 1.f;
    // Segment versus the world-space box, including long, thin boxes where the
    // nearest point to the center may miss an actual crossing.
    float enter = 0.f, leave = 1.f;
    float start[] = {listener.x, listener.y, listener.z};
    float direction[] = {segment.x, segment.y, segment.z};
    float center[] = {obstacle.position.x, obstacle.position.y, obstacle.position.z};
    if (obstacle.shape == 4) {
        for (int axis = 0; axis < 3; ++axis) {
            start[axis] = -offset.dotproduct(obstacle.axes[axis]);
            direction[axis] = segment.dotproduct(obstacle.axes[axis]);
            center[axis] = 0.f;
        }
    }
    const float extent[] = {e.x, e.y, e.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 0.0001f) {
            if (std::abs(start[axis] - center[axis]) > extent[axis]) return 1.f;
        } else {
            float a = (center[axis] - extent[axis] - start[axis]) / direction[axis];
            float b = (center[axis] + extent[axis] - start[axis]) / direction[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
            if (enter > leave) return 1.f;
        }
    }
    return 0.55f;
}
}

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
        if (psSoundAcousticsPreset == 0) {
            Fvector offset;
            offset.sub(obstacle.position, listener);
            const float t = offset.dotproduct(segment) / length2;
            if (t <= 0.05f || t >= 0.95f) continue;
            Fvector closest;
            closest.mad(listener, segment, t);
            if (closest.distance_to_sqr(obstacle.position) >= obstacle.radius * obstacle.radius)
                continue;
            transmission *= 0.68f;
        } else {
            transmission *= segment_proxy(listener, segment, length2, obstacle);
        }
        if (transmission < 0.35f) return 0.35f;
    }
    return transmission;
}

CSoundRender_Core::SAcousticBands CSoundRender_Core::dynamic_transmission_bands(
    const Fvector& source, const CObject* owner) {
    SAcousticBands bands;
    if (!psSoundAcoustics || psSoundAcousticsPreset != 1) return bands;
    const Fvector& listener = listener_position();
    Fvector segment;
    segment.sub(source, listener);
    const float length2 = segment.square_magnitude();
    if (length2 < 0.25f) return bands;
    static const float absorption[5][3] = {
        {0.58f, 0.32f, 0.12f}, // solid
        {0.85f, 0.62f, 0.35f}, // intact glass
        {0.62f, 0.37f, 0.16f}, // wood
        {0.55f, 0.27f, 0.10f}, // metal
        {0.78f, 0.50f, 0.25f}  // fabric
    };
    std::lock_guard<std::mutex> lock(acoustic_mutex);
    for (const auto& obstacle : acoustic_obstacles) {
        if (obstacle.object == owner ||
            segment_proxy(listener, segment, length2, obstacle) >= 1.f) continue;
        const unsigned material = std::min<unsigned>(obstacle.material, 4);
        const Fvector& e = obstacle.half_extent;
        const float thickness = std::min(e.x, std::min(e.y, e.z)) * 2.f;
        const float weight = std::min(2.5f, 1.2f + thickness);
        bands.low *= powf(absorption[material][0], weight);
        bands.mid *= powf(absorption[material][1], weight);
        bands.high *= powf(absorption[material][2], weight);
        if (bands.high < 0.03f) break;
    }
    bands.low = std::max(0.10f, bands.low);
    bands.mid = std::max(0.025f, bands.mid);
    bands.high = std::max(0.01f, bands.high);
    return bands;
}

float CSoundRender_Core::diffraction_transmission(const Fvector& source, const CObject* owner) {
    if (!psSoundAcoustics || psSoundAcousticsPreset != 1 || !geom_MODEL) return 0.f;
    if (!diffraction_budget) return -1.f; // defer without discarding the last valid path
    --diffraction_budget;
    const Fvector& listener = listener_position();
    Fvector path;
    path.sub(source, listener);
    const float distance = path.magnitude();
    if (distance < 1.f || distance > 25.f) return 0.f;
    Fvector direction = path;
    direction.div(distance);
    float fraction = 0.5f;
    bool static_wall = false;
#ifdef _EDITOR
    ETOOLS::ray_options(CDB::OPT_ONLYNEAREST);
    ETOOLS::ray_query(geom_MODEL, listener, direction, distance);
    if (ETOOLS::r_count()) {
        static_wall = true;
        fraction = ETOOLS::r_begin()->range / distance;
    }
#else
    geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
    geom_DB.ray_query(geom_MODEL, listener, direction, distance);
    if (geom_DB.r_count()) {
        static_wall = true;
        fraction = geom_DB.r_begin()->range / distance;
    }
#endif
    {
        std::lock_guard<std::mutex> lock(acoustic_mutex);
        for (const auto& obstacle : acoustic_obstacles) {
            if (obstacle.object == owner ||
                segment_proxy(listener, path, path.square_magnitude(), obstacle) >= 1.f) continue;
            Fvector offset;
            offset.sub(obstacle.position, listener);
            const float t = offset.dotproduct(path) / path.square_magnitude();
            if (t > 0.05f && t < fraction) fraction = t;
        }
    }
    fraction = std::max(0.15f, std::min(0.85f, fraction));
    Fvector side;
    side.set(-path.z / distance, 0.f, path.x / distance);
    // Try two lateral distances on either side, then openings above/below.
    // Each accepted route must clear both fixed and moving blockers.
    for (int candidate = 0; candidate < (static_wall ? 4 : 6); ++candidate) {
        Fvector waypoint;
        waypoint.mad(listener, path, fraction);
        if (candidate < 4)
            waypoint.mad(waypoint, side, (candidate & 1 ? 1.f : -1.f) *
                (candidate < 2 ? 0.9f : 1.7f));
        else
            waypoint.y += candidate == 4 ? 1.1f : -1.1f;
        const Fvector points[] = {listener, waypoint, source};
        bool clear = true;
        for (int leg = 0; leg < 2 && clear; ++leg) {
            Fvector segment;
            segment.sub(points[leg + 1], points[leg]);
            const float range = segment.magnitude();
            Fvector ray = segment;
            ray.div(range);
#ifdef _EDITOR
            ETOOLS::ray_options(CDB::OPT_ONLYNEAREST);
            ETOOLS::ray_query(geom_MODEL, points[leg], ray, range);
            clear = ETOOLS::r_count() == 0;
            if (clear && geom_SOM) {
                ETOOLS::ray_options(CDB::OPT_CULL);
                ETOOLS::ray_query(geom_SOM, points[leg], ray, range);
                float transmission = 1.f;
                for (u32 hit = 0; hit < ETOOLS::r_count(); ++hit)
                    transmission *= *(float*)&ETOOLS::r_begin()[hit].dummy;
                clear = transmission > 0.55f;
            }
#else
            geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
            geom_DB.ray_query(geom_MODEL, points[leg], ray, range);
            clear = geom_DB.r_count() == 0;
            if (clear && geom_SOM) {
                geom_DB.ray_options(CDB::OPT_CULL);
                geom_DB.ray_query(geom_SOM, points[leg], ray, range);
                float transmission = 1.f;
                for (u32 hit = 0; hit < geom_DB.r_count(); ++hit)
                    transmission *= *(float*)&geom_DB.r_begin()[hit].dummy;
                clear = transmission > 0.55f;
            }
#endif
            if (clear) {
                std::lock_guard<std::mutex> lock(acoustic_mutex);
                for (const auto& obstacle : acoustic_obstacles) {
                    if (obstacle.object != owner &&
                        segment_proxy(points[leg], segment, segment.square_magnitude(),
                                      obstacle) < 1.f) {
                        clear = false;
                        break;
                    }
                }
            }
        }
        if (clear) return static_wall ? 0.12f : (candidate < 4 ? 0.35f : 0.20f);
    }
    return 0.f;
}

void CSoundRender_Core::update_acoustic_room(const Fvector& listener) {
    const bool quality = psSoundAcousticsPreset == 1;
    if (!geom_MODEL || !psSoundAcoustics ||
        (room_probe_time >= 0.f && fTimer_Value - room_probe_time < (quality ? 0.5f : 0.35f))) return;
    room_probe_time = fTimer_Value;
    const Fvector directions[] = {
        { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f },
        { 0.f, 1.f, 0.f }, { 0.f, -1.f, 0.f },
        { 0.f, 0.f, 1.f }, { 0.f, 0.f, -1.f }
    };
    const float max_range = 18.f;
    float sum = 0.f;
    unsigned hits = 0;
    const Fvector extra[] = {
        {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
        {1, 1, 0}, {-1, 1, 0}, {0, 1, 1}, {0, 1, -1},
        {1, -1, 0}, {-1, -1, 0}, {0, -1, 1}, {0, -1, -1}
    };
    const int count = quality ? 18 : 6;
    for (int i = 0; i < count; ++i) {
        Fvector dir = i < 6 ? directions[i] : extra[i - 6];
        dir.normalize();
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
    const float openness = 1.f - float(hits) / float(count);
    room_openness += (openness - room_openness) * (quality ? 0.22f : 0.3f);
    const float extent = hits ? sum / hits : max_range;
    room_extent += (extent - room_extent) * (quality ? 0.22f : 0.3f);
}
