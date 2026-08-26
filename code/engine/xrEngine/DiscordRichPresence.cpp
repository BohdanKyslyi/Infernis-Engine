#include "stdafx.h"
#include "DiscordRichPresence.h"

#include "discord_rpc.h"

#include <ctime>

ENGINE_API CDiscordRichPresence g_discord;

CDiscordRichPresence::CDiscordRichPresence()
    : m_initialized(false), m_show_playtime(true), m_start_timestamp(0) {
    m_details[0] = '\0';
    m_state[0] = '\0';
}

void CDiscordRichPresence::Initialize() {
    if (m_initialized)
        return;

    if (!pSettings || !pSettings->section_exist("discord_rpc")) {
        Msg("* Discord RPC: configuration section is missing");
        return;
    }

    const bool enabled = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "enable", false);

    if (!enabled) {
        Msg("* Discord RPC: disabled");
        return;
    }

    LPCSTR application_id =
        READ_IF_EXISTS(pSettings, r_string, "discord_rpc", "application_id", "");

    if (!application_id || !application_id[0] ||
        (application_id[0] == '0' && application_id[1] == '\0')) {
        Msg("! Discord RPC: invalid application_id");
        return;
    }

    m_show_playtime = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "show_playtime", true);

    DiscordEventHandlers handlers{};

    //
    // Auto-registration is unnecessary until we implement
    // join/spectate functionality.
    //
    Discord_Initialize(application_id, &handlers, FALSE, nullptr);

    m_initialized = true;

    if (m_show_playtime)
        m_start_timestamp = static_cast<s64>(std::time(nullptr));

    SetStatus("Playing Infernis Engine");

    Msg("* Discord RPC: initialized");
}

void CDiscordRichPresence::SetStatus(LPCSTR details, LPCSTR state) {
    if (!m_initialized)
        return;

    xr_strcpy(m_details, sizeof(m_details), details ? details : "");

    xr_strcpy(m_state, sizeof(m_state), state ? state : "");

    DiscordRichPresence presence{};

    presence.details = m_details[0] ? m_details : nullptr;

    presence.state = m_state[0] ? m_state : nullptr;

    presence.startTimestamp = m_show_playtime ? m_start_timestamp : 0;

    Discord_UpdatePresence(&presence);
}

void CDiscordRichPresence::Shutdown() {
    if (!m_initialized)
        return;

    Discord_ClearPresence();
    Discord_Shutdown();

    m_initialized = false;
    m_start_timestamp = 0;

    Msg("* Discord RPC: shutdown");
}