#pragma once

#include "../xrEngine/infernis_pbr_console_vars.h"

// Values are compiled into shader permutations. They are deliberately read
// only while shaders/resources are being built, never from a pixel hot path.
//
// Both belong to the settings page now ( pbr_enable, pbr_sslr_mode ), and what is returned here is
// the pair the game STARTED with - xrEngine/infernis_pbr_console_vars.h says why not the live one.
// Their start values are still the two lines of [shader_extensions] this file used to read itself
// ( pbr_settings_config_loaded in xrEngine/xr_ioc_cmd.cpp, same fallbacks ).
inline bool infernis_pbr_rendering_enabled()
{
    return pbr_session_enable != 0;
}

inline u32 infernis_pbr_sslr_mode()
{
    return pbr_session_sslr_mode;
}
