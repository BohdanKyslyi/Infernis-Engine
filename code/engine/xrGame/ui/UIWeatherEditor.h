#pragma once

#include "UIDialogWnd.h"
#include "UIWndCallback.h"

class CUI3tButton;
class CUIComboBox;
class CUIStatic;
class CUITextWnd;
class CUITrackBar;
class CUIXml;
class CEnvDescriptor;
class CWeatherColorPicker;
class CWeatherColorSwatch;

class CUIWeatherEditor : public CUIDialogWnd, public CUIWndCallback {
    typedef CUIDialogWnd inherited;

public:
    CUIWeatherEditor();
    virtual ~CUIWeatherEditor();

    void Init();
    virtual void Show(bool status);
    virtual void Update();
    virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData = nullptr);
    virtual bool OnKeyboardAction(int dik, EUIMessages keyboard_action);

private:
    struct SPropertyControl {
        u32 property_index;
        CUITrackBar* slider;
        CUITextWnd* value;
    };

    struct SColorControl {
        u32 first_property_index;
        CWeatherColorSwatch* swatch;
    };

    struct SDescriptorState {
        CEnvDescriptor* descriptor;
        Fvector4 clouds_color;
        Fvector3 sky_color;
        float sky_rotation;
        float far_plane;
        Fvector3 fog_color;
        float fog_density;
        float fog_distance;
        float rain_density;
        Fvector3 rain_color;
        float bolt_period;
        float bolt_duration;
        float wind_velocity;
        float wind_direction;
        Fvector3 ambient;
        Fvector4 hemi_color;
        Fvector3 sun_color;
        Fvector3 sun_dir;
        float sun_shafts_intensity;
        float water_intensity;
        std::string sky_texture;
        std::string clouds_texture;
        std::string ambient_name;
        std::string sun_name;
        std::string thunderbolt_name;
    };

    void FillWeatherList();
    void FillFrameList();
    void FillTextureLists();
    void FillDefinitionLists();
    void CreatePropertyControls(CUIXml& xml);
    void AddPropertyHeader(CUIXml& xml, LPCSTR caption, float x, float y,
                           int first_color_property = -1);
    void AddPropertyControl(CUIXml& xml, u32 property_index, LPCSTR caption, float x,
                            float y);
    void SelectCurrentWeather();
    void SelectFrame(u32 index);
    void ApplyEditorTime(float game_time);
    void SyncTimeControl(float game_time);
    void SyncPropertyControls();
    void Snapshot(CEnvDescriptor* descriptor);
    SDescriptorState* FindSnapshot(CEnvDescriptor* descriptor);
    void RestoreSnapshot(SDescriptorState& state);
    void RefreshTextureSelection();
    void RefreshDefinitionSelection();
    void SetStatus(LPCSTR string_id, ...);

    float GetPropertyValue(u32 property_index) const;
    void SetPropertyValue(u32 property_index, float value);

    void OnWeatherChanged(CUIWindow*, void*);
    void OnFrameChanged(CUIWindow*, void*);
    void OnSkyChanged(CUIWindow*, void*);
    void OnCloudsChanged(CUIWindow*, void*);
    void OnAmbientChanged(CUIWindow*, void*);
    void OnSunChanged(CUIWindow*, void*);
    void OnThunderboltChanged(CUIWindow*, void*);
    void OnColorSwatch(CUIWindow*, void*);
    void OnColorPicked(CUIWindow*, void*);
    void OnAddFrame(CUIWindow*, void*);
    void OnPreview(CUIWindow*, void*);
    void OnSave(CUIWindow*, void*);
    void OnRevert(CUIWindow*, void*);
    void OnClose(CUIWindow*, void*);

private:
    CUITextWnd* m_title;
    CUITextWnd* m_status;
    CUIComboBox* m_weather;
    CUIComboBox* m_frame;
    CUIComboBox* m_sky;
    CUIComboBox* m_clouds;
    CUIComboBox* m_ambient_definition;
    CUIComboBox* m_sun_definition;
    CUIComboBox* m_thunderbolt_definition;
    CUITrackBar* m_time_slider;
    CUITextWnd* m_time_value;
    CUI3tButton* m_add_frame;
    CUI3tButton* m_preview;
    CUI3tButton* m_save;
    CUI3tButton* m_revert;
    CUI3tButton* m_close;
    CWeatherColorPicker* m_color_picker;

    xr_vector<std::string> m_weather_names;
    xr_vector<std::string> m_sky_textures;
    xr_vector<std::string> m_cloud_textures;
    xr_vector<std::string> m_ambient_definitions;
    xr_vector<std::string> m_sun_definitions;
    xr_vector<std::string> m_thunderbolt_definitions;
    xr_vector<SPropertyControl> m_property_controls;
    xr_vector<SColorControl> m_color_controls;
    xr_vector<SDescriptorState> m_snapshots;
    CEnvDescriptor* m_descriptor;
    float m_editor_time;
    int m_active_color_property;
    bool m_synchronizing;
    bool m_previous_pause;
};

void ToggleWeatherEditor(bool force_show = false, bool force_hide = false);
void DestroyWeatherEditor();
bool WeatherEditorShown();
