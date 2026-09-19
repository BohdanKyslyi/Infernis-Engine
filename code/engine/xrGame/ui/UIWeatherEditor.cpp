#include "stdafx.h"

#include "UIWeatherEditor.h"
#include "UI3tButton.h"
#include "UIBtnHint.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UIEditBox.h"
#include "UIHelper.h"
#include "UIStatic.h"
#include "UITextureMaster.h"
#include "UITrackBar.h"
#include "UIXmlInit.h"
#include "xrUIXmlParser.h"
#include "UICursor.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/xr_input.h"
#include "../game_cl_base.h"
#include "../Level.h"
#include "../string_table.h"
#include "xrRender/UIRender.h"

namespace {
class CWeatherTrackBar : public CUITrackBar {
public:
    void SetEditorValue(float value) {
        clamp(value, m_f_min, m_f_max);
        m_f_val = value;
        UpdatePos();
    }
};

class CWeatherComboBox : public CUIComboBox {
public:
    void ClearEditorList() { ClearList(); }
    void CloseEditorList() {
        ShowList(false);
        Device.seqRender.Remove(this);
    }
};

} // namespace

class CWeatherColorSwatch : public CUIStatic {
    typedef CUIStatic inherited;

public:
    CWeatherColorSwatch() { SetStretchTexture(true); }

    virtual void Draw() {
        inherited::Draw();

        // The swatch itself uses a neutral white texture, so its tint is the
        // literal RGB value. Draw a bright outline afterwards to keep black
        // and very dark colors visible over the transparent editor.
        Frect rect;
        GetAbsoluteRect(rect);
        UI().ClientToScreenScaled(rect.lt);
        UI().ClientToScreenScaled(rect.rb);
        UIRender->SetShader(*GetShader());
        UIRender->StartPrimitive(8, IUIRender::ptLineList, UI().m_currentPointType);
        PushBorderPoint(rect.x1, rect.y1);
        PushBorderPoint(rect.x2, rect.y1);
        PushBorderPoint(rect.x2, rect.y1);
        PushBorderPoint(rect.x2, rect.y2);
        PushBorderPoint(rect.x2, rect.y2);
        PushBorderPoint(rect.x1, rect.y2);
        PushBorderPoint(rect.x1, rect.y2);
        PushBorderPoint(rect.x1, rect.y1);
        UIRender->FlushPrimitive();
    }

    virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action) {
        inherited::OnMouseAction(x, y, mouse_action);
        if (mouse_action == WINDOW_LBUTTON_DOWN && CursorOverWindow()) {
            GetMessageTarget()->SendMessage(this, BUTTON_CLICKED, nullptr);
            return true;
        }
        return false;
    }

    void SetColor(float r, float g, float b) {
        r = clampr(r, 0.f, 1.f);
        g = clampr(g, 0.f, 1.f);
        b = clampr(b, 0.f, 1.f);
        SetTextureColor(color_rgba(iFloor(r * 255.f + .5f), iFloor(g * 255.f + .5f),
                                   iFloor(b * 255.f + .5f), 255));
    }

private:
    static void PushBorderPoint(float x, float y) {
        UIRender->PushPoint(x, y, 0.f, color_rgba(235, 235, 235, 255), .5f, .5f);
    }
};

class CWeatherColorPicker : public CUIWindow {
    typedef CUIWindow inherited;

public:
    CWeatherColorPicker()
        : m_hue(0.f), m_saturation(0.f), m_value(1.f), m_drag_mode(0) {
        // This 4x4 texture is pure opaque white. hud\\default multiplies it by
        // vertex color, producing an untinted palette on every renderer.
        m_shader->create("hud\\default", "ui\\ui_weather_editor_white");
    }

    virtual void Draw() {
        Frect rect;
        GetAbsoluteRect(rect);
        UI().ClientToScreenScaled(rect.lt);
        UI().ClientToScreenScaled(rect.rb);

        UIRender->SetShader(*m_shader);
        const float u = .5f;
        const float v = .5f;

        const float margin = ScaledX(6.f);
        const float gap = ScaledX(6.f);
        const float hue_width = ScaledX(18.f);
        Frect sv;
        sv.set(rect.x1 + margin, rect.y1 + ScaledY(6.f),
               rect.x2 - margin - gap - hue_width, rect.y2 - ScaledY(6.f));
        Frect hue;
        hue.set(sv.x2 + gap, sv.y1, rect.x2 - margin, sv.y2);

        UIRender->StartPrimitive(6 + 6 + 36, IUIRender::ptTriList, UI().m_currentPointType);
        PushQuad(rect, color_rgba(12, 15, 18, 230), color_rgba(12, 15, 18, 230),
                 color_rgba(12, 15, 18, 230), color_rgba(12, 15, 18, 230), u, v);

        float hr, hg, hb;
        HsvToRgb(m_hue, 1.f, 1.f, hr, hg, hb);
        const u32 hue_color = RgbColor(hr, hg, hb);
        PushQuad(sv, color_rgba(255, 255, 255, 255), hue_color,
                 color_rgba(0, 0, 0, 255), color_rgba(0, 0, 0, 255), u, v);

        static const u32 hue_colors[] = {
            color_rgba(255, 0, 0, 255),   color_rgba(255, 255, 0, 255),
            color_rgba(0, 255, 0, 255),   color_rgba(0, 255, 255, 255),
            color_rgba(0, 0, 255, 255),   color_rgba(255, 0, 255, 255),
            color_rgba(255, 0, 0, 255),
        };
        const float segment = hue.height() / 6.f;
        for (u32 i = 0; i < 6; ++i) {
            Frect part;
            part.set(hue.x1, hue.y1 + segment * i, hue.x2, hue.y1 + segment * (i + 1));
            PushQuad(part, hue_colors[i], hue_colors[i], hue_colors[i + 1],
                     hue_colors[i + 1], u, v);
        }
        UIRender->FlushPrimitive();

        DrawMarkers(sv, hue, u, v);
        inherited::Draw();
    }

    virtual void Update() {
        inherited::Update();
        if (m_drag_mode && !pInput->iGetAsyncBtnState(0))
            m_drag_mode = 0;
    }

    virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action) {
        inherited::OnMouseAction(x, y, mouse_action);
        if (mouse_action == WINDOW_LBUTTON_UP) {
            m_drag_mode = 0;
            return true;
        }
        if (mouse_action == WINDOW_LBUTTON_DOWN) {
            const Frect sv = SvRect();
            const Frect hue = HueRect();
            Fvector2 point;
            point.set(x, y);
            if (sv.in(point))
                m_drag_mode = 1;
            else if (hue.in(point))
                m_drag_mode = 2;
            else
                return true;
            UpdateFromMouse(x, y);
            return true;
        }
        if (mouse_action == WINDOW_MOUSE_MOVE && m_drag_mode && pInput->iGetAsyncBtnState(0)) {
            UpdateFromMouse(x, y);
            return true;
        }
        return true;
    }

    void SetColor(float r, float g, float b) {
        RgbToHsv(clampr(r, 0.f, 1.f), clampr(g, 0.f, 1.f), clampr(b, 0.f, 1.f),
                 m_hue, m_saturation, m_value);
    }

    void GetColor(float& r, float& g, float& b) const {
        HsvToRgb(m_hue, m_saturation, m_value, r, g, b);
    }

    bool IsDragging() const { return m_drag_mode != 0; }

private:
    static float ScaledX(float value) {
        Fvector2 result;
        UI().ClientToScreenScaled(result, value, 0.f);
        return result.x;
    }

    static float ScaledY(float value) {
        Fvector2 result;
        UI().ClientToScreenScaled(result, 0.f, value);
        return result.y;
    }

    Frect SvRect() const {
        Frect result;
        const float margin = 6.f;
        const float gap = 6.f;
        const float hue_width = 18.f;
        result.set(margin, 6.f, GetWidth() - margin - gap - hue_width, GetHeight() - 6.f);
        return result;
    }

    Frect HueRect() const {
        const Frect sv = SvRect();
        Frect result;
        result.set(sv.x2 + 6.f, sv.y1, GetWidth() - 6.f, sv.y2);
        return result;
    }

    void UpdateFromMouse(float x, float y) {
        if (m_drag_mode == 1) {
            const Frect sv = SvRect();
            m_saturation = clampr((x - sv.x1) / sv.width(), 0.f, 1.f);
            m_value = 1.f - clampr((y - sv.y1) / sv.height(), 0.f, 1.f);
        } else if (m_drag_mode == 2) {
            const Frect hue = HueRect();
            m_hue = clampr((y - hue.y1) / hue.height(), 0.f, 1.f);
            if (m_hue >= 1.f)
                m_hue = 0.f;
        }
        GetMessageTarget()->SendMessage(this, BUTTON_CLICKED, nullptr);
    }

    static u32 RgbColor(float r, float g, float b) {
        return color_rgba(iFloor(r * 255.f + .5f), iFloor(g * 255.f + .5f),
                          iFloor(b * 255.f + .5f), 255);
    }

    static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
        if (s <= EPS) {
            r = g = b = v;
            return;
        }
        h = h - std::floor(h);
        const float scaled = h * 6.f;
        const int sector = iFloor(scaled);
        const float fraction = scaled - sector;
        const float p = v * (1.f - s);
        const float q = v * (1.f - s * fraction);
        const float t = v * (1.f - s * (1.f - fraction));
        switch (sector % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }

    static void RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
        const float maximum = std::max(r, std::max(g, b));
        const float minimum = std::min(r, std::min(g, b));
        const float delta = maximum - minimum;
        v = maximum;
        s = maximum <= EPS ? 0.f : delta / maximum;
        if (delta <= EPS)
            return;
        if (maximum == r)
            h = (g - b) / delta + (g < b ? 6.f : 0.f);
        else if (maximum == g)
            h = (b - r) / delta + 2.f;
        else
            h = (r - g) / delta + 4.f;
        h /= 6.f;
    }

    static void PushQuad(const Frect& r, u32 lt, u32 rt, u32 lb, u32 rb, float u, float v) {
        UIRender->PushPoint(r.x1, r.y1, 0.f, lt, u, v);
        UIRender->PushPoint(r.x2, r.y1, 0.f, rt, u, v);
        UIRender->PushPoint(r.x2, r.y2, 0.f, rb, u, v);
        UIRender->PushPoint(r.x1, r.y1, 0.f, lt, u, v);
        UIRender->PushPoint(r.x2, r.y2, 0.f, rb, u, v);
        UIRender->PushPoint(r.x1, r.y2, 0.f, lb, u, v);
    }

    static void PushLineRect(const Frect& r, u32 color, float u, float v) {
        UIRender->PushPoint(r.x1, r.y1, 0.f, color, u, v);
        UIRender->PushPoint(r.x2, r.y1, 0.f, color, u, v);
        UIRender->PushPoint(r.x2, r.y1, 0.f, color, u, v);
        UIRender->PushPoint(r.x2, r.y2, 0.f, color, u, v);
        UIRender->PushPoint(r.x2, r.y2, 0.f, color, u, v);
        UIRender->PushPoint(r.x1, r.y2, 0.f, color, u, v);
        UIRender->PushPoint(r.x1, r.y2, 0.f, color, u, v);
        UIRender->PushPoint(r.x1, r.y1, 0.f, color, u, v);
    }

    void DrawMarkers(const Frect& sv, const Frect& hue, float u, float v) {
        const float marker_x = sv.x1 + sv.width() * m_saturation;
        const float marker_y = sv.y1 + sv.height() * (1.f - m_value);
        Frect outer;
        outer.set(marker_x - ScaledX(4.f), marker_y - ScaledY(4.f),
                  marker_x + ScaledX(4.f), marker_y + ScaledY(4.f));
        Frect hue_marker;
        hue_marker.set(hue.x1 - ScaledX(2.f), hue.y1 + hue.height() * m_hue - ScaledY(2.f),
                       hue.x2 + ScaledX(2.f), hue.y1 + hue.height() * m_hue + ScaledY(2.f));
        UIRender->StartPrimitive(16, IUIRender::ptLineList, UI().m_currentPointType);
        PushLineRect(outer, color_rgba(255, 255, 255, 255), u, v);
        PushLineRect(hue_marker, color_rgba(255, 255, 255, 255), u, v);
        UIRender->FlushPrimitive();
    }

private:
    ui_shader m_shader;
    float m_hue;
    float m_saturation;
    float m_value;
    int m_drag_mode;
};

namespace {

void clear_combo(CUIComboBox* combo) {
    static_cast<CWeatherComboBox*>(combo)->ClearEditorList();
}

struct SPropertyInfo {
    float minimum;
    float maximum;
    float step;
};

static const SPropertyInfo properties[] = {
    {0.f, 360.f, 1.f},      {50.f, 1000.f, 5.f},  {0.f, 1.f, .01f},
    {10.f, 1000.f, 5.f},    {0.f, 1.f, .01f},     {0.f, 100.f, 1.f},
    {0.f, 360.f, 1.f},      {0.f, 60.f, .25f},    {0.f, 5.f, .05f},
    {0.f, 1.f, .01f},       {0.f, 1.f, .01f},     {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.f, .01f},       {0.f, 1.5f, .01f},    {0.f, 1.5f, .01f},
    {0.f, 1.5f, .01f},      {-90.f, -.1f, .1f},   {-180.f, 180.f, 1.f},
};

static LPCSTR const property_hint_ids[] = {
    "ui_weather_editor_hint_sky_rotation",
    "ui_weather_editor_hint_far_plane",
    "ui_weather_editor_hint_fog_density",
    "ui_weather_editor_hint_fog_distance",
    "ui_weather_editor_hint_rain_density",
    "ui_weather_editor_hint_wind_velocity",
    "ui_weather_editor_hint_wind_direction",
    "ui_weather_editor_hint_thunderbolt_period",
    "ui_weather_editor_hint_thunderbolt_duration",
    "ui_weather_editor_hint_sun_shafts",
    "ui_weather_editor_hint_water_intensity",
    "ui_weather_editor_hint_sky_color",
    "ui_weather_editor_hint_sky_color",
    "ui_weather_editor_hint_sky_color",
    "ui_weather_editor_hint_clouds_color",
    "ui_weather_editor_hint_clouds_color",
    "ui_weather_editor_hint_clouds_color",
    "ui_weather_editor_hint_clouds_color",
    "ui_weather_editor_hint_fog_color",
    "ui_weather_editor_hint_fog_color",
    "ui_weather_editor_hint_fog_color",
    "ui_weather_editor_hint_rain_color",
    "ui_weather_editor_hint_rain_color",
    "ui_weather_editor_hint_rain_color",
    "ui_weather_editor_hint_ambient_color",
    "ui_weather_editor_hint_ambient_color",
    "ui_weather_editor_hint_ambient_color",
    "ui_weather_editor_hint_hemisphere_color",
    "ui_weather_editor_hint_hemisphere_color",
    "ui_weather_editor_hint_hemisphere_color",
    "ui_weather_editor_hint_hemisphere_color",
    "ui_weather_editor_hint_sun_color",
    "ui_weather_editor_hint_sun_color",
    "ui_weather_editor_hint_sun_color",
    "ui_weather_editor_hint_sun_altitude",
    "ui_weather_editor_hint_sun_longitude",
};

static_assert(sizeof(property_hint_ids) / sizeof(property_hint_ids[0]) ==
                  sizeof(properties) / sizeof(properties[0]),
              "Every weather property must have a learning hint");

constexpr float property_y_offset = 35.f;

CUIWeatherEditor* weather_editor = nullptr;

void append_unique(xr_vector<std::string>& values, const std::string& value) {
    if (value.empty())
        return;
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

bool ends_with(const std::string& value, LPCSTR suffix) {
    const size_t suffix_size = xr_strlen(suffix);
    return value.size() >= suffix_size &&
           value.compare(value.size() - suffix_size, suffix_size, suffix) == 0;
}
} // namespace

CUIWeatherEditor::CUIWeatherEditor()
    : m_title(nullptr), m_status(nullptr), m_weather(nullptr), m_frame(nullptr),
      m_sky(nullptr), m_clouds(nullptr),
      m_ambient_definition(nullptr), m_sun_definition(nullptr),
      m_thunderbolt_definition(nullptr), m_time_slider(nullptr), m_time_value(nullptr),
      m_add_frame(nullptr), m_new_weather_name(nullptr), m_create_weather(nullptr),
      m_learning_mode(nullptr), m_learning_mode_label(nullptr),
      m_preview(nullptr), m_save(nullptr), m_revert(nullptr),
      m_close(nullptr), m_color_picker(nullptr), m_descriptor(nullptr), m_editor_time(0.f),
      m_active_color_property(-1), m_synchronizing(false), m_previous_pause(false),
      m_session_active(false), m_previewing(false), m_time_update_pending(false),
      m_close_requested(false), m_learning_hint_owner(nullptr), m_learning_hint_start(0) {
    m_bWorkInPause = true;
}

CUIWeatherEditor::~CUIWeatherEditor() {}

void CUIWeatherEditor::Init() {
    CUIXml xml;
    xml.Load(CONFIG_PATH, UI_PATH, "weather_editor.xml");
    CUIXmlInit::InitWindow(xml, "main", 0, this);

    m_title = UIHelper::CreateTextWnd(xml, "main:title", this);
    m_status = UIHelper::CreateTextWnd(xml, "main:status", this);

    m_weather = xr_new<CWeatherComboBox>();
    m_weather->SetAutoDelete(true);
    AttachChild(m_weather);
    CUIXmlInit::InitComboBox(xml, "main:weather", 0, m_weather);

    m_frame = xr_new<CWeatherComboBox>();
    m_frame->SetAutoDelete(true);
    AttachChild(m_frame);
    CUIXmlInit::InitComboBox(xml, "main:frame", 0, m_frame);

    m_sky = xr_new<CWeatherComboBox>();
    m_sky->SetAutoDelete(true);
    AttachChild(m_sky);
    CUIXmlInit::InitComboBox(xml, "main:sky", 0, m_sky);

    m_clouds = xr_new<CWeatherComboBox>();
    m_clouds->SetAutoDelete(true);
    AttachChild(m_clouds);
    CUIXmlInit::InitComboBox(xml, "main:clouds", 0, m_clouds);

    m_ambient_definition = xr_new<CWeatherComboBox>();
    m_ambient_definition->SetAutoDelete(true);
    AttachChild(m_ambient_definition);
    CUIXmlInit::InitComboBox(xml, "main:ambient_definition", 0, m_ambient_definition);

    m_sun_definition = xr_new<CWeatherComboBox>();
    m_sun_definition->SetAutoDelete(true);
    AttachChild(m_sun_definition);
    CUIXmlInit::InitComboBox(xml, "main:sun_definition", 0, m_sun_definition);

    m_thunderbolt_definition = xr_new<CWeatherComboBox>();
    m_thunderbolt_definition->SetAutoDelete(true);
    AttachChild(m_thunderbolt_definition);
    CUIXmlInit::InitComboBox(xml, "main:thunderbolt_definition", 0,
                            m_thunderbolt_definition);

    m_time_slider = xr_new<CWeatherTrackBar>();
    m_time_slider->SetAutoDelete(true);
    AttachChild(m_time_slider);
    CUIXmlInit::InitTrackBar(xml, "main:time_slider", 0, m_time_slider);
    m_time_slider->SetOptFBounds(0.f, DAY_LENGTH - 1.f);
    m_time_slider->SetStep(60.f);
    m_time_value = UIHelper::CreateTextWnd(xml, "main:time_value", this);

    CreatePropertyControls(xml);

    m_add_frame = UIHelper::Create3tButton(xml, "main:add_frame", this);
    m_new_weather_name = UIHelper::CreateEditBox(xml, "main:new_weather_name", this);
    m_create_weather = UIHelper::Create3tButton(xml, "main:create_weather", this);
    m_learning_mode = UIHelper::CreateCheck(xml, "main:learning_mode", this);
    m_learning_mode->SetCheck(false);
    m_learning_mode_label =
        UIHelper::CreateTextWnd(xml, "main:learning_mode_label", this);
    m_preview = UIHelper::Create3tButton(xml, "main:preview", this);
    m_save = UIHelper::Create3tButton(xml, "main:save", this);
    m_revert = UIHelper::Create3tButton(xml, "main:revert", this);
    m_close = UIHelper::Create3tButton(xml, "main:close", this);

    // One shared popup keeps the layout compact. Every color header opens it
    // for its own RGB triplet; RGBA alpha remains on the dedicated A slider.
    m_color_picker = xr_new<CWeatherColorPicker>();
    m_color_picker->SetAutoDelete(true);
    m_color_picker->SetWndSize(Fvector2().set(184.f, 126.f));
    m_color_picker->SetWndPos(Fvector2().set(0.f, 0.f));
    AttachChild(m_color_picker);
    m_color_picker->Show(false);

    Register(m_weather);
    Register(m_frame);
    Register(m_time_slider);
    Register(m_sky);
    Register(m_clouds);
    Register(m_ambient_definition);
    Register(m_sun_definition);
    Register(m_thunderbolt_definition);
    Register(m_add_frame);
    Register(m_create_weather);
    Register(m_learning_mode);
    Register(m_preview);
    Register(m_save);
    Register(m_revert);
    Register(m_close);
    Register(m_color_picker);
    for (SColorControl& control : m_color_controls)
        Register(control.swatch);
    for (SPropertyControl& control : m_property_controls)
        Register(control.slider);

    AddCallback(m_weather, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnWeatherChanged));
    AddCallback(m_frame, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnFrameChanged));
    AddCallback(m_time_slider, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnTimeChanged));
    AddCallback(m_sky, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnSkyChanged));
    AddCallback(m_clouds, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnCloudsChanged));
    AddCallback(m_ambient_definition, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnAmbientChanged));
    AddCallback(m_sun_definition, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnSunChanged));
    AddCallback(m_thunderbolt_definition, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnThunderboltChanged));
    AddCallback(m_add_frame, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnAddFrame));
    AddCallback(m_create_weather, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnCreateWeather));
    AddCallback(m_preview, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnPreview));
    AddCallback(m_save, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnSave));
    AddCallback(m_revert, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnRevert));
    AddCallback(m_close, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnClose));
    AddCallback(m_color_picker, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIWeatherEditor::OnColorPicked));
    for (SColorControl& control : m_color_controls)
        AddCallback(control.swatch, BUTTON_CLICKED,
                    CUIWndCallback::void_function(this, &CUIWeatherEditor::OnColorSwatch));
    for (SPropertyControl& control : m_property_controls)
        AddCallback(control.slider, BUTTON_CLICKED,
                    CUIWndCallback::void_function(this, &CUIWeatherEditor::OnPropertyChanged));

    SetStatus("ui_weather_editor_status_ready");
}

void CUIWeatherEditor::Show(bool status) {
    if (status && !IsShown()) {
        CEnvironment& environment = g_pGamePersistent->Environment();
        if (m_session_active) {
            m_previewing = false;
            environment.UpdateWeatherEditorSession(environment.CurrentCycleName, m_editor_time);
            inherited::Show(true);
            SyncTimeControl(m_editor_time);
            SyncPropertyControls();
            return;
        }

        const float current_time = environment.GetGameTime();
        m_previous_pause = environment.m_paused;
        m_session_active = true;
        m_previewing = false;
        environment.BeginWeatherEditorSession(environment.CurrentCycleName, current_time);
        m_color_picker->Show(false);
        m_active_color_property = -1;
        m_close_requested = false;
        m_time_update_pending = false;
        inherited::Show(true);
        FillWeatherList();
        FillTextureLists();
        FillDefinitionLists();
        SelectCurrentWeather();
        return;
    }
    if (!status && IsShown()) {
        ClearLearningHint();
        m_color_picker->Show(false);
        m_active_color_property = -1;
        if (g_pGamePersistent && !m_previewing) {
            CEnvironment& environment = g_pGamePersistent->Environment();
            environment.EndWeatherEditorSession();
            environment.m_paused = m_previous_pause;
            m_session_active = false;
        }

        // An expanded combo registers itself in seqRender and captures the
        // parent. Close every list before hiding/deleting the dialog.
        static_cast<CWeatherComboBox*>(m_weather)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_frame)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_sky)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_clouds)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_ambient_definition)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_sun_definition)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_thunderbolt_definition)->CloseEditorList();
    }
    inherited::Show(status);
}

void CUIWeatherEditor::Update() {
    inherited::Update();
    if (m_close_requested) {
        m_close_requested = false;
        CloseEditorSession();
        return;
    }
    if (!m_descriptor || m_synchronizing) {
        UpdateLearningHint();
        return;
    }

    if (m_time_update_pending) {
        m_time_update_pending = false;
        const float requested_time = m_time_slider->GetFValue();
        if (std::abs(requested_time - m_editor_time) >= 1.f)
            ApplyEditorTime(requested_time);
    }

    const u32 total_seconds = (u32)iFloor(m_editor_time + .5f);
    string32 time_text;
    xr_sprintf(time_text, "%02u:%02u:%02u", total_seconds / 3600,
               (total_seconds % 3600) / 60, total_seconds % 60);
    m_time_value->SetText(time_text);

    for (SPropertyControl& control : m_property_controls) {
        const float value = control.slider->GetFValue();
        if (!fsimilar(value, GetPropertyValue(control.property_index)))
            SetPropertyValue(control.property_index, value);

        string32 value_text;
        xr_sprintf(value_text, "%.3f", GetPropertyValue(control.property_index));
        control.value->SetText(value_text);
    }

    for (SColorControl& control : m_color_controls) {
        control.swatch->SetColor(GetPropertyValue(control.first_property_index),
                                 GetPropertyValue(control.first_property_index + 1),
                                 GetPropertyValue(control.first_property_index + 2));
    }
    if (m_color_picker->IsShown() && m_active_color_property >= 0 &&
        !m_color_picker->IsDragging()) {
        m_color_picker->SetColor(GetPropertyValue((u32)m_active_color_property),
                                 GetPropertyValue((u32)m_active_color_property + 1),
                                 GetPropertyValue((u32)m_active_color_property + 2));
    }
    UpdateLearningHint();
}

void CUIWeatherEditor::SendMessage(CUIWindow* pWnd, s16 msg, void* pData) {
    inherited::SendMessage(pWnd, msg, pData);
    CUIWndCallback::OnEvent(pWnd, msg, pData);
}

bool CUIWeatherEditor::OnKeyboardAction(int dik, EUIMessages keyboard_action) {
    if (keyboard_action == WINDOW_KEY_PRESSED) {
        if (dik == DIK_ESCAPE) {
            m_close_requested = true;
            return true;
        }
        if (dik == DIK_F5) {
            OnRevert(nullptr, nullptr);
            return true;
        }
        if (dik == DIK_F6) {
            OnSave(nullptr, nullptr);
            return true;
        }
        if (dik == DIK_F7) {
            OnPreview(nullptr, nullptr);
            return true;
        }
    }
    return inherited::OnKeyboardAction(dik, keyboard_action);
}

void CUIWeatherEditor::FillWeatherList() {
    m_synchronizing = true;
    clear_combo(m_weather);
    m_weather_names.clear();
    const CEnvironment::EnvsMap& cycles = g_pGamePersistent->Environment().WeatherCycles;
    for (const auto& cycle : cycles) {
        const int index = (int)m_weather_names.size();
        m_weather_names.push_back(cycle.first);
        m_weather->AddItem_(cycle.first.c_str(), index);
    }
    m_synchronizing = false;
}

void CUIWeatherEditor::FillFrameList() {
    m_synchronizing = true;
    clear_combo(m_frame);
    CEnvironment& environment = g_pGamePersistent->Environment();
    const auto cycle = environment.WeatherCycles.find(environment.CurrentCycleName);
    if (cycle != environment.WeatherCycles.end()) {
        for (u32 i = 0; i < cycle->second.size(); ++i)
            m_frame->AddItem_(cycle->second[i]->m_identifier.c_str(), (int)i);
    }
    m_synchronizing = false;
}

void CUIWeatherEditor::FillTextureLists() {
    m_sky_textures.clear();
    m_cloud_textures.clear();

    FS_FileSet files;
    FS.file_list(files, "$game_textures$", FS_ListFiles | FS_ClampExt, "sky\\*.dds");
    for (const FS_File& file : files) {
        std::string texture = file.name.c_str();
        std::replace(texture.begin(), texture.end(), '/', '\\');
        if (ends_with(texture, "#small"))
            continue;

        string_path small_texture;
        strconcat(sizeof(small_texture), small_texture, texture.c_str(), "#small");
        string_path found;
        if (FS.exist(found, "$game_textures$", small_texture, ".dds"))
            append_unique(m_sky_textures, texture);
        else
            append_unique(m_cloud_textures, texture);
    }

    // Always expose textures already referenced by weather configs. This also
    // supports non-standard mod folders and naming conventions.
    for (const auto& cycle : g_pGamePersistent->Environment().WeatherCycles) {
        for (const CEnvDescriptor* descriptor : cycle.second) {
            append_unique(m_sky_textures, descriptor->sky_texture_name);
            append_unique(m_cloud_textures, descriptor->clouds_texture_name);
        }
    }

    std::sort(m_sky_textures.begin(), m_sky_textures.end());
    std::sort(m_cloud_textures.begin(), m_cloud_textures.end());
    clear_combo(m_sky);
    clear_combo(m_clouds);
    for (u32 i = 0; i < m_sky_textures.size(); ++i)
        m_sky->AddItem_(m_sky_textures[i].c_str(), (int)i);
    for (u32 i = 0; i < m_cloud_textures.size(); ++i)
        m_clouds->AddItem_(m_cloud_textures[i].c_str(), (int)i);
}

void CUIWeatherEditor::FillDefinitionLists() {
    CEnvironment& environment = g_pGamePersistent->Environment();
    m_ambient_definitions.clear();
    m_sun_definitions.clear();
    m_thunderbolt_definitions.clear();
    m_ambient_definitions.push_back("");
    m_sun_definitions.push_back("");
    m_thunderbolt_definitions.push_back("");

    const auto append_sections = [](CInifile* config, xr_vector<std::string>& destination) {
        if (!config)
            return;
        for (const CInifile::Sect* section : config->sections())
            append_unique(destination, *section->Name);
    };

    append_sections(environment.m_ambients_config, m_ambient_definitions);
    append_sections(environment.m_suns_config, m_sun_definitions);
    append_sections(environment.m_thunderbolt_collections_config,
                    m_thunderbolt_definitions);

    // Keep custom definitions referenced by loaded weather cycles visible even
    // when a mod builds its lists through includes or non-standard files.
    for (const auto& cycle : environment.WeatherCycles) {
        for (const CEnvDescriptor* descriptor : cycle.second) {
            if (descriptor->env_ambient)
                append_unique(m_ambient_definitions, descriptor->env_ambient->name());
            append_unique(m_sun_definitions, descriptor->lens_flare_id);
            append_unique(m_thunderbolt_definitions, descriptor->tb_id);
        }
    }

    std::sort(m_ambient_definitions.begin() + 1, m_ambient_definitions.end());
    std::sort(m_sun_definitions.begin() + 1, m_sun_definitions.end());
    std::sort(m_thunderbolt_definitions.begin() + 1, m_thunderbolt_definitions.end());

    clear_combo(m_ambient_definition);
    clear_combo(m_sun_definition);
    clear_combo(m_thunderbolt_definition);
    for (u32 i = 0; i < m_ambient_definitions.size(); ++i)
        m_ambient_definition->AddItem_(
            i ? m_ambient_definitions[i].c_str()
              : CStringTable().translate("ui_weather_editor_none").c_str(),
            (int)i);
    for (u32 i = 0; i < m_sun_definitions.size(); ++i)
        m_sun_definition->AddItem_(
            i ? m_sun_definitions[i].c_str()
              : CStringTable().translate("ui_weather_editor_none").c_str(),
            (int)i);
    for (u32 i = 0; i < m_thunderbolt_definitions.size(); ++i)
        m_thunderbolt_definition->AddItem_(
            i ? m_thunderbolt_definitions[i].c_str()
              : CStringTable().translate("ui_weather_editor_none").c_str(),
            (int)i);
}

void CUIWeatherEditor::AddPropertyHeader(CUIXml& xml, LPCSTR caption, float x, float y,
                                         int first_color_property) {
    CUITextWnd* header = UIHelper::CreateTextWnd(xml, "main:property_header_template", this);
    header->SetWndPos(Fvector2().set(x, y + property_y_offset));
    header->SetTextST(caption);

    if (first_color_property >= 0) {
        CWeatherColorSwatch* swatch = xr_new<CWeatherColorSwatch>();
        swatch->SetAutoDelete(true);
        swatch->SetWndPos(Fvector2().set(x + 260.f, y + property_y_offset + 1.f));
        swatch->SetWndSize(Fvector2().set(46.f, 14.f));
        swatch->InitTexture("ui\\ui_weather_editor_white");
        AttachChild(swatch);

        SColorControl control;
        control.first_property_index = (u32)first_color_property;
        control.header = header;
        control.swatch = swatch;
        m_color_controls.push_back(control);
    }
}

void CUIWeatherEditor::AddPropertyControl(CUIXml& xml, u32 property_index, LPCSTR caption,
                                          float x, float y) {
    VERIFY(property_index < sizeof(properties) / sizeof(properties[0]));

    CUITextWnd* label = UIHelper::CreateTextWnd(xml, "main:property_label_template", this);
    label->SetWndPos(Fvector2().set(x, y + property_y_offset));
    label->SetTextST(caption);

    CWeatherTrackBar* slider = xr_new<CWeatherTrackBar>();
    slider->SetAutoDelete(true);
    AttachChild(slider);
    CUIXmlInit::InitTrackBar(xml, "main:property_slider_template", 0, slider);
    slider->SetWndPos(Fvector2().set(x + 104.f, y + property_y_offset + 2.f));
    slider->SetOptFBounds(properties[property_index].minimum, properties[property_index].maximum);
    slider->SetStep(properties[property_index].step);

    CUITextWnd* value = UIHelper::CreateTextWnd(xml, "main:property_value_template", this);
    value->SetWndPos(Fvector2().set(x + 251.f, y + property_y_offset));

    SPropertyControl control;
    control.property_index = property_index;
    control.label = label;
    control.slider = slider;
    control.value = value;
    m_property_controls.push_back(control);
}

void CUIWeatherEditor::CreatePropertyControls(CUIXml& xml) {
    const float left = 8.f;
    const float center = 336.f;
    const float right = 664.f;

    // Atmosphere
    AddPropertyHeader(xml, "ui_weather_editor_ambient_color_rgb", left, 124.f, 24);
    AddPropertyControl(xml, 24, "ui_weather_editor_channel_r", left, 144.f);
    AddPropertyControl(xml, 25, "ui_weather_editor_channel_g", left, 163.f);
    AddPropertyControl(xml, 26, "ui_weather_editor_channel_b", left, 182.f);

    AddPropertyHeader(xml, "ui_weather_editor_fog_color_rgb", left, 207.f, 18);
    AddPropertyControl(xml, 18, "ui_weather_editor_channel_r", left, 227.f);
    AddPropertyControl(xml, 19, "ui_weather_editor_channel_g", left, 246.f);
    AddPropertyControl(xml, 20, "ui_weather_editor_channel_b", left, 265.f);
    AddPropertyControl(xml, 2, "ui_weather_editor_fog_density", left, 292.f);
    AddPropertyControl(xml, 3, "ui_weather_editor_fog_distance", left, 311.f);
    AddPropertyControl(xml, 1, "ui_weather_editor_far_plane", left, 330.f);
    AddPropertyControl(xml, 10, "ui_weather_editor_water_intensity", left, 349.f);

    // Sky and clouds
    AddPropertyHeader(xml, "ui_weather_editor_sky_color_rgb", center, 124.f, 11);
    AddPropertyControl(xml, 11, "ui_weather_editor_channel_r", center, 144.f);
    AddPropertyControl(xml, 12, "ui_weather_editor_channel_g", center, 163.f);
    AddPropertyControl(xml, 13, "ui_weather_editor_channel_b", center, 182.f);

    AddPropertyHeader(xml, "ui_weather_editor_clouds_color_rgba", center, 207.f, 14);
    AddPropertyControl(xml, 14, "ui_weather_editor_channel_r", center, 227.f);
    AddPropertyControl(xml, 15, "ui_weather_editor_channel_g", center, 246.f);
    AddPropertyControl(xml, 16, "ui_weather_editor_channel_b", center, 265.f);
    AddPropertyControl(xml, 17, "ui_weather_editor_channel_a", center, 284.f);

    AddPropertyHeader(xml, "ui_weather_editor_hemisphere_color_rgba", center, 309.f, 27);
    AddPropertyControl(xml, 27, "ui_weather_editor_channel_r", center, 329.f);
    AddPropertyControl(xml, 28, "ui_weather_editor_channel_g", center, 348.f);
    AddPropertyControl(xml, 29, "ui_weather_editor_channel_b", center, 367.f);
    AddPropertyControl(xml, 30, "ui_weather_editor_channel_a", center, 386.f);
    AddPropertyControl(xml, 0, "ui_weather_editor_sky_rotation", center, 413.f);
    AddPropertyControl(xml, 9, "ui_weather_editor_sun_shafts", center, 432.f);

    // Weather, sun and wind
    AddPropertyHeader(xml, "ui_weather_editor_rain_color_rgb", right, 124.f, 21);
    AddPropertyControl(xml, 21, "ui_weather_editor_channel_r", right, 144.f);
    AddPropertyControl(xml, 22, "ui_weather_editor_channel_g", right, 163.f);
    AddPropertyControl(xml, 23, "ui_weather_editor_channel_b", right, 182.f);
    AddPropertyControl(xml, 4, "ui_weather_editor_rain_density", right, 207.f);

    AddPropertyHeader(xml, "ui_weather_editor_sun_color_rgb", right, 234.f, 31);
    AddPropertyControl(xml, 31, "ui_weather_editor_channel_r", right, 254.f);
    AddPropertyControl(xml, 32, "ui_weather_editor_channel_g", right, 273.f);
    AddPropertyControl(xml, 33, "ui_weather_editor_channel_b", right, 292.f);
    AddPropertyControl(xml, 34, "ui_weather_editor_sun_altitude", right, 319.f);
    AddPropertyControl(xml, 35, "ui_weather_editor_sun_longitude", right, 338.f);

    AddPropertyHeader(xml, "ui_weather_editor_wind", right, 365.f);
    AddPropertyControl(xml, 5, "ui_weather_editor_wind_velocity", right, 385.f);
    AddPropertyControl(xml, 6, "ui_weather_editor_wind_direction", right, 404.f);

    AddPropertyHeader(xml, "ui_weather_editor_thunderbolt", right, 431.f);
    AddPropertyControl(xml, 7, "ui_weather_editor_thunderbolt_period", right, 451.f);
    AddPropertyControl(xml, 8, "ui_weather_editor_thunderbolt_duration", right, 470.f);
}

void CUIWeatherEditor::SelectCurrentWeather() {
    if (!g_pGamePersistent)
        return;
    CEnvironment& environment = g_pGamePersistent->Environment();
    u32 weather_index = 0;
    for (; weather_index < m_weather_names.size(); ++weather_index) {
        if (m_weather_names[weather_index] == environment.CurrentCycleName)
            break;
    }
    if (weather_index < m_weather_names.size())
        m_weather->SetItemIDX((int)weather_index);

    FillFrameList();
    u32 frame_index = 0;
    const auto cycle = environment.WeatherCycles.find(environment.CurrentCycleName);
    if (cycle != environment.WeatherCycles.end() && environment.Current[0]) {
        const auto found = std::find(cycle->second.begin(), cycle->second.end(), environment.Current[0]);
        if (found != cycle->second.end())
            frame_index = (u32)std::distance(cycle->second.begin(), found);
    }
    SelectFrame(frame_index);
}

void CUIWeatherEditor::SelectFrame(u32 index) {
    CEnvironment& environment = g_pGamePersistent->Environment();
    const auto cycle = environment.WeatherCycles.find(environment.CurrentCycleName);
    if (cycle == environment.WeatherCycles.end() || index >= cycle->second.size())
        return;

    m_descriptor = cycle->second[index];
    Snapshot(m_descriptor);
    m_synchronizing = true;
    m_frame->SetItemIDX((int)index);
    m_synchronizing = false;

    ApplyEditorTime(m_descriptor->exec_time);
    SyncTimeControl(m_descriptor->exec_time);

    RefreshTextureSelection();
    RefreshDefinitionSelection();
    SyncPropertyControls();
    SetStatus("ui_weather_editor_status_editing", environment.CurrentCycleName.c_str(),
              m_descriptor->m_identifier.c_str());
}

void CUIWeatherEditor::ApplyEditorTime(float game_time) {
    if (!g_pGamePersistent)
        return;

    CEnvironment& environment = g_pGamePersistent->Environment();
    const float normalized_time = clampr(game_time, 0.f, DAY_LENGTH - 1.f);
    environment.UpdateWeatherEditorSession(environment.CurrentCycleName, normalized_time);
    m_editor_time = normalized_time;
}

void CUIWeatherEditor::SyncTimeControl(float game_time) {
    m_editor_time = clampr(game_time, 0.f, DAY_LENGTH - 1.f);
    static_cast<CWeatherTrackBar*>(m_time_slider)->SetEditorValue(m_editor_time);
}

void CUIWeatherEditor::SyncPropertyControls() {
    if (!m_descriptor)
        return;
    m_synchronizing = true;
    for (SPropertyControl& control : m_property_controls) {
        const SPropertyInfo& property = properties[control.property_index];
        const float maximum = control.property_index == 3
                                  ? std::min(property.maximum, m_descriptor->far_plane)
                                  : property.maximum;
        control.slider->SetOptFBounds(property.minimum, maximum);
        control.slider->SetStep(property.step);
        static_cast<CWeatherTrackBar*>(control.slider)
            ->SetEditorValue(GetPropertyValue(control.property_index));

        string32 value_text;
        xr_sprintf(value_text, "%.3f", GetPropertyValue(control.property_index));
        control.value->SetText(value_text);
    }
    for (SColorControl& control : m_color_controls)
        control.swatch->SetColor(GetPropertyValue(control.first_property_index),
                                 GetPropertyValue(control.first_property_index + 1),
                                 GetPropertyValue(control.first_property_index + 2));
    if (m_color_picker->IsShown() && m_active_color_property >= 0 &&
        !m_color_picker->IsDragging())
        m_color_picker->SetColor(GetPropertyValue((u32)m_active_color_property),
                                 GetPropertyValue((u32)m_active_color_property + 1),
                                 GetPropertyValue((u32)m_active_color_property + 2));
    m_synchronizing = false;
}

void CUIWeatherEditor::Snapshot(CEnvDescriptor* descriptor) {
    if (!descriptor || FindSnapshot(descriptor))
        return;
    SDescriptorState state;
    state.descriptor = descriptor;
    state.clouds_color = descriptor->clouds_color;
    state.sky_color = descriptor->sky_color;
    state.sky_rotation = descriptor->sky_rotation;
    state.far_plane = descriptor->far_plane;
    state.fog_color = descriptor->fog_color;
    state.fog_density = descriptor->fog_density;
    state.fog_distance = descriptor->fog_distance;
    state.rain_density = descriptor->rain_density;
    state.rain_color = descriptor->rain_color;
    state.bolt_period = descriptor->bolt_period;
    state.bolt_duration = descriptor->bolt_duration;
    state.wind_velocity = descriptor->wind_velocity;
    state.wind_direction = descriptor->wind_direction;
    state.ambient = descriptor->ambient;
    state.hemi_color = descriptor->hemi_color;
    state.sun_color = descriptor->sun_color;
    state.sun_dir = descriptor->sun_dir;
    state.sun_shafts_intensity = descriptor->m_fSunShaftsIntensity;
    state.water_intensity = descriptor->m_fWaterIntensity;
    state.sky_texture = descriptor->sky_texture_name;
    state.clouds_texture = descriptor->clouds_texture_name;
    state.ambient_name = descriptor->env_ambient ? descriptor->env_ambient->name().c_str() : "";
    state.sun_name = descriptor->lens_flare_id;
    state.thunderbolt_name = descriptor->tb_id;
    m_snapshots.push_back(state);
}

CUIWeatherEditor::SDescriptorState* CUIWeatherEditor::FindSnapshot(CEnvDescriptor* descriptor) {
    for (SDescriptorState& state : m_snapshots) {
        if (state.descriptor == descriptor)
            return &state;
    }
    return nullptr;
}

void CUIWeatherEditor::RestoreSnapshot(SDescriptorState& state) {
    CEnvDescriptor& descriptor = *state.descriptor;
    descriptor.set_sky_texture(state.sky_texture.c_str());
    descriptor.set_clouds_texture(state.clouds_texture.c_str());
    CEnvironment& environment = g_pGamePersistent->Environment();
    descriptor.set_ambient(environment, state.ambient_name.c_str());
    descriptor.set_sun(environment, state.sun_name.c_str());
    descriptor.set_thunderbolt_collection(environment, state.thunderbolt_name.c_str());
    descriptor.clouds_color = state.clouds_color;
    descriptor.sky_color = state.sky_color;
    descriptor.sky_rotation = state.sky_rotation;
    descriptor.far_plane = state.far_plane;
    descriptor.fog_color = state.fog_color;
    descriptor.fog_density = state.fog_density;
    descriptor.fog_distance = state.fog_distance;
    descriptor.rain_density = state.rain_density;
    descriptor.rain_color = state.rain_color;
    descriptor.bolt_period = state.bolt_period;
    descriptor.bolt_duration = state.bolt_duration;
    descriptor.wind_velocity = state.wind_velocity;
    descriptor.wind_direction = state.wind_direction;
    descriptor.ambient = state.ambient;
    descriptor.hemi_color = state.hemi_color;
    descriptor.sun_color = state.sun_color;
    descriptor.sun_dir = state.sun_dir;
    descriptor.m_fSunShaftsIntensity = state.sun_shafts_intensity;
    descriptor.m_fWaterIntensity = state.water_intensity;
}

void CUIWeatherEditor::RefreshTextureSelection() {
    if (!m_descriptor)
        return;
    m_synchronizing = true;
    const auto sky = std::find(m_sky_textures.begin(), m_sky_textures.end(),
                               m_descriptor->sky_texture_name);
    if (sky != m_sky_textures.end())
        m_sky->SetItemIDX((int)std::distance(m_sky_textures.begin(), sky));
    const auto clouds = std::find(m_cloud_textures.begin(), m_cloud_textures.end(),
                                  m_descriptor->clouds_texture_name);
    if (clouds != m_cloud_textures.end())
        m_clouds->SetItemIDX((int)std::distance(m_cloud_textures.begin(), clouds));
    m_synchronizing = false;
}

void CUIWeatherEditor::RefreshDefinitionSelection() {
    if (!m_descriptor)
        return;

    m_synchronizing = true;
    const std::string ambient_name =
        m_descriptor->env_ambient ? m_descriptor->env_ambient->name().c_str() : "";
    const auto ambient = std::find(m_ambient_definitions.begin(), m_ambient_definitions.end(),
                                   ambient_name);
    if (ambient != m_ambient_definitions.end())
        m_ambient_definition->SetItemIDX(
            (int)std::distance(m_ambient_definitions.begin(), ambient));

    const auto sun = std::find(m_sun_definitions.begin(), m_sun_definitions.end(),
                               m_descriptor->lens_flare_id);
    if (sun != m_sun_definitions.end())
        m_sun_definition->SetItemIDX((int)std::distance(m_sun_definitions.begin(), sun));

    const auto thunderbolt =
        std::find(m_thunderbolt_definitions.begin(), m_thunderbolt_definitions.end(),
                  m_descriptor->tb_id);
    if (thunderbolt != m_thunderbolt_definitions.end())
        m_thunderbolt_definition->SetItemIDX(
            (int)std::distance(m_thunderbolt_definitions.begin(), thunderbolt));
    m_synchronizing = false;
}

void CUIWeatherEditor::ClearLearningHint() {
    if (g_statHint && m_learning_hint_owner &&
        g_statHint->Owner() == m_learning_hint_owner)
        g_statHint->Discard();
    m_learning_hint_owner = nullptr;
    m_learning_hint_start = 0;
}

void CUIWeatherEditor::UpdateLearningHint() {
    if (!g_statHint || !m_learning_mode || !m_learning_mode_label) {
        ClearLearningHint();
        return;
    }

    CUIWindow* hovered = nullptr;
    LPCSTR hint_id = nullptr;
    bool immediate = false;
    const auto use_hint = [&hovered, &hint_id, &immediate](CUIWindow* window, LPCSTR id,
                                                          bool show_immediately) {
        if (!hovered && window && window->CursorOverWindow()) {
            hovered = window;
            hint_id = id;
            immediate = show_immediately;
        }
    };

    // The learning-mode control explains itself even while the mode is off.
    use_hint(m_learning_mode, "ui_weather_editor_hint_learning_mode", true);
    use_hint(m_learning_mode_label, "ui_weather_editor_hint_learning_mode", true);

    if (!hovered && m_learning_mode->GetCheck()) {
        if (m_color_picker->IsShown())
            use_hint(m_color_picker, "ui_weather_editor_hint_color_picker", false);
        use_hint(m_title, "ui_weather_editor_hint_title", false);
        use_hint(m_status, "ui_weather_editor_hint_status", false);
        use_hint(m_weather, "ui_weather_editor_hint_weather", false);
        use_hint(m_frame, "ui_weather_editor_hint_frame", false);
        use_hint(m_sky, "ui_weather_editor_hint_sky_texture", false);
        use_hint(m_clouds, "ui_weather_editor_hint_clouds_texture", false);
        use_hint(m_ambient_definition, "ui_weather_editor_hint_ambient_definition", false);
        use_hint(m_sun_definition, "ui_weather_editor_hint_sun_definition", false);
        use_hint(m_thunderbolt_definition,
                 "ui_weather_editor_hint_thunderbolt_definition", false);
        use_hint(m_time_slider, "ui_weather_editor_hint_time", false);
        use_hint(m_time_value, "ui_weather_editor_hint_time", false);
        use_hint(m_new_weather_name, "ui_weather_editor_hint_new_file", false);
        use_hint(m_create_weather, "ui_weather_editor_hint_create_file", false);
        use_hint(m_add_frame, "ui_weather_editor_hint_add_section", false);
        use_hint(m_preview, "ui_weather_editor_hint_preview", false);
        use_hint(m_save, "ui_weather_editor_hint_save", false);
        use_hint(m_revert, "ui_weather_editor_hint_revert", false);
        use_hint(m_close, "ui_weather_editor_hint_close", false);

        for (const SColorControl& control : m_color_controls) {
            use_hint(control.header, property_hint_ids[control.first_property_index], false);
            use_hint(control.swatch, property_hint_ids[control.first_property_index], false);
        }
        for (const SPropertyControl& control : m_property_controls) {
            use_hint(control.label, property_hint_ids[control.property_index], false);
            use_hint(control.slider, property_hint_ids[control.property_index], false);
            use_hint(control.value, property_hint_ids[control.property_index], false);
        }
    }

    if (!hovered) {
        ClearLearningHint();
        return;
    }

    if (hovered != m_learning_hint_owner) {
        ClearLearningHint();
        m_learning_hint_owner = hovered;
        m_learning_hint_start = Device.dwTimeGlobal;
        if (!immediate)
            return;
    }

    if (!immediate && Device.dwTimeGlobal < m_learning_hint_start + 350)
        return;

    if (!g_statHint->Owner()) {
        g_statHint->SetHintText(hovered, hint_id);

        Fvector2 position = GetUICursor().GetCursorPosition();
        position.y -= g_statHint->GetHeight() + 8.f;
        if (position.x + g_statHint->GetWidth() > UI_BASE_WIDTH)
            position.x = UI_BASE_WIDTH - g_statHint->GetWidth() - 4.f;
        if (position.x < 4.f)
            position.x = 4.f;
        if (position.y < 4.f)
            position.y = GetUICursor().GetCursorPosition().y + 24.f;
        if (position.y + g_statHint->GetHeight() > UI_BASE_HEIGHT)
            position.y = UI_BASE_HEIGHT - g_statHint->GetHeight() - 4.f;
        g_statHint->SetWndPos(position);
    }

    if (g_statHint->Owner() == hovered)
        g_statHint->Draw_();
}

void CUIWeatherEditor::SetStatus(LPCSTR string_id, ...) {
    string512 text;
    va_list args;
    va_start(args, string_id);
    const shared_str format = CStringTable().translate(string_id);
    vsprintf(text, format.c_str(), args);
    va_end(args);
    m_status->SetText(text);
}

float CUIWeatherEditor::GetPropertyValue(u32 property_index) const {
    if (!m_descriptor)
        return 0.f;
    const CEnvDescriptor& d = *m_descriptor;
    switch (property_index) {
    case 0: return rad2deg(d.sky_rotation);
    case 1: return d.far_plane;
    case 2: return d.fog_density;
    case 3: return d.fog_distance;
    case 4: return d.rain_density;
    case 5: return d.wind_velocity;
    case 6: return rad2deg(d.wind_direction);
    case 7: return d.bolt_period;
    case 8: return d.bolt_duration;
    case 9: return d.m_fSunShaftsIntensity;
    case 10: return d.m_fWaterIntensity;
    case 11: return d.sky_color.x;
    case 12: return d.sky_color.y;
    case 13: return d.sky_color.z;
    case 14: return d.clouds_color.x;
    case 15: return d.clouds_color.y;
    case 16: return d.clouds_color.z;
    case 17: return d.clouds_color.w;
    case 18: return d.fog_color.x;
    case 19: return d.fog_color.y;
    case 20: return d.fog_color.z;
    case 21: return d.rain_color.x;
    case 22: return d.rain_color.y;
    case 23: return d.rain_color.z;
    case 24: return d.ambient.x;
    case 25: return d.ambient.y;
    case 26: return d.ambient.z;
    case 27: return d.hemi_color.x;
    case 28: return d.hemi_color.y;
    case 29: return d.hemi_color.z;
    case 30: return d.hemi_color.w;
    case 31: return d.sun_color.x;
    case 32: return d.sun_color.y;
    case 33: return d.sun_color.z;
    // UI uses the physical meaning of the angles. The legacy .ltx key names
    // remain swapped only in CEnvDescriptor::load/save for compatibility.
    case 34: return rad2deg(d.sun_dir.getP());
    case 35: return rad2deg(d.sun_dir.getH());
    default: return 0.f;
    }
}

void CUIWeatherEditor::SetPropertyValue(u32 property_index, float value) {
    if (!m_descriptor)
        return;
    CEnvDescriptor& d = *m_descriptor;
    Snapshot(&d);
    float heading = d.sun_dir.getH();
    float pitch = d.sun_dir.getP();
    switch (property_index) {
    case 0: d.sky_rotation = deg2rad(value); break;
    case 1:
        d.far_plane = value;
        d.fog_distance = std::min(d.fog_distance, d.far_plane);
        break;
    case 2: d.fog_density = value; break;
    case 3: d.fog_distance = std::min(value, d.far_plane); break;
    case 4: d.rain_density = value; break;
    case 5: d.wind_velocity = value; break;
    case 6: d.wind_direction = deg2rad(value); break;
    case 7: d.bolt_period = value; break;
    case 8: d.bolt_duration = value; break;
    case 9: d.m_fSunShaftsIntensity = value; break;
    case 10: d.m_fWaterIntensity = value; break;
    case 11: d.sky_color.x = value; break;
    case 12: d.sky_color.y = value; break;
    case 13: d.sky_color.z = value; break;
    case 14: d.clouds_color.x = value; break;
    case 15: d.clouds_color.y = value; break;
    case 16: d.clouds_color.z = value; break;
    case 17: d.clouds_color.w = value; break;
    case 18: d.fog_color.x = value; break;
    case 19: d.fog_color.y = value; break;
    case 20: d.fog_color.z = value; break;
    case 21: d.rain_color.x = value; break;
    case 22: d.rain_color.y = value; break;
    case 23: d.rain_color.z = value; break;
    case 24: d.ambient.x = value; break;
    case 25: d.ambient.y = value; break;
    case 26: d.ambient.z = value; break;
    case 27: d.hemi_color.x = value; break;
    case 28: d.hemi_color.y = value; break;
    case 29: d.hemi_color.z = value; break;
    case 30: d.hemi_color.w = value; break;
    case 31: d.sun_color.x = value; break;
    case 32: d.sun_color.y = value; break;
    case 33: d.sun_color.z = value; break;
    case 34: d.sun_dir.setHP(heading, deg2rad(value)); break;
    case 35: d.sun_dir.setHP(deg2rad(value), pitch); break;
    }

    if (property_index == 1)
        SyncPropertyControls();
}

void CUIWeatherEditor::OnWeatherChanged(CUIWindow*, void*) {
    if (m_synchronizing)
        return;
    const int index = m_weather->CurrentID();
    if (index < 0 || (u32)index >= m_weather_names.size())
        return;
    CEnvironment& environment = g_pGamePersistent->Environment();
    environment.UpdateWeatherEditorSession(m_weather_names[index], m_editor_time);
    FillFrameList();
    SelectFrame(0);
}

void CUIWeatherEditor::OnFrameChanged(CUIWindow*, void*) {
    if (!m_synchronizing)
        SelectFrame((u32)m_frame->CurrentID());
}

void CUIWeatherEditor::OnTimeChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    // Mouse motion can generate several callbacks before the next rendered
    // frame. Coalesce them so environment selection runs at most once per UI
    // update and the time slider remains responsive.
    m_time_update_pending = true;
}

void CUIWeatherEditor::OnPropertyChanged(CUIWindow* window, void*) {
    if (m_synchronizing || !m_descriptor)
        return;

    for (SPropertyControl& control : m_property_controls) {
        if (control.slider != window)
            continue;

        // A property belongs to a concrete weather section. If the timeline
        // was left between sections, snap back to that section so the edit is
        // visible at full weight instead of looking like a frozen slider.
        if (!fsimilar(m_editor_time, m_descriptor->exec_time, .5f)) {
            ApplyEditorTime(m_descriptor->exec_time);
            SyncTimeControl(m_descriptor->exec_time);
        }
        SetPropertyValue(control.property_index, control.slider->GetFValue());
        return;
    }
}

void CUIWeatherEditor::OnSkyChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    const int index = m_sky->CurrentID();
    if (index < 0 || (u32)index >= m_sky_textures.size())
        return;
    Snapshot(m_descriptor);
    if (m_descriptor->set_sky_texture(m_sky_textures[index].c_str()))
        SetStatus("ui_weather_editor_status_sky_changed", m_sky_textures[index].c_str());
    else
        RefreshTextureSelection();
}

void CUIWeatherEditor::OnCloudsChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    const int index = m_clouds->CurrentID();
    if (index < 0 || (u32)index >= m_cloud_textures.size())
        return;
    Snapshot(m_descriptor);
    if (m_descriptor->set_clouds_texture(m_cloud_textures[index].c_str()))
        SetStatus("ui_weather_editor_status_clouds_changed", m_cloud_textures[index].c_str());
    else
        RefreshTextureSelection();
}

void CUIWeatherEditor::OnAmbientChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    const int index = m_ambient_definition->CurrentID();
    if (index < 0 || (u32)index >= m_ambient_definitions.size())
        return;
    Snapshot(m_descriptor);
    if (m_descriptor->set_ambient(g_pGamePersistent->Environment(),
                                  m_ambient_definitions[index].c_str()))
        SetStatus("ui_weather_editor_status_ambient_changed",
                  m_ambient_definitions[index].c_str());
    else
        RefreshDefinitionSelection();
}

void CUIWeatherEditor::OnSunChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    const int index = m_sun_definition->CurrentID();
    if (index < 0 || (u32)index >= m_sun_definitions.size())
        return;
    Snapshot(m_descriptor);
    if (m_descriptor->set_sun(g_pGamePersistent->Environment(),
                              m_sun_definitions[index].c_str()))
        SetStatus("ui_weather_editor_status_sun_changed", m_sun_definitions[index].c_str());
    else
        RefreshDefinitionSelection();
}

void CUIWeatherEditor::OnThunderboltChanged(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor)
        return;
    const int index = m_thunderbolt_definition->CurrentID();
    if (index < 0 || (u32)index >= m_thunderbolt_definitions.size())
        return;
    Snapshot(m_descriptor);
    if (m_descriptor->set_thunderbolt_collection(
            g_pGamePersistent->Environment(), m_thunderbolt_definitions[index].c_str()))
        SetStatus("ui_weather_editor_status_thunderbolt_changed",
                  m_thunderbolt_definitions[index].c_str());
    else
        RefreshDefinitionSelection();
}

void CUIWeatherEditor::OnColorSwatch(CUIWindow* window, void*) {
    if (!m_descriptor)
        return;

    for (SColorControl& control : m_color_controls) {
        if (control.swatch != window)
            continue;

        if (m_color_picker->IsShown() &&
            m_active_color_property == (int)control.first_property_index) {
            m_color_picker->Show(false);
            m_active_color_property = -1;
            return;
        }

        m_active_color_property = (int)control.first_property_index;
        m_color_picker->SetColor(GetPropertyValue(control.first_property_index),
                                 GetPropertyValue(control.first_property_index + 1),
                                 GetPropertyValue(control.first_property_index + 2));

        const Fvector2 swatch_position = control.swatch->GetWndPos();
        float picker_x = swatch_position.x - m_color_picker->GetWidth() +
                         control.swatch->GetWidth();
        float picker_y = swatch_position.y + control.swatch->GetHeight() + 3.f;
        clamp(picker_x, 0.f, GetWidth() - m_color_picker->GetWidth());
        clamp(picker_y, 0.f, GetHeight() - m_color_picker->GetHeight());
        m_color_picker->SetWndPos(Fvector2().set(picker_x, picker_y));
        m_color_picker->Show(true);
        SetStatus("ui_weather_editor_status_color_picker");
        return;
    }
}

void CUIWeatherEditor::OnColorPicked(CUIWindow*, void*) {
    if (m_synchronizing || !m_descriptor || m_active_color_property < 0)
        return;

    float r, g, b;
    m_color_picker->GetColor(r, g, b);
    Snapshot(m_descriptor);
    SetPropertyValue((u32)m_active_color_property, r);
    SetPropertyValue((u32)m_active_color_property + 1, g);
    SetPropertyValue((u32)m_active_color_property + 2, b);

    for (SPropertyControl& control : m_property_controls) {
        if ((int)control.property_index < m_active_color_property ||
            (int)control.property_index > m_active_color_property + 2)
            continue;
        static_cast<CWeatherTrackBar*>(control.slider)
            ->SetEditorValue(GetPropertyValue(control.property_index));
    }
}

void CUIWeatherEditor::OnAddFrame(CUIWindow*, void*) {
    if (!m_descriptor)
        return;

    CEnvironment& environment = g_pGamePersistent->Environment();
    CEnvDescriptor* created = nullptr;
    if (!environment.AddWeatherFrame(environment.CurrentCycleName, m_editor_time,
                                     *m_descriptor, &created)) {
        SetStatus("ui_weather_editor_status_duplicate_time");
        return;
    }

    FillFrameList();
    const auto cycle = environment.WeatherCycles.find(environment.CurrentCycleName);
    if (cycle == environment.WeatherCycles.end())
        return;
    const auto found = std::find(cycle->second.begin(), cycle->second.end(), created);
    if (found == cycle->second.end())
        return;

    SelectFrame((u32)std::distance(cycle->second.begin(), found));
    SetStatus("ui_weather_editor_status_frame_added",
              created->m_identifier.c_str());
}

void CUIWeatherEditor::OnCreateWeather(CUIWindow*, void*) {
    if (!m_descriptor)
        return;

    const std::string name = m_new_weather_name->GetText();
    CEnvironment& environment = g_pGamePersistent->Environment();
    std::string path;
    if (!environment.CreateWeather(name, *m_descriptor, &path)) {
        SetStatus("ui_weather_editor_status_create_error");
        return;
    }

    environment.UpdateWeatherEditorSession(name, 0.f);
    FillWeatherList();
    for (u32 index = 0; index < m_weather_names.size(); ++index) {
        if (m_weather_names[index] != name)
            continue;
        m_synchronizing = true;
        m_weather->SetItemIDX((int)index);
        m_synchronizing = false;
        break;
    }
    FillFrameList();
    SelectFrame(0);
    m_new_weather_name->ClearText();
    SetStatus("ui_weather_editor_status_created", path.c_str());
}

void CUIWeatherEditor::OnPreview(CUIWindow*, void*) {
    SetStatus("ui_weather_editor_status_preview");
    EnterPreview();
}

void CUIWeatherEditor::EnterPreview() {
    if (!m_session_active || !IsShown())
        return;
    m_previewing = true;
    HideDialog();
}

void CUIWeatherEditor::CloseEditorSession() {
    if (!m_session_active)
        return;

    m_previewing = false;
    if (IsShown()) {
        HideDialog();
        return;
    }

    if (g_pGamePersistent) {
        CEnvironment& environment = g_pGamePersistent->Environment();
        environment.EndWeatherEditorSession();
        environment.m_paused = m_previous_pause;
    }
    m_session_active = false;
}

void CUIWeatherEditor::OnSave(CUIWindow*, void*) {
    CEnvironment& environment = g_pGamePersistent->Environment();
    std::string path;
    if (!environment.SaveWeather(environment.CurrentCycleName, true, &path)) {
        SetStatus("ui_weather_editor_status_save_error");
        return;
    }

    // Saving establishes a new revert baseline for every frame of this cycle.
    const auto cycle = environment.WeatherCycles.find(environment.CurrentCycleName);
    if (cycle != environment.WeatherCycles.end()) {
        for (CEnvDescriptor* descriptor : cycle->second) {
            SDescriptorState* state = FindSnapshot(descriptor);
            if (state) {
                m_snapshots.erase(std::remove_if(m_snapshots.begin(), m_snapshots.end(),
                    [descriptor](const SDescriptorState& item) { return item.descriptor == descriptor; }),
                    m_snapshots.end());
            }
            Snapshot(descriptor);
        }
    }
    SetStatus("ui_weather_editor_status_saved", path.c_str());
}

void CUIWeatherEditor::OnRevert(CUIWindow*, void*) {
    SDescriptorState* state = FindSnapshot(m_descriptor);
    if (!state)
        return;
    RestoreSnapshot(*state);
    RefreshTextureSelection();
    RefreshDefinitionSelection();
    SyncPropertyControls();
    SetStatus("ui_weather_editor_status_reverted");
}

void CUIWeatherEditor::OnClose(CUIWindow*, void*) { m_close_requested = true; }

void ToggleWeatherEditor(bool force_show, bool force_hide) {
    if (!g_pGameLevel || !g_pGamePersistent)
        return;
    if (!weather_editor) {
        weather_editor = xr_new<CUIWeatherEditor>();
        weather_editor->Init();
    }
    if (force_hide) {
        weather_editor->CloseEditorSession();
        return;
    }

    const bool show = force_show || !weather_editor->IsShown();
    if (show && !weather_editor->IsShown())
        weather_editor->ShowDialog(true);
    else if (!force_show && weather_editor->IsShown())
        weather_editor->CloseEditorSession();
}

void PreviewWeatherEditor() {
    if (weather_editor)
        weather_editor->EnterPreview();
}

void DestroyWeatherEditor() {
    if (!weather_editor)
        return;
    weather_editor->CloseEditorSession();
    xr_delete(weather_editor);
}

bool WeatherEditorShown() { return weather_editor && weather_editor->IsSessionActive(); }
