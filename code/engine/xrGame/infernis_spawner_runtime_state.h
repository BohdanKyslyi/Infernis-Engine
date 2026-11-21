#pragma once

// Save payload independent of actor packets, pstor and Lua binders.
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace infernis_spawner::runtime {
enum Option : std::uint32_t {
    immortal = 1u << 0, power = 1u << 1, bleeding = 1u << 2,
    repair = 1u << 3, unlimited_ammo = 1u << 4, position = 1u << 5,
    info = 1u << 6, restrictors = 1u << 7, distance = 1u << 8,
    weather_lock = 1u << 9, weather_fx = 1u << 10
};
constexpr std::uint32_t known_flags = (1u << 11) - 1;
constexpr std::uint32_t save_chunk = 0x5350574e; // SPWN, optional ALife root chunk
constexpr std::size_t max_payload = 1024;

inline Option option(std::string_view name) {
    if (name == "immortal_show") return immortal;
    if (name == "power_show") return power;
    if (name == "bleeding_show") return bleeding;
    if (name == "repair_show") return repair;
    if (name == "ammunitions_show") return unlimited_ammo;
    if (name == "position_show") return position;
    if (name == "info_show") return info;
    if (name == "space_restrictor_show") return restrictors;
    if (name == "distance_show") return distance;
    if (name == "weather_state") return weather_lock;
    if (name == "god_weather_fx") return weather_fx;
    return static_cast<Option>(0);
}

struct State {
    std::uint32_t flags = 0;
    std::string weather;
    std::string anchor_level;
    float anchor[3] = {};
    bool has(Option flag) const { return (flags & flag) != 0; }
    void set(Option flag, bool value) {
        if (value) flags |= flag; else flags &= ~flag;
    }
};

inline bool valid_state(const State& s) {
    return !(s.flags & ~known_flags) && s.weather.size() <= 127 &&
        s.anchor_level.size() <= 127 && s.weather.find('\0') == std::string::npos &&
        s.anchor_level.find('\0') == std::string::npos &&
        std::isfinite(s.anchor[0]) && std::isfinite(s.anchor[1]) && std::isfinite(s.anchor[2]);
}

inline std::vector<std::uint8_t> encode(const State& s) {
    if (!valid_state(s)) return {};
    std::vector<std::uint8_t> data;
    const auto integer = [&data](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    const auto text = [&data, &integer](const std::string& v) {
        integer(static_cast<std::uint32_t>(v.size()));
        data.insert(data.end(), v.begin(), v.end());
    };
    integer(1); integer(s.flags); text(s.weather); text(s.anchor_level);
    for (float value : s.anchor) {
        std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); integer(bits);
    }
    return data;
}

// Reject partial/unknown payloads atomically, leaving the caller's defaults intact.
inline bool decode(const std::uint8_t* data, std::size_t size, State& output) {
    if (!data || size > max_payload) return false;
    std::size_t at = 0;
    const auto integer = [&](std::uint32_t& v) {
        if (size - at < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(data[at++]) << (8 * i);
        return true;
    };
    const auto text = [&](std::string& v) {
        std::uint32_t length;
        if (!integer(length) || length > 127 || size - at < length) return false;
        v.assign(reinterpret_cast<const char*>(data + at), length); at += length;
        return true;
    };
    State s;
    std::uint32_t version;
    if (!integer(version) || version != 1 || !integer(s.flags) ||
        !text(s.weather) || !text(s.anchor_level)) return false;
    for (float& value : s.anchor) {
        std::uint32_t bits;
        if (!integer(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
    }
    if (at != size || !valid_state(s)) return false;
    output = s;
    return true;
}
} // namespace infernis_spawner::runtime
