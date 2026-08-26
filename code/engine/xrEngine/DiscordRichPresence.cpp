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

    void CopyDiscordText(char* destination, size_t destination_size, const xr_string& source) {
        if (!destination || destination_size == 0)
            return;

        size_t length = source.size();

        if (length >= destination_size) {
            length = destination_size - 1;

            const char* value = source.c_str();

            //
            // Do not cut a multibyte UTF-8 character in half.
            //
            while (length > 0 && (static_cast<u8>(value[length]) & 0xC0) == 0x80)
                --length;
        }

        CopyMemory(destination, source.c_str(), length);
        destination[length] = '\0';
    }
} // namespace

extern int g_current_renderer;

ENGINE_API CDiscordRichPresence g_discord;

CDiscordRichPresence::CDiscordRichPresence()
    : m_initialized(false), m_show_playtime(true), m_show_location(true), m_show_renderer(true),
      m_show_task(true), m_show_task_name(true), m_renderer_id(-1), m_start_timestamp(0) {
    m_details[0] = '\0';
    m_state[0] = '\0';

    m_location[0] = '\0';
    m_task[0] = '\0';

    m_large_image_key[0] = '\0';
    m_large_image_text[0] = '\0';

    m_renderer_image_key[0] = '\0';
    m_renderer_image_text[0] = '\0';

    xr_strcpy(m_menu_state, sizeof(m_menu_state), "In Main Menu");
    xr_strcpy(m_exploring_state, sizeof(m_exploring_state), "Exploring the Zone");
    xr_strcpy(m_hidden_task_state, sizeof(m_hidden_task_state), "On a mission");
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

    m_show_task = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "show_task", true);
    m_show_task_name = READ_IF_EXISTS(pSettings, r_bool, "discord_rpc", "show_task_name", true);

    LPCSTR large_image_key = READ_IF_EXISTS(pSettings, r_string, "discord_rpc", "large_image", "");

    LPCSTR large_image_text =
        READ_IF_EXISTS(pSettings, r_string, "discord_rpc", "large_image_text", "Infernis Engine");

    xr_strcpy(m_large_image_key, sizeof(m_large_image_key), large_image_key);

    const xr_string utf8_large_image_text = ToDiscordUTF8(large_image_text);

    CopyDiscordText(m_large_image_text, sizeof(m_large_image_text), utf8_large_image_text);

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

void CDiscordRichPresence::SetMenuStatus() {
    m_location[0] = '\0';
    m_task[0] = '\0';

    SetStatus("Infernis Engine", m_menu_state);
}

void CDiscordRichPresence::SetLocationStatus(LPCSTR location_name) {
    m_location[0] = '\0';

    if (m_show_location && location_name && location_name[0])
        xr_strcpy(m_location, sizeof(m_location), location_name);

    UpdateGameStatus();
}

void CDiscordRichPresence::SetTaskStatus(LPCSTR task_name) {
    m_task[0] = '\0';

    if (m_show_task && task_name && task_name[0])
        xr_strcpy(m_task, sizeof(m_task), task_name);

    UpdateGameStatus();
}

void CDiscordRichPresence::ClearTaskStatus() {
    m_task[0] = '\0';

    UpdateGameStatus();
}

void CDiscordRichPresence::UpdateGameStatus() {
    LPCSTR details = m_location[0] ? m_location : "Infernis Engine";

    LPCSTR state = m_exploring_state;

    if (m_show_task && m_task[0])
        state = m_show_task_name ? m_task : m_hidden_task_state;

    SetStatus(details, state);
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

    CopyDiscordText(m_renderer_image_text, sizeof(m_renderer_image_text), utf8_renderer_text);

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

    string128 discord_details;
    string128 discord_state;

    CopyDiscordText(discord_details, sizeof(discord_details), utf8_details);
    CopyDiscordText(discord_state, sizeof(discord_state), utf8_state);

    const bool renderer_changed = m_renderer_id != g_current_renderer;

    if (renderer_changed)
        UpdateRendererInfo();

    //
    // Do not send an identical presence unless the renderer changed.
    //
    if (!renderer_changed && xr_strcmp(m_details, discord_details) == 0 &&
        xr_strcmp(m_state, discord_state) == 0) {
        return;
    }

    xr_strcpy(m_details, sizeof(m_details), discord_details);
    xr_strcpy(m_state, sizeof(m_state), discord_state);

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

void CDiscordRichPresence::SetLocalizedStatusTexts(LPCSTR menu_state, LPCSTR exploring_state,
                                                   LPCSTR hidden_task_state) {
    if (menu_state && menu_state[0])
        xr_strcpy(m_menu_state, sizeof(m_menu_state), menu_state);

    if (exploring_state && exploring_state[0])
        xr_strcpy(m_exploring_state, sizeof(m_exploring_state), exploring_state);

    if (hidden_task_state && hidden_task_state[0])
        xr_strcpy(m_hidden_task_state, sizeof(m_hidden_task_state), hidden_task_state);
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
    m_location[0] = '\0';
    m_task[0] = '\0';
}