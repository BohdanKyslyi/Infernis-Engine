#pragma once

class ENGINE_API CDiscordRichPresence {
public:
    CDiscordRichPresence();

    void Initialize();
    void Shutdown();

    void SetMenuStatus();
    void SetLocationStatus(LPCSTR location_name);

    void SetStatus(LPCSTR details, LPCSTR state = nullptr);

    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized;
    bool m_show_playtime;
    bool m_show_location;

    s64 m_start_timestamp;

    string128 m_details;
    string128 m_state;
};

extern ENGINE_API CDiscordRichPresence g_discord;