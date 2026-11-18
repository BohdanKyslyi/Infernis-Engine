#pragma once

inline bool GunslingerMonsterFeatureEnabled(LPCSTR key) {
    constexpr LPCSTR section = "gunslinger_ie";
    return READ_IF_EXISTS(pSettings, r_bool, section, "enable_monster_extensions", true) &&
        READ_IF_EXISTS(pSettings, r_bool, section, key, true);
}
