#pragma once

#include <algorithm>
#include <cmath>

namespace weather_motion {
constexpr float pi = 3.14159265358979323846f;
constexpr float turn = 2.f * pi;
constexpr float tree_amplitude_max = 1.f;
constexpr float rain_tilt_max = 3.f;

inline float tree_amplitude(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.f, tree_amplitude_max) : 1.f;
}

inline float rain_tilt_multiplier(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.f, rain_tilt_max) : 1.f;
}

inline float normalize_direction(float radians) {
    if (!std::isfinite(radians)) return 0.f;
    float result = std::fmod(radians, turn);
    if (result < 0.f) result += turn;
    return result >= turn ? 0.f : result;
}

inline float interpolate_direction(float from, float to, float weight) {
    from = normalize_direction(from);
    to = normalize_direction(to);
    const float delta = std::remainder(to - from, turn);
    return normalize_direction(from + delta * std::clamp(weight, 0.f, 1.f));
}

inline float rain_wind_influence(float velocity, float gust) {
    // The environment supplies gust in [0, 1]. Keep the steady wind present
    // between gusts instead of periodically turning a windy rain vertical.
    const float wind = std::isfinite(velocity) ? std::max(0.f, velocity) : 0.f;
    const float strength = std::isfinite(gust) ? std::clamp(gust, 0.f, 1.f) : 0.f;
    return std::clamp(wind * (0.75f + 0.25f * strength) / 20.f, 0.f, 1.f);
}

inline float rain_path_length(float vertical_height, float direction_y) {
    return vertical_height / std::max(0.1f, -direction_y);
}

inline float rain_plane_offset(float position, float eye_position,
    float vertical_offset, float direction, float direction_y) {
    return position + direction * vertical_offset / std::max(0.1f, -direction_y) - eye_position;
}
} // namespace weather_motion
