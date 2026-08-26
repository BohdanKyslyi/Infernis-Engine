#include "stdafx.h"
#include "DiscordRichPresence.h"

#include "discord_rpc.h"

#include <ctime>

namespace {

constexpr UINT XRayCodePage = 1251;

xr_string ToDiscordUTF8(LPCSTR source) {
    if (!source || !source[0])
        return xr_string();

    const int wide_length = MultiByteToWideChar(XRayCodePage, 0, source, -1, nullptr, 0);

    if (wide_length <= 0)
        return xr_string(source);

    xr_vector<wchar_t> wide_buffer(wide_length);

    if (!MultiByteToWideChar(XRayCodePage, 0, source, -1, &wide_buffer[0], wide_length)) {
        return xr_string(source);
    }

    const int utf8_length =
        WideCharToMultiByte(CP_UTF8, 0, &wide_buffer[0], -1, nullptr, 0, nullptr, nullptr);

    if (utf8_length <= 0)
        return xr_string(source);

    xr_vector<char> utf8_buffer(utf8_length);

    if (!WideCharToMultiByte(CP_UTF8, 0, &wide_buffer[0], -1, &utf8_buffer[0], utf8_length, nullptr,
                             nullptr)) {
        return xr_string(source);
    }

    return xr_string(&utf8_buffer[0]);
}

} // namespace

extern int g_current_renderer;

ENGINE_API CDiscordRichPresence g_discord;

CDiscordRichPresence::CDiscordRichPresence()
    : m_initialized(false), m_show_playtime(true), m_show_location(true), m_show_renderer(true),
      m_renderer_id(-1), m_start_timestamp(0) {
    m_details[0] = '\0';
    m_state[0] = '\0';

    m_large_image_key[0] = '\0';
    m_large_image_text[0] = '\0';

    m_renderer_image_key[0] = '\0';
    m_renderer_image_text[0] = '\0';
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
    m_show_location = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "show_location", true);
    m_show_renderer = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "show_renderer", true);

    LPCSTR large_image_key = READ_IF_EXISTS(pSettings, r_string, "discord_rpc", "large_image", "");

    LPCSTR large_image_text =
        READ_IF_EXISTS(pSettings, r_string, "discord_rpc", "large_image_text", "Infernis Engine");

    xr_strcpy(m_large_image_key, sizeof(m_large_image_key), large_image_key);

    const xr_string utf8_large_image_text = ToDiscordUTF8(large_image_text);

    xr_strcpy(m_large_image_text, sizeof(m_large_image_text), utf8_large_image_text.c_str());

    DiscordEventHandlers handlers{};

    //
    // Auto-registration is unnecessary until we implement
    // join/spectate functionality.
    //
    Discord_Initialize(application_id, &handlers, FALSE, nullptr);

    m_initialized = true;

    if (m_show_playtime)
        m_start_timestamp = static_cast<s64>(std::time(nullptr));

    SetMenuStatus();

    Msg("* Discord RPC: initialized");
}

void CDiscordRichPresence::SetMenuStatus() { SetStatus("Infernis Engine", "In Main Menu"); }

void CDiscordRichPresence::SetLocationStatus(LPCSTR location_name) {
    if (!m_show_location || !location_name || !location_name[0]) {
        SetStatus("Playing Infernis Engine");
        return;
    }

    SetStatus(location_name, "Exploring the Zone");
}

void CDiscordRichPresence::UpdateRendererInfo() {
    m_renderer_id = g_current_renderer;

    m_renderer_image_key[0] = '\0';
    m_renderer_image_text[0] = '\0';

    if (!m_show_renderer)
        return;

    LPCSTR image_setting = nullptr;
    LPCSTR renderer_text = nullptr;

    switch (g_current_renderer) {
    case 1:
        image_setting = "renderer_dx9_image";
        renderer_text = "DirectX 9 - Renderer R1";
        break;

    case 2:
        image_setting = "renderer_dx9_image";
        renderer_text = "DirectX 9 - Renderer R2";
        break;

    case 3:
        image_setting = "renderer_dx10_image";
        renderer_text = "DirectX 10 - Renderer R3";
        break;

    case 4:
        image_setting = "renderer_dx11_image";
        renderer_text = "DirectX 11 - Renderer R4";
        break;

    default:
        Msg("! Discord RPC: unknown renderer [%d]", g_current_renderer);
        return;
    }

    LPCSTR image_key = READ_IF_EXISTS(pSettings, r_string, "discord_rpc", image_setting, "");

    xr_strcpy(m_renderer_image_key, sizeof(m_renderer_image_key), image_key);

    const xr_string utf8_renderer_text = ToDiscordUTF8(renderer_text);

    xr_strcpy(m_renderer_image_text, sizeof(m_renderer_image_text), utf8_renderer_text.c_str());

    Msg("* Discord RPC: renderer [%s], image [%s]", renderer_text,
        m_renderer_image_key[0] ? m_renderer_image_key : "-");
}

void CDiscordRichPresence::SetStatus(LPCSTR details, LPCSTR state) {
    if (!m_initialized)
        return;

    LPCSTR safe_details = details ? details : "";
    LPCSTR safe_state = state ? state : "";

    const xr_string utf8_details = ToDiscordUTF8(safe_details);
    const xr_string utf8_state = ToDiscordUTF8(safe_state);

    const bool renderer_changed = m_renderer_id != g_current_renderer;

    if (renderer_changed)
        UpdateRendererInfo();

    //
    // Do not send an identical presence unless the renderer changed.
    //
    if (!renderer_changed && xr_strcmp(m_details, utf8_details.c_str()) == 0 &&
        xr_strcmp(m_state, utf8_state.c_str()) == 0) {
        return;
    }

    xr_strcpy(m_details, sizeof(m_details), utf8_details.c_str());
    xr_strcpy(m_state, sizeof(m_state), utf8_state.c_str());

    DiscordRichPresence presence{};

    presence.details = m_details[0] ? m_details : nullptr;

    presence.state = m_state[0] ? m_state : nullptr;

    presence.startTimestamp = m_show_playtime ? m_start_timestamp : 0;

    presence.largeImageKey = m_large_image_key[0] ? m_large_image_key : nullptr;

    presence.largeImageText =
        m_large_image_key[0] && m_large_image_text[0] ? m_large_image_text : nullptr;

    presence.smallImageKey = m_renderer_image_key[0] ? m_renderer_image_key : nullptr;

    presence.smallImageText =
        m_renderer_image_key[0] && m_renderer_image_text[0] ? m_renderer_image_text : nullptr;

    Discord_UpdatePresence(&presence);

    //
    // Log the original Windows-1251 text,
    // because X-Ray log does not expect UTF-8.
    //
    Msg("* Discord RPC: status updated [%s] [%s]", safe_details[0] ? safe_details : "-",
        safe_state[0] ? safe_state : "-");
}

void CDiscordRichPresence::Shutdown() {
    if (!m_initialized)
        return;

    Discord_ClearPresence();
    Discord_Shutdown();

    m_initialized = false;
    m_start_timestamp = 0;
    m_renderer_id = -1;

    Msg("* Discord RPC: shutdown");
}