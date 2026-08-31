#pragma once

// Values are compiled into shader permutations. They are deliberately read
// only while shaders/resources are being built, never from a pixel hot path.
inline bool infernis_pbr_rendering_enabled()
{
    if (!pSettings || !pSettings->section_exist("shader_extensions"))
        return true;

    return READ_IF_EXISTS(
        pSettings,
        r_bool,
        "shader_extensions",
        "enable_pbr_rendering",
        true
    );
}

inline u32 infernis_pbr_sslr_mode()
{
    if (!pSettings || !pSettings->section_exist("shader_extensions") ||
        !pSettings->line_exist("shader_extensions", "sslr_mode"))
        return 1; // performance

    LPCSTR mode = pSettings->r_string("shader_extensions", "sslr_mode");

    if (0 == stricmp(mode, "off"))
        return 0;
    if (0 == stricmp(mode, "balanced"))
        return 2;
    if (0 == stricmp(mode, "quality"))
        return 3;
    if (0 == stricmp(mode, "reference"))
        return 4;

    // Unknown values and the explicit "performance" value use the safe,
    // production default rather than enabling an expensive permutation.
    return 1;
}
