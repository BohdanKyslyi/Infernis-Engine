#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <string>

namespace lightning_editor {
struct Parameter { const char* key; float minimum, maximum, step, fallback; bool integral; };
inline constexpr Parameter parameters[] = {
    {"enable_multi_strikes", 0.f, 1.f, 1.f, 0.f, true},
    {"multi_strike_chance", 0.f, 100.f, 1.f, 25.f, true},
    {"max_simultaneous", 2.f, 8.f, 1.f, 3.f, true},
    {"same_location_chance", 0.f, 100.f, 1.f, 40.f, true},
    {"branching_chance", 0.f, 100.f, 1.f, 50.f, true},
    {"height_factor", 0.05f, 2.f, 0.01f, 0.8f, false},
    {"size_multiplier", 0.1f, 10.f, 0.05f, 2.5f, false},
    {"prob_near", 0.f, 100.f, 1.f, 20.f, true},
    {"prob_medium", 0.f, 100.f, 1.f, 40.f, true},
    {"dist_near_min", 0.f, 3000.f, 1.f, 0.f, false},
    {"dist_near_max", 0.f, 3000.f, 1.f, 5.f, false},
    {"dist_medium_min", 0.f, 3000.f, 1.f, 10.f, false},
    {"dist_medium_max", 0.f, 3000.f, 1.f, 40.f, false},
    {"dist_far_min", 0.f, 6000.f, 1.f, 40.f, false},
    {"dist_far_max", 0.f, 6000.f, 1.f, 150.f, false},
    {"thunder_range_min", 0.1f, 6000.f, 1.f, 120.f, false},
    {"thunder_range_max", 0.2f, 10000.f, 1.f, 1000.f, false},
    {"thunder_fade_dist", 1.f, 6000.f, 1.f, 300.f, false},
    {"thunder_pitch_drop", 0.f, 1.f, 0.01f, 0.35f, false},
    {"thunder_vol_drop", 0.f, 1.f, 0.01f, 0.25f, false},
    {"strike_snd_range_min", 0.1f, 3000.f, 1.f, 8.f, false},
    {"strike_snd_range_max", 0.2f, 6000.f, 1.f, 35.f, false},
    {"strike_damage_power", 0.f, 20.f, 0.05f, 2.5f, false},
    {"strike_damage_radius", 0.f, 200.f, 0.5f, 20.f, false},
};
inline constexpr unsigned count = sizeof(parameters) / sizeof(parameters[0]);
struct Settings {
    std::array<float, count> values{};
    std::string particle, sound;
    Settings() { for (unsigned i=0; i<count; ++i) values[i]=parameters[i].fallback; }
};
inline Settings normalize(Settings s) {
    for (unsigned i=0; i<count; ++i) {
        const auto& p=parameters[i];
        float v=std::isfinite(s.values[i]) ? s.values[i] : p.fallback;
        v=std::clamp(v,p.minimum,p.maximum);
        s.values[i]=p.integral ? std::round(v) : v;
    }
    s.values[8]=std::min(s.values[8],100.f-s.values[7]);
    for (unsigned i : {9u,11u,13u}) s.values[i+1]=std::max(s.values[i+1],s.values[i]);
    for (unsigned i : {15u,20u}) s.values[i+1]=std::max(s.values[i+1],s.values[i]+.1f);
    return s;
}
inline void change(Settings& s, unsigned i, float value) {
    if(i>=count) return;
    s.values[i]=value;
    if(i==7) s.values[8]=std::min(s.values[8],100.f-std::clamp(value,0.f,100.f));
    if(i==8) s.values[7]=std::min(s.values[7],100.f-std::clamp(value,0.f,100.f));
    for(unsigned low : {9u,11u,13u,15u,20u}) {
        const float gap=(low==15 || low==20) ? .1f : 0.f;
        if(i==low) s.values[low+1]=std::max(s.values[low+1],value+gap);
        if(i==low+1) s.values[low]=std::min(s.values[low],value-gap);
    }
    s=normalize(s);
}
} // namespace lightning_editor
