#pragma once

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace infernis_spawner {
enum class WorldKind { none, npc, monster, anomaly, physics };

struct DestinationToken {
    bool valid = false;
    bool object = false;
    unsigned id = 0;
};

inline DestinationToken parse_destination(std::string_view text) {
    DestinationToken result;
    std::string_view digits;
    if (text.substr(0, 7) == "object:") { result.object = true; digits = text.substr(7); }
    else if (text.substr(0, 7) == "vertex:") digits = text.substr(7);
    else return result;
    if (digits.empty() || digits.size() > 5) return result;
    unsigned value = 0;
    for (char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return result;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value >= 65535) return result;
    result.valid = true; result.id = value;
    return result;
}

inline WorldKind world_class(std::string_view id) {
    if (id == "O_PHYSIC" || id == "P_DSTRBL") return WorldKind::physics;
    if (id == "AI_STL" || id == "AI_STL_S") return WorldKind::npc;
    constexpr std::string_view monsters[] = {
        "AI_BLOOD", "AI_BOAR", "AI_DOG_R", "AI_FLESH", "AI_DOG_B", "AI_BURER",
        "AI_CAT", "AI_HIMER", "AI_CONTR", "AI_FRACT", "AI_POLTR", "AI_GIANT",
        "AI_ZOM", "AI_SNORK", "AI_TUSH", "AI_DOG_P", "AI_DOG_F", "AI_RAT",
        "SM_BLOOD", "SM_BOARW", "SM_DOG_S", "SM_FLESH", "SM_P_DOG", "SM_BURER",
        "SM_CAT_S", "SM_CHIMS", "SM_CONTR", "SM_IZLOM", "SM_POLTR", "SM_GIANT",
        "SM_ZOMBI", "SM_SNORK", "SM_TUSHK", "SM_DOG_P", "SM_DOG_F"
    };
    for (auto value : monsters) if (id == value) return WorldKind::monster;
    constexpr std::string_view anomalies[] = {
        "Z_MBALD", "Z_MINCER", "Z_ACIDF", "Z_GALANT", "Z_RADIO", "Z_BFUZZ",
        "Z_RUSTYH", "Z_DEAD", "Z_TORRID", "Z_NOGRAV", "Z_CFIRE",
        "ZS_BFUZZ", "ZS_MBALD", "ZS_GALAN", "ZS_MINCE", "ZS_RADIO", "ZS_TORRD"
    };
    for (auto value : anomalies) if (id == value) return WorldKind::anomaly;
    return WorldKind::none;
}

inline WorldKind world_category(std::string_view category) {
    if (category == "npc") return WorldKind::npc;
    if (category == "monsters") return WorldKind::monster;
    if (category == "anomalies") return WorldKind::anomaly;
    if (category == "physics") return WorldKind::physics;
    return WorldKind::none;
}

inline const char* world_kind_name(WorldKind kind) {
    switch (kind) {
    case WorldKind::npc: return "npc";
    case WorldKind::monster: return "monster";
    case WorldKind::anomaly: return "anomaly";
    case WorldKind::physics: return "physics";
    default: return "none";
    }
}

inline bool valid_anomaly_radius(double radius) {
    return std::isfinite(radius) && radius > 0.0 && radius <= 100.0;
}

inline bool valid_anomaly_hit(std::string_view hit) {
    constexpr std::string_view values[] = {"burn", "light_burn", "shock", "strike", "wound",
        "radiation", "telepatic", "fire_wound", "chemical_burn", "explosion", "wound_2"};
    for (auto value : values) {
        if (hit.size() != value.size()) continue;
        bool equal = true;
        for (size_t i = 0; i < hit.size(); ++i)
            equal &= std::tolower(static_cast<unsigned char>(hit[i])) == value[i];
        if (equal) return true;
    }
    return false;
}

inline bool creature_position_valid(bool inside_graph, double plane_height_difference) {
    return inside_graph && std::isfinite(plane_height_difference) &&
           std::abs(plane_height_difference) <= 1.0;
}
} // namespace infernis_spawner
