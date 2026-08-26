#pragma once

class ENGINE_API CDiscordRichPresence {
public:
    CDiscordRichPresence();

    void Initialize();
    void Shutdown();

    void SetMenuStatus();
    void SetLocationStatus(LPCSTR location_name);
    void SetTaskStatus(LPCSTR task_name);
    void ClearTaskStatus();

    void SetStatus(LPCSTR details, LPCSTR state = nullptr);

    void SetLocalizedStatusTexts(LPCSTR menu_state, LPCSTR exploring_state,
                                 LPCSTR hidden_task_state);

    bool IsInitialized() const { return m_initialized; }

private:
    void UpdateGameStatus();
    void UpdateRendererInfo();

private:
    bool m_initialized;
    bool m_show_playtime;
    bool m_show_location;
    bool m_show_renderer;
    bool m_show_task;
    bool m_show_task_name;

    int m_renderer_id;

    s64 m_start_timestamp;

    string128 m_details;
    string128 m_state;

    string128 m_location;
    string256 m_task;

    string128 m_menu_state;
    string128 m_exploring_state;
    string128 m_hidden_task_state;

    string64 m_large_image_key;
    string128 m_large_image_text;

    string64 m_renderer_image_key;
    string128 m_renderer_image_text;
};

extern ENGINE_API CDiscordRichPresence g_discord;
