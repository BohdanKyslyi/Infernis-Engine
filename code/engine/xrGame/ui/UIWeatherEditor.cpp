#include "stdafx.h"

#include "UIWeatherEditor.h"
#include "WeatherEditorModel.h"
#include "UI3tButton.h"
#include "UIBtnHint.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UIEditBox.h"
#include "UIListBoxItem.h"
#include "../xr_level_controller.h"
#include "xrSound/Sound.h"
#include "UIHelper.h"
#include "UIStatic.h"
#include "UITextureMaster.h"
#include "UITrackBar.h"
#include "UIXmlInit.h"
#include "xrUIXmlParser.h"
#include "UICursor.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/Rain.h"
#include "../../xrEngine/thunderbolt.h"
#include "xrRender/particles_systems_library_interface.hpp"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/xr_input.h"
#include "../game_cl_base.h"
#include "../Level.h"
#include "../Actor.h"
#include "../string_table.h"
#include "xrRender/UIRender.h"
#include "../../xrEngine/ssfx_console_vars.h" // ssfx_session_enable

extern ENGINE_API int g_current_renderer; // xrEngine/EngineAPI.cpp: 4 = the DX11 renderer runs

namespace {
// Resolve the optional preview API from the module that owns the active
// renderer object's vtable. Do not guess a renderer DLL name or load a second one.
typedef BOOL (__cdecl *WeatherPreviewV1)(const Frect*);

WeatherPreviewV1 weather_preview_api() {
    if (!UIRender) return nullptr;
    const void* table = *reinterpret_cast<void* const*>(UIRender);
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(table), &module))
        return nullptr;
    FARPROC entry = GetProcAddress(module, "xrWeatherEditorPreviewV1");
    // MSVC x86 may retain the cdecl leading underscore in the export table.
    if (!entry) entry = GetProcAddress(module, "_xrWeatherEditorPreviewV1");
    return reinterpret_cast<WeatherPreviewV1>(entry);
}

bool draw_weather_preview(const Frect& rect) {
    WeatherPreviewV1 api = weather_preview_api();
    if (!api) {
        static bool reported = false;
        if (!reported) {
            Msg("! Weather editor: renderer has no preview V1 API; using transparent preview");
            reported = true;
        }
        return false;
    }
    return api(&rect) != FALSE;
}

void release_weather_preview() {
    if (WeatherPreviewV1 api = weather_preview_api()) api(nullptr);
}

class CWeatherTrackBar : public CUITrackBar {
public:
    void SetEditorValue(float value) {
        clamp(value, m_f_min, m_f_max);
        m_f_val = value;
        UpdatePos();
    }
};

class CWeatherValueEdit : public CUIEditBox {
public:
    bool Editing() const { return m_bInputFocus; }
    bool TextChanged() const { return m_model_text != GetText(); }
    bool PendingChange() const {
        if(!Editing()) return false;
        float model,input;
        if(weather_editor_model::parse_number(m_model_text,model) &&
           weather_editor_model::parse_number(GetText(),input)) return model!=input;
        return m_model_text!=GetText();
    }
    void SetEditorText(LPCSTR text) {
        if(!Editing()) {m_model_text=text;SetText(text);}
    }
private:
    std::string m_model_text;
};

// Restrict the new slicing to editor buttons; other game menus stay unchanged.
class CWeatherButton : public CUI3tButton {
public:
    virtual void DrawTexture() {
        if (!m_bTextureEnable || !m_background) { CUI3tButton::DrawTexture(); return; }
        CUIStatic* state = m_background->Get(S_Current);
        if (!state || !state->GetShader() || !state->GetShader()->inited()) return;
        Frect r; GetAbsoluteRect(r);
        UI().ClientToScreenScaled(r.lt); UI().ClientToScreenScaled(r.rb);
        const Frect uv = state->GetTextureRect();
        if (r.width() <= 0.f || r.height() <= 0.f || uv.width() <= 0.f || uv.height() <= 0.f) return;
        UIRender->SetShader(*state->GetShader());
        Fvector2 atlas; UIRender->GetActiveTextureResolution(atlas);
        // Uniform corner scaling in physical pixels, independent of widescreen UI scaling.
        const float border = std::min(8.f, std::min(uv.width(), uv.height()) / 3.f);
        const float scale = std::min(r.height() / uv.height(), r.width() / (border * 2.f));
        const float edge = border * scale;
        const float xs[4] = {r.x1, r.x1+edge, r.x2-edge, r.x2};
        const float ys[4] = {r.y1, r.y1+edge, r.y2-edge, r.y2};
        const float us[4] = {uv.x1/atlas.x, (uv.x1+border)/atlas.x,
                            (uv.x2-border)/atlas.x, uv.x2/atlas.x};
        const float vs[4] = {uv.y1/atlas.y, (uv.y1+border)/atlas.y,
                            (uv.y2-border)/atlas.y, uv.y2/atlas.y};
        const u32 color = state->GetTextureColor();
        UIRender->StartPrimitive(54, IUIRender::ptTriList, UI().m_currentPointType);
        const auto point = [&](unsigned x, unsigned y) {
            UIRender->PushPoint(xs[x]-.5f, ys[y]-.5f, 0.f, color, us[x], vs[y]);
        };
        for (unsigned y=0;y<3;++y) for (unsigned x=0;x<3;++x) {
            point(x,y); point(x+1,y); point(x+1,y+1);
            point(x,y); point(x+1,y+1); point(x,y+1);
        }
        UIRender->FlushPrimitive();
    }
};

CUI3tButton* create_editor_button(CUIXml& xml, LPCSTR path, CUIWindow* parent) {
    auto* button = xr_new<CWeatherButton>();
    button->SetAutoDelete(true);
    parent->AttachChild(button);
    CUIXmlInit::Init3tButton(xml, path, 0, button);
    // DrawTexture owns the background pass. Skip it during child traversal.
    if (button->m_background)
        button->m_background->SetCustomDraw(true);
    if (button->m_back_frameline)
        button->m_back_frameline->SetCustomDraw(true);
    return button;
}

class CWeatherPreviewFrame : public CUIStatic {
public:
    virtual void Draw() {
        Frect r;GetAbsoluteRect(r);
        UI().ClientToScreenScaled(r.lt);UI().ClientToScreenScaled(r.rb);
        UIRender->SetShader(*GetShader());
        UIRender->StartPrimitive(8,IUIRender::ptLineList,UI().m_currentPointType);
        const auto point=[](float x,float y) {UIRender->PushPoint(x,y,0.f,color_rgba(150,160,145,200),.5f,.5f);};
        point(r.x1,r.y1);point(r.x2,r.y1);point(r.x2,r.y1);point(r.x2,r.y2);
        point(r.x2,r.y2);point(r.x1,r.y2);point(r.x1,r.y2);point(r.x1,r.y1);
        UIRender->FlushPrimitive();
    }
};

class CWeatherComboBox : public CUIComboBox {
public:
    void ClearEditorList() { CloseEditorList(); ClearList(); }
    virtual void CloseEditorList() {
        ShowList(false);
        Device.seqRender.Remove(this);
    }
};

// The editable header and result list share the combo's capture and lifetime.
constexpr LPCSTR lightning_sound_folders[] = {"ambient\\", "nature\\", "anomaly\\"};

class CWeatherResourceCombo : public CWeatherComboBox {
public:
    void Configure(CUIXml& xml, const xr_vector<std::string>& resources, bool sounds = false, bool opens_up = false) {
        m_resources = &resources;
        m_none = CStringTable().translate("ui_weather_editor_none");
        m_sounds = sounds;
        m_search = xr_new<CWeatherValueEdit>();
        m_search->SetAutoDelete(true);
        AttachChild(m_search);
        CUIXmlInit::InitEditBox(xml, "main:resource_search", 0, m_search);
        m_search->InitCustomEdit(Fvector2().set(0.f, 0.f),
                                Fvector2().set(GetWidth() - 18.f, GetHeight()));
        m_search->Show(false);
        if (opens_up)
            m_list_frame.SetWndPos(Fvector2().set(0.f, -m_list_frame.GetHeight()));
    }

    bool SearchOpen() { return m_list_frame.IsShown(); }

    void RoutePopupMouse(float x, float y, EUIMessages action) {
        Frect rect;
        GetAbsoluteRect(rect);
        OnMouseAction(x - rect.x1, y - rect.y1, action);
    }

    void SetResource(int id) {
        if (id < 0 || static_cast<size_t>(id) >= m_resources->size())
            id = 0;
        m_itoken_id = id;
        SetText(m_resources->empty() ? "" : Label(id));
    }

    void CloseEditorList() override {
        CWeatherComboBox::CloseEditorList();
        FinishSearch();
        if (m_sample._handle())
            m_sample.destroy();
    }

    void SendMessage(CUIWindow* source, s16 message, void* data) override {
        if (source == &m_list_box && message == LIST_ITEM_CLICKED) {
            auto* selected = m_list_box.GetSelectedItem();
            const auto id = selected ? reinterpret_cast<intptr_t>(selected->GetData()) : 0;
            if (id == -1 || id == -2) {
                // The clicked row is still dispatching its own event.
                m_page_step = id == -1 ? -1 : 1;
                return;
            }
        }
        CWeatherComboBox::SendMessage(source, message, data);
        if (source == &m_list_box && message == LIST_ITEM_CLICKED)
            FinishSearch();
    }

protected:
    void OnBtnClicked() override {
        if (m_list_frame.IsShown()) {
            CloseEditorList();
            return;
        }
        m_query.clear();
        m_page = 0;
        m_page_step = 0;
        m_search->SetText("");
        RebuildResults();
        ShowList(true);
        m_text.Show(false);
        m_search->Show(true);
        m_search->CaptureFocus(true);
    }

    bool OnKeyboardAction(int key, EUIMessages action) override {
        if (m_list_frame.IsShown() && action == WINDOW_KEY_PRESSED) {
            if (key == DIK_ESCAPE) {
                CloseEditorList();
                return true;
            }
            if (m_sounds && (key == DIK_PRIOR || key == DIK_NEXT)) {
                m_page_step = key == DIK_PRIOR ? -1 : 1;
                return true;
            }
            if (key == DIK_RETURN || key == DIK_NUMPADENTER) {
                RefreshResults();
                if (m_list_box.GetSize() && (!m_query.empty() || m_list_box.GetSelectedItem())) {
                    auto* row = m_list_box.GetSelectedItem();
                    if (row && reinterpret_cast<intptr_t>(row->GetData()) < 0)
                        return true;
                    if (!row)
                        m_list_box.SetSelectedIDX(m_page ? 1 : 0);
                    OnListItemSelect();
                    FinishSearch();
                }
                return true;
            }
        }
        return CWeatherComboBox::OnKeyboardAction(key, action);
    }

    bool OnMouseAction(float x, float y, EUIMessages action) override {
        if (m_list_frame.IsShown() && (action == WINDOW_LBUTTON_DOWN ||
                                      action == WINDOW_LBUTTON_DB_CLICK)) {
            if (y >= 0.f && y < m_frameLine.GetHeight() && x >= 0.f && x < m_search->GetWidth()) {
                m_search->CaptureFocus(true);
                return true;
            }
            if (m_sounds && TryPlay(x, y))
                return true;
        }
        const bool handled = CWeatherComboBox::OnMouseAction(x, y, action);
        if (!m_list_frame.IsShown())
            FinishSearch();
        return handled;
    }

    void Update() override {
        CWeatherComboBox::Update();
        if (m_list_frame.IsShown()) {
            RefreshResults();
            if (m_page_step) {
                m_page = std::max(0, m_page + m_page_step);
                m_page_step = 0;
                RebuildResults();
            }
        }
    }

private:
    LPCSTR Label(int id) const {
        return (*m_resources)[id].empty() ? m_none.c_str() : (*m_resources)[id].c_str();
    }

    void FinishSearch() {
        if (!m_search || !m_search->IsShown())
            return;
        if (m_search->Editing()) {
            m_search->CaptureFocus(false);
            SetKeyboardCapture(m_search, false);
        }
        if (m_sample._handle())
            m_sample.destroy();
        m_search->Show(false);
        m_text.Show(true);
        m_text.Enable(false);
    }

    void RefreshResults() {
        if (m_query == m_search->GetText())
            return;
        m_query = m_search->GetText();
        m_page = 0;
        m_page_step = 0;
        RebuildResults();
    }

    static std::string Fold(std::string value) {
        for (char& c : value)
            c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

    void RebuildResults() {
        const int selected = CurrentID();
        m_none = CStringTable().translate("ui_weather_editor_none");
        m_list_box.Clear();
        const std::string query = Fold(m_query);
        xr_vector<std::string> terms;
        std::istringstream input(query);
        for (std::string term; input >> term;)
            terms.push_back(term);
        xr_vector<int> ranked[3];
        for (size_t id = 0; id < m_resources->size(); ++id) {
            if (!terms.empty() && (*m_resources)[id].empty())
                continue;
            const auto name = Fold((*m_resources)[id]);
            if (m_sounds && !name.empty() &&
                std::none_of(std::begin(lightning_sound_folders), std::end(lightning_sound_folders),
                             [&](LPCSTR folder) { return name.find(folder) == 0; }))
                continue;
            if (!std::all_of(terms.begin(), terms.end(), [&](const std::string& term) {
                    return name.find(term) != std::string::npos;
                }))
                continue;
            const auto slash = name.find_last_of('\\');
            const auto leaf = slash == std::string::npos ? 0 : slash + 1;
            const int rank = name.compare(leaf, query.size(), query) == 0 ? 0 :
                             name.compare(0, query.size(), query) == 0 ? 1 : 2;
            ranked[rank].push_back(static_cast<int>(id));
        }
        const size_t total = ranked[0].size() + ranked[1].size() + ranked[2].size();
        const size_t pages = m_sounds && total ? (total + page_size - 1) / page_size : 1;
        m_page = std::min(m_page, static_cast<int>(pages - 1));
        const size_t first = m_sounds ? m_page * page_size : 0;
        const size_t last = m_sounds ? first + page_size : total;

        string32 page_label;
        if (m_page > 0) {
            xr_sprintf(page_label, "< %d/%u", m_page, static_cast<unsigned>(pages));
            AddItem_(page_label, -1);
        }
        size_t index = 0;
        for (const auto& group : ranked)
            for (int id : group) {
                const size_t row_index = index++;
                if (row_index < first || row_index >= last)
                    continue;
                auto* row = AddItem_(Label(id), id);
                if (terms.empty() && id == selected)
                    m_list_box.SetSelected(row);
            }
        if (last < total) {
            xr_sprintf(page_label, "> %d/%u", m_page + 2, static_cast<unsigned>(pages));
            AddItem_(page_label, -2);
        }
        m_list_box.ScrollToBegin();
        if (m_sounds)
            for (auto* item : m_list_box.Items()) {
                auto* row = static_cast<CUIListBoxItem*>(item);
                if (reinterpret_cast<intptr_t>(row->GetData()) <= 0)
                    continue;
                row->GetTextItem()->SetWidth(row->GetWidth() - play_width);
                row->GetTextItem()->SetEllipsis(true);
                row->AddTextField("[>]", play_width);
            }
    }

    bool TryPlay(float x, float y) {
        Frect origin;
        GetAbsoluteRect(origin);
        Fvector2 point;
        point.set(origin.x1 + x, origin.y1 + y);
        Frect viewport;
        m_list_box.GetAbsoluteRect(viewport);
        if (!viewport.in(point))
            return false;
        for (auto* item : m_list_box.Items()) {
            auto* row = static_cast<CUIListBoxItem*>(item);
            Frect icon;
            row->GetAbsoluteRect(icon);
            icon.x1 += row->GetTextItem()->GetWidth();
            icon.x2 = icon.x1 + play_width;
            const int id = static_cast<int>(reinterpret_cast<intptr_t>(row->GetData()));
            if (id <= 0 || static_cast<size_t>(id) >= m_resources->size() || !icon.in(point))
                continue;
            const auto& name = (*m_resources)[id];
            string_path filename;
            if (::Sound && FS.exist(filename, "$game_sounds$", name.c_str(), ".ogg")) {
                if (m_sample._handle())
                    m_sample.destroy();
                m_sample.create(name.c_str(), st_Effect, sg_SourceType);
                if (m_sample._handle())
                    m_sample.play(nullptr, sm_2D);
            }
            return true;
        }
        return false;
    }

    static constexpr size_t page_size = 32;
    static constexpr float play_width = 24.f;
    int m_page = 0;
    int m_page_step = 0;
    const xr_vector<std::string>* m_resources = nullptr;
    CWeatherValueEdit* m_search = nullptr;
    std::string m_query;
    shared_str m_none;
    bool m_sounds = false;
    ref_sound m_sample;
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

    void InitFields(CUIXml& xml) {
        const LPCSTR labels[] = {"HEX", "R", "G", "B"};
        for (unsigned i = 0; i < 4; ++i) {
            const float x = i == 0 ? 6.f : 208.f;
            const float y = i == 0 ? 6.f : 34.f + (i - 1) * 32.f;
            auto* label = UIHelper::CreateTextWnd(xml, "main:property_label_template", this);
            label->SetWndPos(Fvector2().set(x, y));
            label->SetWidth(32.f);
            label->SetText(labels[i]);
            auto* edit = xr_new<CWeatherValueEdit>();
            edit->SetAutoDelete(true);
            AttachChild(edit);
            CUIXmlInit::InitEditBox(xml, "main:number_template", 0, edit);
            edit->InitCustomEdit(Fvector2().set(x + 34.f, y),
                                 Fvector2().set(i == 0 ? 90.f : 48.f, 22.f));
            edit->SetMessageTarget(this);
            m_fields[i] = edit;
        }
        SyncFields();
    }

    void Show(bool shown) override {
        if (!shown) {
            m_drag_mode = 0;
            for (auto* edit : m_fields)
                if (edit && edit->Editing()) {
                    edit->CaptureFocus(false);
                    SetKeyboardCapture(edit, false);
                }
        }
        inherited::Show(shown);
    }

    void SendMessage(CUIWindow* source, s16 message, void* data) override {
        if (message == EDIT_TEXT_COMMIT || message == EDIT_TEXT_CANCEL) {
            for (unsigned i = 0; i < 4; ++i)
                if (source == m_fields[i]) {
                    if (message == EDIT_TEXT_COMMIT && m_fields[i]->TextChanged())
                        CommitField(i);
                    SyncFields();
                    return;
                }
        }
        inherited::SendMessage(source, message, data);
    }

    virtual void Draw() {
        Frect rect;
        GetAbsoluteRect(rect);
        UI().ClientToScreenScaled(rect.lt);
        UI().ClientToScreenScaled(rect.rb);

        UIRender->SetShader(*m_shader);
        const float u = .5f;
        const float v = .5f;

        const auto screen_rect = [&](Frect local) {
            Frect screen;
            screen.set(rect.x1 + ScaledX(local.x1), rect.y1 + ScaledY(local.y1),
                       rect.x1 + ScaledX(local.x2), rect.y1 + ScaledY(local.y2));
            return screen;
        };
        const Frect sv = screen_rect(SvRect());
        const Frect hue = screen_rect(HueRect());

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
        SyncFields();
    }

    void GetColor(float& r, float& g, float& b) const {
        HsvToRgb(m_hue, m_saturation, m_value, r, g, b);
    }

    bool IsDragging() const { return m_drag_mode != 0; }

private:
    void SyncFields() {
        if (!m_fields[0])
            return;
        float rgb[3];
        GetColor(rgb[0], rgb[1], rgb[2]);
        unsigned channels[3];
        for (unsigned i = 0; i < 3; ++i) {
            channels[i] = static_cast<unsigned>(iFloor(rgb[i] * 255.f + .5f));
            string32 number;
            xr_sprintf(number, "%u", channels[i]);
            m_fields[i + 1]->SetEditorText(number);
        }
        string32 hex;
        xr_sprintf(hex, "#%02X%02X%02X", channels[0], channels[1], channels[2]);
        m_fields[0]->SetEditorText(hex);
    }

    void CommitField(unsigned field) {
        float rgb[3];
        GetColor(rgb[0], rgb[1], rgb[2]);
        if (field) {
            float value;
            if (!weather_editor_model::parse_number(m_fields[field]->GetText(), value))
                return;
            rgb[field - 1] = clampr(value, 0.f, 255.f) / 255.f;
        } else {
            std::string hex = m_fields[0]->GetText();
            if (!hex.empty() && hex.front() == '#')
                hex.erase(0, 1);
            if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                return;
            const auto value = std::stoul(hex, nullptr, 16);
            for (unsigned i = 0; i < 3; ++i)
                rgb[i] = static_cast<float>((value >> (16 - 8 * i)) & 255) / 255.f;
        }
        SetColor(rgb[0], rgb[1], rgb[2]);
        GetMessageTarget()->SendMessage(this, BUTTON_CLICKED, nullptr);
    }

    CWeatherValueEdit* m_fields[4]{};

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
        result.set(6.f, 34.f, 176.f, GetHeight() - 6.f);
        return result;
    }

    Frect HueRect() const {
        const Frect sv = SvRect();
        Frect result;
        result.set(sv.x2 + 6.f, sv.y1, 200.f, sv.y2);
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
        SyncFields();
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

static constexpr SPropertyInfo properties[] = {
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
    {0.f, weather_motion::tree_amplitude_max, .01f},
    {0.f, weather_motion::rain_tilt_max, .01f},
    {0.f, 10.f, .01f},
};

static_assert(properties[36].maximum == 1.f, "Tree amplitude must be limited to 0..1");
static_assert(properties[37].maximum == 3.f, "Rain tilt multiplier must be limited to 0..3");

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
    "ui_weather_editor_hint_trees_amplitude",
    "ui_weather_editor_hint_rain_tilt_multiplier",
    "ui_weather_editor_hint_rain_speed_multiplier",
};

static_assert(sizeof(property_hint_ids) / sizeof(property_hint_ids[0]) ==
                  sizeof(properties) / sizeof(properties[0]),
              "Every weather property must have a learning hint");

// Screen Space Shaders: the Anomaly weathers ( Weather_ATMOS ) count wind velocity in 0 .. 1000 -
// Anomaly's own editor, ui_debug_weather.script:69, goes to max = 1000 - and the SSS rain reads it
// so ( Rain.cpp, wind_velocity * 0.001 ). While SSS runs ( switched on, DX11 ) the wind slider
// has that range; otherwise the game's 0 .. 100 above.
const SPropertyInfo& property_info(u32 property_index) {
    static const SPropertyInfo ssfx_wind_velocity = {0.f, 1000.f, 1.f};
    if (property_index == 5 && ssfx_session_enable && g_current_renderer == 4)
        return ssfx_wind_velocity;
    return properties[property_index];
}

static LPCSTR const lightning_hint_ids[] = {
    "ui_lightning_hint_enable_multi_strikes",
    "ui_lightning_hint_multi_strike_chance",
    "ui_lightning_hint_max_simultaneous",
    "ui_lightning_hint_same_location_chance",
    "ui_lightning_hint_branching_chance",
    "ui_lightning_hint_height_factor",
    "ui_lightning_hint_size_multiplier",
    "ui_lightning_hint_prob_near",
    "ui_lightning_hint_prob_medium",
    "ui_lightning_hint_dist_near_min",
    "ui_lightning_hint_dist_near_max",
    "ui_lightning_hint_dist_medium_min",
    "ui_lightning_hint_dist_medium_max",
    "ui_lightning_hint_dist_far_min",
    "ui_lightning_hint_dist_far_max",
    "ui_lightning_hint_thunder_range_min",
    "ui_lightning_hint_thunder_range_max",
    "ui_lightning_hint_thunder_fade_dist",
    "ui_lightning_hint_thunder_pitch_drop",
    "ui_lightning_hint_thunder_vol_drop",
    "ui_lightning_hint_strike_snd_range_min",
    "ui_lightning_hint_strike_snd_range_max",
    "ui_lightning_hint_strike_damage_power",
    "ui_lightning_hint_strike_damage_radius",
};
static_assert(sizeof(lightning_hint_ids)/sizeof(lightning_hint_ids[0])==lightning_editor::count,"Lightning hints must cover every parameter");

constexpr float property_y_offset = 0.f;
const LPCSTR weather_category_ids[]={"ui_env_sky_sun","ui_env_clouds","ui_env_fog","ui_env_rain","ui_env_wind","ui_env_ambient"};
const LPCSTR lightning_category_ids[]={"ui_env_burst","ui_env_distances","ui_env_thunder","ui_env_effects"};

CWeatherValueEdit* create_number(CUIXml& xml,CUIWindow* parent,float x,float y,float width=70.f) {
    auto* edit=xr_new<CWeatherValueEdit>();edit->SetAutoDelete(true);parent->AttachChild(edit);
    CUIXmlInit::InitEditBox(xml,"main:number_template",0,edit);
    edit->InitCustomEdit(Fvector2().set(x,y),Fvector2().set(width,22.f));
    return edit;
}

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

CUIWeatherEditor::~CUIWeatherEditor() {
    release_weather_preview();
}

void CUIWeatherEditor::Init() {
    CUIXml xml;
    xml.Load(CONFIG_PATH, UI_PATH, "weather_editor.xml");
    CUIXmlInit::InitWindow(xml, "main", 0, this);

    m_title = UIHelper::CreateTextWnd(xml, "main:title", this);
    m_status = UIHelper::CreateTextWnd(xml, "main:status", this);

    m_weather = xr_new<CWeatherResourceCombo>();
    m_weather->SetAutoDelete(true);
    AttachChild(m_weather);
    CUIXmlInit::InitComboBox(xml, "main:weather", 0, m_weather);
    static_cast<CWeatherResourceCombo*>(m_weather)->Configure(xml, m_weather_names);

    m_frame = xr_new<CWeatherComboBox>();
    m_frame->SetAutoDelete(true);
    AttachChild(m_frame);
    CUIXmlInit::InitComboBox(xml, "main:frame", 0, m_frame);

    m_sky = xr_new<CWeatherResourceCombo>();
    m_sky->SetAutoDelete(true);
    AttachChild(m_sky);
    CUIXmlInit::InitComboBox(xml, "main:sky", 0, m_sky);
    static_cast<CWeatherResourceCombo*>(m_sky)->Configure(xml, m_sky_textures);

    m_clouds = xr_new<CWeatherResourceCombo>();
    m_clouds->SetAutoDelete(true);
    AttachChild(m_clouds);
    CUIXmlInit::InitComboBox(xml, "main:clouds", 0, m_clouds);
    static_cast<CWeatherResourceCombo*>(m_clouds)->Configure(xml, m_cloud_textures);

    m_ambient_definition = xr_new<CWeatherResourceCombo>();
    m_ambient_definition->SetAutoDelete(true);
    AttachChild(m_ambient_definition);
    CUIXmlInit::InitComboBox(xml, "main:ambient_definition", 0, m_ambient_definition);
    static_cast<CWeatherResourceCombo*>(m_ambient_definition)->Configure(xml, m_ambient_definitions);

    m_sun_definition = xr_new<CWeatherResourceCombo>();
    m_sun_definition->SetAutoDelete(true);
    AttachChild(m_sun_definition);
    CUIXmlInit::InitComboBox(xml, "main:sun_definition", 0, m_sun_definition);
    static_cast<CWeatherResourceCombo*>(m_sun_definition)->Configure(xml, m_sun_definitions);

    m_thunderbolt_definition = xr_new<CWeatherResourceCombo>();
    m_thunderbolt_definition->SetAutoDelete(true);
    AttachChild(m_thunderbolt_definition);
    CUIXmlInit::InitComboBox(xml, "main:thunderbolt_definition", 0,
                            m_thunderbolt_definition);
    static_cast<CWeatherResourceCombo*>(m_thunderbolt_definition)->Configure(xml, m_thunderbolt_definitions);

    m_time_slider = xr_new<CWeatherTrackBar>();
    m_time_slider->SetAutoDelete(true);
    AttachChild(m_time_slider);
    CUIXmlInit::InitTrackBar(xml, "main:time_slider", 0, m_time_slider);
    m_time_slider->SetOptFBounds(0.f, DAY_LENGTH - 1.f);
    m_time_slider->SetStep(60.f);
    m_time_value = UIHelper::CreateTextWnd(xml, "main:time_value", this);

    CreatePropertyControls(xml);
    CreateLightningControls(xml);

    m_add_frame = create_editor_button(xml, "main:add_frame", this);
    m_file_panel=xr_new<CUIWindow>();m_file_panel->SetAutoDelete(true);AttachChild(m_file_panel);
    m_file_panel->SetWndPos(Fvector2().set(188.f,536.f));m_file_panel->SetWndSize(Fvector2().set(788.f,68.f));
    UIHelper::CreateStatic(xml,"main:file_back",m_file_panel);
    auto* file_label=UIHelper::CreateTextWnd(xml,"main:file_label",m_file_panel);
    (void)file_label;
    m_new_weather_name = UIHelper::CreateEditBox(xml, "main:new_weather_name", m_file_panel);
    m_create_weather = create_editor_button(xml, "main:create_weather", m_file_panel);
    m_learning_mode = UIHelper::CreateCheck(xml, "main:learning_mode", this);
    m_learning_mode->SetCheck(false);
    m_learning_mode_label =
        UIHelper::CreateTextWnd(xml, "main:learning_mode_label", this);
    m_preview = create_editor_button(xml, "main:preview", this);
    m_save = create_editor_button(xml, "main:save", this);
    m_revert = create_editor_button(xml, "main:revert", this);
    m_close = create_editor_button(xml, "main:close", this);
    m_weather_tab = create_editor_button(xml, "main:weather_tab", this);
    m_lightning_tab = create_editor_button(xml, "main:lightning_tab", this);
    CreateNavigation(xml);

    // One shared popup keeps the layout compact. Every color header opens it
    // for its own RGB triplet; RGBA alpha remains on the dedicated A slider.
    m_color_picker = xr_new<CWeatherColorPicker>();
    m_color_picker->SetAutoDelete(true);
    m_color_picker->SetWndSize(Fvector2().set(296.f, 158.f));
    m_color_picker->SetWndPos(Fvector2().set(0.f, 0.f));
    AttachChild(m_color_picker);
    m_color_picker->InitFields(xml);
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
    Register(m_weather_tab);
    Register(m_lightning_tab);
    Register(m_lightning_apply);
    Register(m_lightning_test);
    Register(m_multi_strikes);
    Register(m_files);
    for(auto* category:m_categories) Register(category);
    for(auto& control:m_lightning_controls) {Register(control.slider);Register(control.value);}
    for(auto& control:m_property_controls) Register(control.value);
    for (SColorControl& control : m_color_controls)
        Register(control.swatch);
    for (SPropertyControl& control : m_property_controls)
        Register(control.slider);

    AddCallback(m_files,BUTTON_CLICKED,CUIWndCallback::void_function(this,&CUIWeatherEditor::OnFiles));
    for(auto* category:m_categories)
        AddCallback(category,BUTTON_CLICKED,CUIWndCallback::void_function(this,&CUIWeatherEditor::OnCategory));
    for(auto& control:m_property_controls)
        AddCallback(control.value,EDIT_TEXT_COMMIT,CUIWndCallback::void_function(this,&CUIWeatherEditor::OnNumberCommitted));
    for(auto& control:m_lightning_controls)
        AddCallback(control.value,EDIT_TEXT_COMMIT,CUIWndCallback::void_function(this,&CUIWeatherEditor::OnNumberCommitted));
    AddCallback(m_weather_tab,BUTTON_CLICKED,
                CUIWndCallback::void_function(this,&CUIWeatherEditor::OnSelectTab));
    AddCallback(m_lightning_tab,BUTTON_CLICKED,
                CUIWndCallback::void_function(this,&CUIWeatherEditor::OnSelectTab));
    AddCallback(m_lightning_apply,BUTTON_CLICKED,
                CUIWndCallback::void_function(this,&CUIWeatherEditor::OnLightningAssets));
    AddCallback(m_multi_strikes,BUTTON_CLICKED,CUIWndCallback::void_function(this,&CUIWeatherEditor::OnLightningMulti));
    AddCallback(m_lightning_test,BUTTON_CLICKED,
                CUIWndCallback::void_function(this,&CUIWeatherEditor::OnTestLightning));
    for(auto& control:m_lightning_controls)
        AddCallback(control.slider,BUTTON_CLICKED,
                    CUIWndCallback::void_function(this,&CUIWeatherEditor::OnLightningChanged));
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

    Msg("* Weather editor: wind limits v2; trees=0..1, rain tilt multiplier=0..3");
    SetLightningPage(false);
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
            if(m_lightning_mode) SyncLightningControls();
            return;
        }

        m_snapshots.clear();
        // Closing the dialog does not save changes. Keep disk baselines across reopenings.
        if(!m_saved_baselines_ready) {
            for(const auto& cycle:environment.WeatherCycles)
                m_weather_baselines[cycle.first]=WeatherSignature(cycle.first);
            if(environment.eff_Thunderbolt) m_lightning_saved=environment.eff_Thunderbolt->GetEditorSettings();
            m_rain_tilt_saved=environment.eff_Rain?environment.eff_Rain->GetWindTiltMultiplier():1.f;
            m_rain_speed_saved=environment.eff_Rain?environment.eff_Rain->GetWindSpeedMultiplier():0.f;
            m_saved_baselines_ready=true;
        }
        SetLightningPage(false);
        if(environment.eff_Thunderbolt)
            m_lightning_baseline=environment.eff_Thunderbolt->GetEditorSettings();
        const float current_time = environment.GetGameTime();
        m_rain_tilt_baseline = environment.eff_Rain
            ? environment.eff_Rain->GetWindTiltMultiplier() : 1.f;
        m_rain_speed_baseline = environment.eff_Rain
            ? environment.eff_Rain->GetWindSpeedMultiplier() : 0.f;
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
        m_previewing = false;
        release_weather_preview();
        ClearLearningHint();
        m_color_picker->Show(false);
        m_active_color_property = -1;
        if (g_pGamePersistent && !m_keep_session) {
            CEnvironment& environment = g_pGamePersistent->Environment();
            environment.EndWeatherEditorSession();
            environment.m_paused = m_previous_pause;
            m_session_active = false;
        }
        m_keep_session = false;

        // An expanded combo registers itself in seqRender and captures the
        // parent. Close every list before hiding/deleting the dialog.
        static_cast<CWeatherComboBox*>(m_lightning_particle)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_lightning_sound)->CloseEditorList();
        static_cast<CWeatherComboBox*>(m_lightning_collection)->CloseEditorList();
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

void CUIWeatherEditor::Draw() {
    if (m_previewing) return;
    if (m_preview_frame && m_session_active) {
        Frect rect; m_preview_frame->GetAbsoluteRect(rect);
        UI().ClientToScreenScaled(rect.lt); UI().ClientToScreenScaled(rect.rb);
        // Capture before drawing any editor panels or lists.
        draw_weather_preview(rect);
    }
    inherited::Draw();
}

bool CUIWeatherEditor::OnMouseAction(float x,float y,EUIMessages action) {
    if (m_previewing)
        return true;
    for (auto* combo : {m_weather, m_sky, m_clouds, m_ambient_definition, m_sun_definition,
                        m_thunderbolt_definition, m_lightning_collection,
                        m_lightning_particle, m_lightning_sound}) {
        auto* resource = static_cast<CWeatherResourceCombo*>(combo);
        if (resource && resource->IsShown() && resource->SearchOpen()) {
            resource->RoutePopupMouse(x, y, action);
            return true;
        }
    }
    return inherited::OnMouseAction(x, y, action);
}

bool CUIWeatherEditor::OnKeyboardHold(int dik) {
    return m_previewing ? true : inherited::OnKeyboardHold(dik);
}

void CUIWeatherEditor::Update() {
    if (m_previewing) { GetUICursor().Hide(); return; }
    inherited::Update();
    if (m_close_requested) {
        m_close_requested = false;
        CommitNumericEdits();
        if(m_lightning_mode && !ApplyLightningAssets()) return;
        CloseEditorSession();
        return;
    }
    UpdateDirtyState();
    if(m_time_update_pending && m_descriptor && !m_synchronizing) {
        m_time_update_pending=false;
        const float requested=m_time_slider->GetFValue();
        if(std::abs(requested-m_editor_time)>=1.f) ApplyEditorTime(requested);
    }
    if(m_lightning_mode && !m_synchronizing) {
        SyncLightningControls();
        SyncTimeControl(m_editor_time);
        UpdateLearningHint();
        return;
    }
    if (!m_descriptor || m_synchronizing) {
        UpdateLearningHint();
        return;
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
        static_cast<CWeatherValueEdit*>(control.value)->SetEditorText(value_text);
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
    if (m_previewing) {
        if (keyboard_action == WINDOW_KEY_PRESSED && (dik == DIK_F7 || dik == DIK_ESCAPE))
            LeavePreview();
        return true; // Hidden controls cannot receive hotkeys or edit input.
    }
    if (keyboard_action == WINDOW_KEY_PRESSED) {
        if (dik == DIK_ESCAPE) {
            for (auto* combo : {m_weather, m_sky, m_clouds, m_ambient_definition, m_sun_definition,
                                m_thunderbolt_definition, m_lightning_collection,
                                m_lightning_particle, m_lightning_sound}) {
                auto* resource = static_cast<CWeatherResourceCombo*>(combo);
                if (resource->SearchOpen()) {
                    resource->CloseEditorList();
                    return true;
                }
            }
        }
        if (dik == DIK_ESCAPE) {
            m_keep_session = false;
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
        // F7 hides the editor and keeps its session: the edited weather and time stay fixed in the
        // game ( time is paused ). It does not write files ( that is F6 ). Esc ends the session.
        // The preview stays on its button and on "weather_editor preview".
        if (dik == DIK_F7) {
            OnSaveClose(nullptr, nullptr);
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
    for (const auto& cycle : cycles)
        m_weather_names.push_back(cycle.first);
    static_cast<CWeatherResourceCombo*>(m_weather)->SetResource(0);
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
    static_cast<CWeatherResourceCombo*>(m_sky)->SetResource(0);
    static_cast<CWeatherResourceCombo*>(m_clouds)->SetResource(0);
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
    clear_combo(m_lightning_collection);
    static_cast<CWeatherResourceCombo*>(m_ambient_definition)->SetResource(0);
    static_cast<CWeatherResourceCombo*>(m_sun_definition)->SetResource(0);
    static_cast<CWeatherResourceCombo*>(m_thunderbolt_definition)->SetResource(0);
    static_cast<CWeatherResourceCombo*>(m_lightning_collection)->SetResource(0);
}

void CUIWeatherEditor::AddPropertyHeader(CUIXml& xml, LPCSTR caption, float x, float y,
                                         int first_color_property) {
    CUITextWnd* header = UIHelper::CreateTextWnd(xml, "main:property_header_template", this);
    header->SetWndPos(Fvector2().set(x, y + property_y_offset));
    header->SetTextST(caption);
    TrackWeatherWindow(header,m_build_category);

    if (first_color_property >= 0) {
        CWeatherColorSwatch* swatch = xr_new<CWeatherColorSwatch>();
        swatch->SetAutoDelete(true);
        swatch->SetWndPos(Fvector2().set(x + 330.f, y + property_y_offset + 1.f));
        swatch->SetWndSize(Fvector2().set(46.f, 14.f));
        swatch->InitTexture("ui\\ui_weather_editor_white");
        AttachChild(swatch);
        TrackWeatherWindow(swatch,m_build_category);

        SColorControl control;
        control.first_property_index = (u32)first_color_property;
        control.header = header;
        control.swatch = swatch;
        m_color_controls.push_back(control);
    }
}

void CUIWeatherEditor::TrackWeatherWindow(CUIWindow* window,unsigned category) {
    m_weather_property_windows.push_back(window);
    m_category_windows.push_back({window,category});
}

void CUIWeatherEditor::AddPropertyControl(CUIXml& xml,u32 property_index,LPCSTR caption,float x,float y) {
    VERIFY(property_index<sizeof(properties)/sizeof(properties[0]));
    VERIFY(weather_editor_model::weather_categories[property_index]==m_build_category);
    const bool compact=property_index>=11 && property_index<=33;
    auto* label=UIHelper::CreateTextWnd(xml,"main:property_label_template",this);
    label->SetWndPos(Fvector2().set(x,y));label->SetTextST(caption);
    label->SetWndSize(Fvector2().set(compact?18.f:180.f,22.f));
    auto* slider=xr_new<CWeatherTrackBar>();slider->SetAutoDelete(true);AttachChild(slider);
    CUIXmlInit::InitTrackBar(xml,"main:property_slider_template",0,slider);
    slider->SetWndPos(Fvector2().set(x+184.f,y+5.f));
    slider->SetOptFBounds(property_info(property_index).minimum,property_info(property_index).maximum);
    slider->SetStep(property_info(property_index).step);
    auto* value=create_number(xml,this,x+(compact?20.f:308.f),y,compact?64.f:70.f);
    m_property_controls.push_back({property_index,label,slider,value});
    TrackWeatherWindow(label,m_build_category);TrackWeatherWindow(value,m_build_category);
    // RGB values use compact editable fields; the retained sliders provide the same value model.
    if(!compact) TrackWeatherWindow(slider,m_build_category);
    else slider->Show(false);
}

void CUIWeatherEditor::CreatePropertyControls(CUIXml& xml) {
    const float x=188.f;
    const auto selector=[&](CUIComboBox* combo,LPCSTR caption,float y,unsigned category) {
        auto* label=UIHelper::CreateTextWnd(xml,"main:property_label_template",this);
        label->SetWndPos(Fvector2().set(x,y));label->SetWndSize(Fvector2().set(140.f,22.f));label->SetTextST(caption);
        TrackWeatherWindow(label,category);TrackWeatherWindow(combo,category);
    };
    const auto color=[&](u32 first,LPCSTR caption,float y,unsigned count=3) {
        AddPropertyHeader(xml,caption,x,y,first);
        const LPCSTR channels[]={"ui_weather_editor_channel_r","ui_weather_editor_channel_g","ui_weather_editor_channel_b","ui_weather_editor_channel_a"};
        for(unsigned c=0;c<count;++c) AddPropertyControl(xml,first+c,channels[c],x+c*94.f,y+22.f);
    };
    m_build_category=0;
    selector(m_sky,"ui_weather_editor_sky",176.f,0);
    color(11,"ui_weather_editor_sky_color_rgb",208.f);
    AddPropertyControl(xml,0,"ui_weather_editor_sky_rotation",x,262.f);
    color(27,"ui_weather_editor_hemisphere_color_rgba",294.f,4);
    selector(m_sun_definition,"ui_weather_editor_sun",348.f,0);
    color(31,"ui_weather_editor_sun_color_rgb",380.f);
    AddPropertyControl(xml,34,"ui_weather_editor_sun_altitude",x,434.f);
    AddPropertyControl(xml,35,"ui_weather_editor_sun_longitude",x,466.f);
    AddPropertyControl(xml,9,"ui_weather_editor_sun_shafts",x,498.f);
    m_build_category=1;
    selector(m_clouds,"ui_weather_editor_clouds",176.f,1);
    color(14,"ui_weather_editor_clouds_color_rgba",216.f,4);
    m_build_category=2;
    color(18,"ui_weather_editor_fog_color_rgb",176.f);
    AddPropertyControl(xml,2,"ui_weather_editor_fog_density",x,236.f);
    AddPropertyControl(xml,3,"ui_weather_editor_fog_distance",x,268.f);
    AddPropertyControl(xml,1,"ui_weather_editor_far_plane",x,300.f);
    m_build_category=3;
    color(21,"ui_weather_editor_rain_color_rgb",176.f);
    AddPropertyControl(xml,4,"ui_weather_editor_rain_density",x,236.f);
    AddPropertyControl(xml,37,"ui_weather_editor_rain_tilt_multiplier",x,268.f);
    AddPropertyControl(xml,38,"ui_weather_editor_rain_speed_multiplier",x,300.f);
    selector(m_thunderbolt_definition,"ui_weather_editor_thunderbolt_selector",348.f,3);
    AddPropertyControl(xml,7,"ui_weather_editor_thunderbolt_period",x,392.f);
    AddPropertyControl(xml,8,"ui_weather_editor_thunderbolt_duration",x,424.f);
    m_build_category=4;
    AddPropertyHeader(xml,"ui_weather_editor_wind",x,176.f);
    AddPropertyControl(xml,5,"ui_weather_editor_wind_velocity",x,214.f);
    AddPropertyControl(xml,6,"ui_weather_editor_wind_direction",x,246.f);
    AddPropertyControl(xml,36,"ui_weather_editor_trees_amplitude",x,278.f);
    m_build_category=5;
    selector(m_ambient_definition,"ui_weather_editor_ambient",176.f,5);
    color(24,"ui_weather_editor_ambient_color_rgb",216.f);
    AddPropertyControl(xml,10,"ui_weather_editor_water_intensity",x,276.f);
}

void CUIWeatherEditor::CreateLightningControls(CUIXml& xml) {
    m_lightning_header=xr_new<CUIWindow>();m_lightning_header->SetAutoDelete(true);AttachChild(m_lightning_header);
    m_lightning_header->SetWndPos(Fvector2().set(188.f,174.f));m_lightning_header->SetWndSize(Fvector2().set(380.f,58.f));
    auto* collection_label=UIHelper::CreateTextWnd(xml,"main:property_header_template",m_lightning_header);
    collection_label->SetTextST("ui_lightning_test_collection");
    m_lightning_collection=xr_new<CWeatherResourceCombo>();m_lightning_collection->SetAutoDelete(true);
    m_lightning_header->AttachChild(m_lightning_collection);
    CUIXmlInit::InitComboBox(xml,"main:lightning_collection_template",0,m_lightning_collection);
    static_cast<CWeatherResourceCombo*>(m_lightning_collection)->Configure(xml, m_thunderbolt_definitions);
    m_lightning_panel=xr_new<CUIWindow>();m_lightning_panel->SetAutoDelete(true);AttachChild(m_lightning_panel);
    m_lightning_panel->SetWndPos(Fvector2().set(188.f,242.f));m_lightning_panel->SetWndSize(Fvector2().set(380.f,280.f));
    for(unsigned i=0;i<lightning_editor::count;++i) {
        const unsigned category=weather_editor_model::lightning_category(i);
        const unsigned first=category==0?0:category==1?7:category==2?15:22;
        const float y=(i-first)*32.f;
        const auto& parameter=lightning_editor::parameters[i];
        std::string key="ui_lightning_";key+=parameter.key;
        auto* label=UIHelper::CreateTextWnd(xml,"main:property_label_template",m_lightning_panel);
        label->SetWndPos(Fvector2().set(0.f,y));label->SetWndSize(Fvector2().set(180.f,22.f));label->SetTextST(key.c_str());
        auto* slider=xr_new<CWeatherTrackBar>();slider->SetAutoDelete(true);m_lightning_panel->AttachChild(slider);
        CUIXmlInit::InitTrackBar(xml,"main:property_slider_template",0,slider);
        slider->SetWndPos(Fvector2().set(184.f,y+5.f));slider->SetOptFBounds(parameter.minimum,parameter.maximum);slider->SetStep(parameter.step);
        auto* value=create_number(xml,m_lightning_panel,308.f,y);
        m_lightning_controls.push_back({i,label,slider,value});
    }
    m_multi_strikes=UIHelper::CreateCheck(xml,"main:multi_strikes_template",m_lightning_panel);
    m_assets_panel=xr_new<CUIWindow>();m_assets_panel->SetAutoDelete(true);AttachChild(m_assets_panel);
    m_assets_panel->SetWndPos(Fvector2().set(188.f,534.f));m_assets_panel->SetWndSize(Fvector2().set(788.f,78.f));
    UIHelper::CreateStatic(xml,"main:assets_back",m_assets_panel);
    auto* particle_label=UIHelper::CreateTextWnd(xml,"main:property_header_template",m_assets_panel);
    particle_label->SetTextST("ui_lightning_particle");particle_label->SetWndSize(Fvector2().set(270.f,18.f));
    auto* sound_label=UIHelper::CreateTextWnd(xml,"main:property_header_template",m_assets_panel);
    sound_label->SetWndPos(Fvector2().set(284.f,0.f));sound_label->SetTextST("ui_lightning_sound");sound_label->SetWndSize(Fvector2().set(270.f,18.f));
    auto* particle = xr_new<CWeatherResourceCombo>();
    particle->SetAutoDelete(true);
    m_assets_panel->AttachChild(particle);
    CUIXmlInit::InitComboBox(xml, "main:lightning_particle_template", 0, particle);
    particle->Configure(xml, m_lightning_particles, false, true);
    m_lightning_particle = particle;
    auto* sound = xr_new<CWeatherResourceCombo>();
    sound->SetAutoDelete(true);
    m_assets_panel->AttachChild(sound);
    CUIXmlInit::InitComboBox(xml, "main:lightning_sound_template", 0, sound);
    sound->Configure(xml, m_lightning_sounds, true, true);
    m_lightning_sound = sound;
    m_lightning_apply=create_editor_button(xml,"main:lightning_apply_template",m_assets_panel);
    m_lightning_test=create_editor_button(xml,"main:lightning_test_template",this);
}

void CUIWeatherEditor::FillLightningParticles() {
    m_synchronizing=true;
    static_cast<CWeatherComboBox*>(m_lightning_particle)->CloseEditorList();
    clear_combo(m_lightning_particle);m_lightning_particles.clear();m_lightning_particles.push_back("");
    const auto& library=g_pGamePersistent->Environment().m_pRender->particles_systems_library();
    auto i=library.particles_group_begin();const auto end=library.particles_group_end();
    for(;i!=end;library.particles_group_next(i))
        append_unique(m_lightning_particles,library.particles_group_id(**i).c_str());
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
    if(effect) append_unique(m_lightning_particles,effect->GetEditorSettings().particle);
    std::sort(m_lightning_particles.begin()+1,m_lightning_particles.end());

    m_synchronizing=false;
}

void CUIWeatherEditor::FillLightningSounds() {
    m_synchronizing=true;
    static_cast<CWeatherComboBox*>(m_lightning_sound)->CloseEditorList();
    clear_combo(m_lightning_sound);
    m_lightning_sounds.clear();
    m_lightning_sounds.push_back(""); // Empty config value disables the impact sound.
    for (LPCSTR folder : lightning_sound_folders) {
        string_path path;
        FS.update_path(path, "$game_sounds$", folder);
        FS_FileSet files;
        FS.file_list(files, path, FS_ListFiles | FS_ClampExt, "*.ogg");
        for (const FS_File& file : files) {
            std::string name = std::string(folder) + file.name.c_str();
            std::replace(name.begin(), name.end(), '/', '\\');
            m_lightning_sounds.push_back(name);
        }
    }
    // Keep the configured sound resolvable even if it is outside the picker.
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
    if(effect) append_unique(m_lightning_sounds,effect->GetEditorSettings().sound);
    std::sort(m_lightning_sounds.begin()+1,m_lightning_sounds.end());
    m_lightning_sounds.erase(std::unique(m_lightning_sounds.begin()+1,m_lightning_sounds.end()),m_lightning_sounds.end());

    m_synchronizing=false;
}

void CUIWeatherEditor::CreateNavigation(CUIXml& xml) {
    m_category_title=UIHelper::CreateTextWnd(xml,"main:category_title",this);
    m_context_help=UIHelper::CreateTextWnd(xml,"main:context_help",this);
    m_dirty_status=UIHelper::CreateTextWnd(xml,"main:dirty_status",this);
    m_files=create_editor_button(xml,"main:files",this);
    for(unsigned i=0;i<6;++i) {
        auto* button=create_editor_button(xml,"main:category_template",this);
        button->SetWndPos(Fvector2().set(8.f,176.f+i*40.f));
        button->SetStateTextColor(color_rgba(235,220,150,255),S_Disabled);
        button->SetStateTextColor(color_rgba(200,205,195,255),S_Enabled);
        m_categories.push_back(button);
    }
    auto* preview=xr_new<CWeatherPreviewFrame>();m_preview_frame=preview;preview->SetAutoDelete(true);
    preview->SetWndPos(Fvector2().set(600.f,170.f));preview->SetWndSize(Fvector2().set(376.f,280.f));
    preview->InitTexture("ui\\ui_weather_editor_white");preview->Enable(false);AttachChild(preview);
    UIHelper::CreateTextWnd(xml,"main:live_label",this);
}

void CUIWeatherEditor::RefreshCategory() {
    const unsigned selected=m_lightning_mode?m_lightning_category:m_weather_category;
    const unsigned count=m_lightning_mode?4:6;
    for(unsigned i=0;i<m_categories.size();++i) {
        auto* button=m_categories[i];button->Show(i<count);button->Enable(i!=selected);
        if(i<count) button->TextItemControl()->SetTextST(m_lightning_mode?lightning_category_ids[i]:weather_category_ids[i]);
    }
    m_category_title->SetTextST(m_lightning_mode?lightning_category_ids[selected]:weather_category_ids[selected]);
    for(const auto& item:m_category_windows)
        item.window->Show(!m_lightning_mode && item.category==selected);
    for(auto& control:m_lightning_controls) {
        const bool visible=m_lightning_mode && weather_editor_model::lightning_category(control.index)==selected;
        control.label->Show(visible);control.slider->Show(visible && control.index!=0);control.value->Show(visible && control.index!=0);
    }
    m_multi_strikes->Show(m_lightning_mode && selected==0);
    m_context_help->SetTextST("ui_env_live_hint");
}

void CUIWeatherEditor::OnCategory(CUIWindow* window,void*) {
    CommitNumericEdits();CloseLists();ClearLearningHint();
    m_color_picker->Show(false);m_active_color_property=-1;
    for(unsigned i=0;i<m_categories.size();++i) if(m_categories[i]==window) {
        if(m_lightning_mode) m_lightning_category=i;else m_weather_category=i;
        RefreshCategory();return;
    }
}

void CUIWeatherEditor::OnFiles(CUIWindow*,void*) {
    if(m_lightning_mode) return;
    CommitNumericEdits();CloseLists();m_files_visible=!m_files_visible;
    m_file_panel->Show(m_files_visible);
}

void CUIWeatherEditor::CommitNumericEdits(bool apply) {
    if(m_synchronizing) return;
    const auto commit=[this,apply](CUIEditBox* field) {
        auto* edit=static_cast<CWeatherValueEdit*>(field);
        if(edit->Editing()) {
            edit->CaptureFocus(false);
            edit->GetParent()->SetKeyboardCapture(edit,false);
            if(apply) OnNumberCommitted(edit,nullptr);
        }
    };
    if(m_lightning_mode) {for(auto& control:m_lightning_controls) commit(control.value);}
    else {for(auto& control:m_property_controls) commit(control.value);}
}

void CUIWeatherEditor::OnNumberCommitted(CUIWindow* window,void*) {
    if(m_synchronizing) return;
    if(m_lightning_mode) {
        auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;if(!effect)return;
        for(auto& control:m_lightning_controls) if(window==control.value) {
            float value;
            if(weather_editor_model::parse_number(control.value->GetText(),value)) {
                auto settings=effect->GetEditorSettings();lightning_editor::change(settings,control.index,value);
                if(!effect->ApplyEditorSettings(settings)) SetStatus("ui_lightning_assets_error");
            } else SetStatus("ui_env_number_error");
            SyncLightningControls();m_next_dirty_update=0;return;
        }
    } else if(m_descriptor) {
        for(auto& control:m_property_controls) if(window==control.value) {
            float value;
            if(weather_editor_model::parse_number(control.value->GetText(),value)) {
                if(!fsimilar(m_editor_time,m_descriptor->exec_time,.5f)) {
                    ApplyEditorTime(m_descriptor->exec_time);SyncTimeControl(m_descriptor->exec_time);
                }
                // Screen Space Shaders: the bounds of the slider ( property_info, wind 0 .. 1000 with SSS ).
                const auto& bounds=property_info(control.property_index);
                SetPropertyValue(control.property_index,std::clamp(value,bounds.minimum,bounds.maximum));
            } else SetStatus("ui_env_number_error");
            SyncPropertyControls();m_next_dirty_update=0;return;
        }
    }
}

std::string CUIWeatherEditor::WeatherSignature(const std::string& name) const {
    const auto& cycles=g_pGamePersistent->Environment().WeatherCycles;
    const auto cycle=cycles.find(name);if(cycle==cycles.end()) return "";
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);
    const auto text=[&out](const std::string& value){out<<value.size()<<':'<<value<<';';};
    for(const auto* d:cycle->second) {
        const float values[]={d->exec_time,d->sky_rotation,d->far_plane,d->fog_density,d->fog_distance,
            d->rain_density,d->bolt_period,d->bolt_duration,d->trees_amplitude,d->wind_velocity,d->wind_direction,
            d->m_fSunShaftsIntensity,d->m_fWaterIntensity,d->clouds_color.x,d->clouds_color.y,d->clouds_color.z,d->clouds_color.w,
            d->sky_color.x,d->sky_color.y,d->sky_color.z,d->fog_color.x,d->fog_color.y,d->fog_color.z,
            d->rain_color.x,d->rain_color.y,d->rain_color.z,d->ambient.x,d->ambient.y,d->ambient.z,
            d->hemi_color.x,d->hemi_color.y,d->hemi_color.z,d->hemi_color.w,d->sun_color.x,d->sun_color.y,d->sun_color.z,
            d->sun_dir.x,d->sun_dir.y,d->sun_dir.z};
        text(d->m_identifier);for(float value:values)out<<value<<';';
        text(d->sky_texture_name);text(d->clouds_texture_name);
        text(d->env_ambient?d->env_ambient->name().c_str():"");text(d->lens_flare_id);text(d->tb_id);
    }
    return out.str();
}

void CUIWeatherEditor::UpdateDirtyState() {
    if(!m_session_active || Device.dwTimeGlobal<m_next_dirty_update) return;
    m_next_dirty_update=Device.dwTimeGlobal+250;
    const auto& environment=g_pGamePersistent->Environment();
    // Only serialize the active cycle. Other edited cycles retain their cached state.
    const std::string& name=environment.CurrentCycleName;
    const auto baseline=m_weather_baselines.find(name);
    m_weather_dirty[name]=baseline==m_weather_baselines.end() || baseline->second!=WeatherSignature(name);
    bool weather=std::any_of(m_weather_dirty.begin(),m_weather_dirty.end(),[](const auto& entry){return entry.second;});
    if(environment.eff_Rain) weather=weather ||
        environment.eff_Rain->GetWindTiltMultiplier()!=m_rain_tilt_saved ||
        environment.eff_Rain->GetWindSpeedMultiplier()!=m_rain_speed_saved;
    bool lightning=false;
    if(environment.eff_Thunderbolt) {
        const auto settings=environment.eff_Thunderbolt->GetEditorSettings();
        lightning=settings.values!=m_lightning_saved.values || settings.particle!=m_lightning_saved.particle || settings.sound!=m_lightning_saved.sound;
        if(m_lightning_mode) {
            const int particle=m_lightning_particle->CurrentID(),sound=m_lightning_sound->CurrentID();
            if(particle>=0 && static_cast<size_t>(particle)<m_lightning_particles.size())
                lightning=lightning || m_lightning_particles[particle]!=m_lightning_saved.particle;
            if(sound>=0 && static_cast<size_t>(sound)<m_lightning_sounds.size())
                lightning=lightning || m_lightning_sounds[sound]!=m_lightning_saved.sound;
            for(const auto& control:m_lightning_controls) lightning=lightning || static_cast<CWeatherValueEdit*>(control.value)->PendingChange();
        }
    }
    if(!m_lightning_mode) for(const auto& control:m_property_controls)
        weather=weather || static_cast<CWeatherValueEdit*>(control.value)->PendingChange();
    std::string weather_text=CStringTable().translate("ui_weather_tab").c_str();if(weather)weather_text+=" *";
    std::string lightning_text=CStringTable().translate("ui_lightning_page").c_str();if(lightning)lightning_text+=" *";
    m_weather_tab->TextItemControl()->SetText(weather_text.c_str());m_lightning_tab->TextItemControl()->SetText(lightning_text.c_str());
    m_dirty_status->SetTextST((m_lightning_mode?lightning:weather)?"ui_env_unsaved":"ui_env_saved");
    m_dirty_status->SetTextColor((m_lightning_mode?lightning:weather)?color_rgba(230,215,140,255):color_rgba(160,190,160,255));
}

void CUIWeatherEditor::CloseLists() {
    for(CUIComboBox* combo:{m_weather,m_frame,m_sky,m_clouds,m_ambient_definition,m_sun_definition,
        m_thunderbolt_definition,m_lightning_collection,m_lightning_particle,m_lightning_sound})
        static_cast<CWeatherComboBox*>(combo)->CloseEditorList();
}

void CUIWeatherEditor::SetLightningPage(bool enabled) {
    ClearLearningHint();CloseLists();
    m_lightning_mode=enabled;
    if(m_color_picker) m_color_picker->Show(false);
    m_active_color_property=-1;
    m_lightning_panel->Show(enabled);m_lightning_header->Show(enabled);m_assets_panel->Show(enabled);
    m_lightning_test->Show(enabled);m_files->Show(!enabled);
    m_file_panel->Show(!enabled && m_files_visible);
    m_weather_tab->Enable(enabled);m_lightning_tab->Enable(!enabled);
    for(auto* tab:{m_weather_tab,m_lightning_tab}) {
        tab->SetStateTextColor(color_rgba(235,220,150,255),S_Disabled);
        tab->SetStateTextColor(color_rgba(190,200,210,255),S_Enabled);
    }
    m_title->SetTextST("ui_env_title");
    m_context_help->SetWndSize(Fvector2().set(376.f,enabled?32.f:66.f));
    m_save->TextItemControl()->SetTextST(enabled?"ui_env_save_lightning":"ui_env_save_weather");
    m_revert->TextItemControl()->SetTextST(enabled?"ui_env_revert_lightning":"ui_env_revert_weather");
    if(enabled) {FillLightningParticles();FillLightningSounds();SyncLightningControls(true);}
    else if(m_descriptor) {RefreshDefinitionSelection();SyncPropertyControls();}
    RefreshCategory();m_next_dirty_update=0;
}

void CUIWeatherEditor::SyncLightningControls(bool assets) {
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;if(!effect) return;
    const auto settings=effect->GetEditorSettings();m_synchronizing=true;
    m_multi_strikes->SetCheck(settings.values[0]>.5f);
    for(auto& control:m_lightning_controls) {
        static_cast<CWeatherTrackBar*>(control.slider)->SetEditorValue(settings.values[control.index]);
        string64 text;
        if(lightning_editor::parameters[control.index].integral) xr_sprintf(text,"%.0f",settings.values[control.index]);
        else if(settings.values[control.index]>=1000.f) xr_sprintf(text,"%.0f",settings.values[control.index]);
        else xr_sprintf(text,"%.2f",settings.values[control.index]);
        static_cast<CWeatherValueEdit*>(control.value)->SetEditorText(text);
    }
    if(assets) {
        const auto found=std::find(m_lightning_particles.begin(),m_lightning_particles.end(),settings.particle);
        static_cast<CWeatherResourceCombo*>(m_lightning_particle)->SetResource(
            static_cast<int>(found - m_lightning_particles.begin()));
        const auto sound=std::find(m_lightning_sounds.begin(),m_lightning_sounds.end(),settings.sound);
        static_cast<CWeatherResourceCombo*>(m_lightning_sound)->SetResource(
            static_cast<int>(sound - m_lightning_sounds.begin()));
    }
    m_synchronizing=false;
}

bool CUIWeatherEditor::ApplyLightningAssets() {
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;if(!effect) return false;
    auto settings=effect->GetEditorSettings();const int i=m_lightning_particle->CurrentID();
    if(i>=0 && static_cast<unsigned>(i)<m_lightning_particles.size())settings.particle=m_lightning_particles[i];
    const int sound=m_lightning_sound->CurrentID();
    if(sound>=0 && static_cast<unsigned>(sound)<m_lightning_sounds.size())settings.sound=m_lightning_sounds[sound];
    if(!effect->ApplyEditorSettings(settings)) {SetStatus("ui_lightning_assets_error");return false;}
    return true;
}

void CUIWeatherEditor::OnSelectTab(CUIWindow* window,void*) {
    const bool lightning=window==m_lightning_tab;
    if(lightning==m_lightning_mode) return;
    CommitNumericEdits();
    if(m_lightning_mode && !ApplyLightningAssets()) return;
    SetLightningPage(lightning);
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
    SetStatus(m_lightning_mode?(effect && effect->IsCustomLightningEnabled()?"ui_lightning_ready":"ui_lightning_disabled"):
              "ui_weather_editor_status_ready");
}
void CUIWeatherEditor::OnLightningChanged(CUIWindow* window,void*) {
    if(m_synchronizing || !m_lightning_mode) return;
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;if(!effect)return;
    auto settings=effect->GetEditorSettings();
    for(auto& control:m_lightning_controls) if(control.slider==window) {
        lightning_editor::change(settings,control.index,control.slider->GetFValue());
        if(!effect->ApplyEditorSettings(settings))SetStatus("ui_lightning_assets_error");
        SyncLightningControls();return;
    }
}
void CUIWeatherEditor::OnLightningMulti(CUIWindow*,void*) {
    if(m_synchronizing || !m_lightning_mode) return;
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;if(!effect)return;
    auto settings=effect->GetEditorSettings();settings.values[0]=m_multi_strikes->GetCheck()?1.f:0.f;
    effect->ApplyEditorSettings(settings);SyncLightningControls();m_next_dirty_update=0;
}
void CUIWeatherEditor::OnLightningAssets(CUIWindow*,void*) {
    CommitNumericEdits();
    if(ApplyLightningAssets())SetStatus("ui_lightning_assets_applied");
}
void CUIWeatherEditor::OnTestLightning(CUIWindow*,void*) {
    CommitNumericEdits();
    if(!m_lightning_mode || !ApplyLightningAssets())return;
    auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
    const int selected=m_lightning_collection->CurrentID();
    if(!effect || selected<0 || static_cast<unsigned>(selected)>=m_thunderbolt_definitions.size() ||
        !effect->PreviewEditor(m_thunderbolt_definitions[selected].c_str(),m_descriptor?m_descriptor->bolt_duration:.5f)) {
        SetStatus("ui_lightning_test_error");return;
    }
    SetStatus("ui_lightning_test_started");
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
        static_cast<CWeatherResourceCombo*>(m_weather)->SetResource((int)weather_index);

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
    const u32 seconds=static_cast<u32>(iFloor(m_editor_time+.5f));string32 text;
    xr_sprintf(text,"%02u:%02u:%02u",seconds/3600,(seconds%3600)/60,seconds%60);m_time_value->SetText(text);
}

void CUIWeatherEditor::SyncPropertyControls() {
    if (!m_descriptor)
        return;
    m_synchronizing = true;
    for (SPropertyControl& control : m_property_controls) {
        const SPropertyInfo& property = property_info(control.property_index);
        const float maximum = control.property_index == 3
                                  ? std::min(property.maximum, m_descriptor->far_plane)
                                  : property.maximum;
        control.slider->SetOptFBounds(property.minimum, maximum);
        control.slider->SetStep(property.step);
        static_cast<CWeatherTrackBar*>(control.slider)
            ->SetEditorValue(GetPropertyValue(control.property_index));

        string32 value_text;
        xr_sprintf(value_text, "%.3f", GetPropertyValue(control.property_index));
        static_cast<CWeatherValueEdit*>(control.value)->SetEditorText(value_text);
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
    state.trees_amplitude = descriptor->trees_amplitude;
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
    descriptor.trees_amplitude = state.trees_amplitude;
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
        static_cast<CWeatherResourceCombo*>(m_sky)->SetResource((int)std::distance(m_sky_textures.begin(), sky));
    const auto clouds = std::find(m_cloud_textures.begin(), m_cloud_textures.end(),
                                  m_descriptor->clouds_texture_name);
    if (clouds != m_cloud_textures.end())
        static_cast<CWeatherResourceCombo*>(m_clouds)->SetResource((int)std::distance(m_cloud_textures.begin(), clouds));
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
        static_cast<CWeatherResourceCombo*>(m_ambient_definition)->SetResource(
            (int)std::distance(m_ambient_definitions.begin(), ambient));

    const auto sun = std::find(m_sun_definitions.begin(), m_sun_definitions.end(),
                               m_descriptor->lens_flare_id);
    if (sun != m_sun_definitions.end())
        static_cast<CWeatherResourceCombo*>(m_sun_definition)->SetResource((int)std::distance(m_sun_definitions.begin(), sun));

    const auto thunderbolt =
        std::find(m_thunderbolt_definitions.begin(), m_thunderbolt_definitions.end(),
                  m_descriptor->tb_id);
    if (thunderbolt != m_thunderbolt_definitions.end())
        static_cast<CWeatherResourceCombo*>(m_thunderbolt_definition)->SetResource(
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
        if (!hovered && window && window->IsShown() && window->CursorOverWindow()) {
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
                 m_lightning_mode?"ui_lightning_hint_collection":"ui_weather_editor_hint_thunderbolt_definition", false);
        use_hint(m_time_slider, "ui_weather_editor_hint_time", false);
        use_hint(m_time_value, "ui_weather_editor_hint_time", false);
        use_hint(m_new_weather_name, "ui_weather_editor_hint_new_file", false);
        use_hint(m_create_weather, "ui_weather_editor_hint_create_file", false);
        use_hint(m_add_frame, "ui_weather_editor_hint_add_section", false);
        use_hint(m_preview, "ui_weather_editor_hint_preview", false);
        use_hint(m_weather_tab,"ui_lightning_hint_page",false);
        use_hint(m_lightning_tab,"ui_lightning_hint_page",false);
        if(m_lightning_mode) use_hint(m_lightning_collection,"ui_lightning_hint_collection",false);
        use_hint(m_save,m_lightning_mode?"ui_lightning_hint_save":"ui_weather_editor_hint_save",false);
        use_hint(m_revert,m_lightning_mode?"ui_lightning_hint_revert":"ui_weather_editor_hint_revert",false);
        use_hint(m_close, "ui_weather_editor_hint_close", false);

        if(m_lightning_mode) {
            use_hint(m_multi_strikes,"ui_lightning_hint_enable_multi_strikes",false);
            use_hint(m_lightning_particle,"ui_lightning_hint_particle",false);
            use_hint(m_lightning_sound,"ui_lightning_hint_sound",false);
            use_hint(m_lightning_apply,"ui_lightning_hint_assets",false);
            use_hint(m_lightning_test,"ui_lightning_hint_test",false);
            for(const auto& control:m_lightning_controls) {
                use_hint(control.label,lightning_hint_ids[control.index],false);
                use_hint(control.slider,lightning_hint_ids[control.index],false);
                use_hint(control.value,lightning_hint_ids[control.index],false);
            }
        }
        if(!m_lightning_mode) for (const SColorControl& control : m_color_controls) {
            use_hint(control.header, property_hint_ids[control.first_property_index], false);
            use_hint(control.swatch, property_hint_ids[control.first_property_index], false);
        }
        if(!m_lightning_mode) for (const SPropertyControl& control : m_property_controls) {
            use_hint(control.label, property_hint_ids[control.property_index], false);
            use_hint(control.slider, property_hint_ids[control.property_index], false);
            use_hint(control.value, property_hint_ids[control.property_index], false);
        }
    }

    if(hovered && hint_id) m_context_help->SetTextST(hint_id);
    else m_context_help->SetTextST("ui_env_live_hint");
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
    case 36: return d.trees_amplitude;
    case 37: {
        const CEffect_Rain* rain = g_pGamePersistent->Environment().eff_Rain;
        return rain ? rain->GetWindTiltMultiplier() : 1.f;
    }
    case 38: {
        const CEffect_Rain* rain = g_pGamePersistent->Environment().eff_Rain;
        return rain ? rain->GetWindSpeedMultiplier() : 0.f;
    }
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
    case 36: d.trees_amplitude = weather_motion::tree_amplitude(value); break;
    case 37:
        if (g_pGamePersistent->Environment().eff_Rain)
            g_pGamePersistent->Environment().eff_Rain->SetWindTiltMultiplier(value);
        break;
    case 38:
        if (g_pGamePersistent->Environment().eff_Rain)
            g_pGamePersistent->Environment().eff_Rain->SetWindSpeedMultiplier(value);
        break;
    }

    if ((property_index == 5 || property_index == 6) &&
        g_pGamePersistent->Environment().eff_Rain)
        g_pGamePersistent->Environment().eff_Rain->RefreshTrajectories();
    if (property_index == 1)
        SyncPropertyControls();
}

void CUIWeatherEditor::OnWeatherChanged(CUIWindow*, void*) {
    CommitNumericEdits();
    if (m_synchronizing)
        return;
    const int index = m_weather->CurrentID();
    if (index < 0 || (u32)index >= m_weather_names.size())
        return;
    CEnvironment& environment = g_pGamePersistent->Environment();
    const auto old=environment.CurrentCycleName;
    m_weather_dirty[old]=m_weather_baselines[old]!=WeatherSignature(old);
    environment.UpdateWeatherEditorSession(m_weather_names[index], m_editor_time);
    FillFrameList();
    SelectFrame(0);
}

void CUIWeatherEditor::OnFrameChanged(CUIWindow*, void*) {
    CommitNumericEdits();
    if (!m_synchronizing)
        SelectFrame((u32)m_frame->CurrentID());
}

void CUIWeatherEditor::OnTimeChanged(CUIWindow*, void*) {
    CommitNumericEdits();
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
    if(m_lightning_mode) return;
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
    CommitNumericEdits();
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
    CommitNumericEdits();
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
    CommitNumericEdits();
    if (!m_descriptor)
        return;

    const std::string name = m_new_weather_name->GetText();
    CEnvironment& environment = g_pGamePersistent->Environment();
    std::string path;
    if (!environment.CreateWeather(name, *m_descriptor, &path)) {
        SetStatus("ui_weather_editor_status_create_error");
        return;
    }

    const auto old=environment.CurrentCycleName;
    m_weather_dirty[old]=m_weather_baselines[old]!=WeatherSignature(old);
    m_weather_baselines[name]=WeatherSignature(name);m_weather_dirty[name]=false;
    environment.UpdateWeatherEditorSession(name, 0.f);
    FillWeatherList();
    for (u32 index = 0; index < m_weather_names.size(); ++index) {
        if (m_weather_names[index] != name)
            continue;
        m_synchronizing = true;
        static_cast<CWeatherResourceCombo*>(m_weather)->SetResource((int)index);
        m_synchronizing = false;
        break;
    }
    FillFrameList();
    SelectFrame(0);
    m_new_weather_name->ClearText();
    SetStatus("ui_weather_editor_status_created", path.c_str());
}

void CUIWeatherEditor::OnPreview(CUIWindow*, void*) {
    CommitNumericEdits();
    if(m_lightning_mode && !ApplyLightningAssets()) return;
    SetStatus("ui_weather_editor_status_preview");
    EnterPreview();
}

void CUIWeatherEditor::EnterPreview() {
    if (!m_session_active || !IsShown())
        return;
    CommitNumericEdits();
    CloseLists();
    ClearLearningHint();
    m_color_picker->Show(false);
    m_active_color_property = -1;
    m_previewing = true;
    GetUICursor().Hide();
    release_weather_preview();
    // Keep the dialog/input receiver alive: F7 must work in release builds
    // and must not reach Level_input's developer quick-reload binding.
}

void CUIWeatherEditor::LeavePreview() {
    if (!m_previewing) return;
    if (g_pGameLevel) {
        // The editor captures subsequent key releases after leaving preview.
        if (auto* actor = smart_cast<CActor*>(Level().CurrentEntity())) {
            actor->IR_OnKeyboardRelease(kWPN_FIRE);
            actor->IR_OnKeyboardRelease(kWPN_ZOOM);
        }
    }
    m_previewing = false;
    GetUICursor().Show();
    SetStatus("ui_weather_editor_status_ready");
}

void CUIWeatherEditor::CloseEditorSession() {
    CommitNumericEdits();
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
    m_save_ok = false;
    CommitNumericEdits();
    if(m_lightning_mode) {
        auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
        if(!effect || !ApplyLightningAssets()) return;
        if(!effect->SaveEditorSettings()) {SetStatus("ui_lightning_save_error");return;}
        m_lightning_baseline=effect->GetEditorSettings();m_lightning_saved=m_lightning_baseline;
        SetStatus("ui_lightning_saved");
        m_save_ok = true;
        return;
    }
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
    m_weather_baselines[environment.CurrentCycleName]=WeatherSignature(environment.CurrentCycleName);
    m_weather_dirty[environment.CurrentCycleName]=false;
    if (environment.eff_Rain && !environment.eff_Rain->SaveWindSettings()) {
        SetStatus("ui_weather_editor_status_rain_save_error");
        return;
    }
    m_rain_tilt_baseline = environment.eff_Rain
        ? environment.eff_Rain->GetWindTiltMultiplier() : 1.f;
    m_rain_speed_baseline = environment.eff_Rain
        ? environment.eff_Rain->GetWindSpeedMultiplier() : 0.f;
    m_rain_tilt_saved=m_rain_tilt_baseline;m_rain_speed_saved=m_rain_speed_baseline;
    m_next_dirty_update=0;
    SetStatus("ui_weather_editor_status_saved", path.c_str());
    m_save_ok = true;
}

void CUIWeatherEditor::OnSaveClose(CUIWindow*, void*) {
    // F7: no file is written. The window is hidden, the session stays active, so the weather and the
    // time set in the editor remain fixed in the game until the editor is reopened and closed
    // with Esc, or "weather_editor off".
    CommitNumericEdits();
    if (m_lightning_mode && !ApplyLightningAssets())
        return;
    m_keep_session = true;
    m_close_requested = true;
}

void CUIWeatherEditor::OnRevert(CUIWindow*, void*) {
    CommitNumericEdits(false);
    if(m_lightning_mode) {
        auto* effect=g_pGamePersistent->Environment().eff_Thunderbolt;
        if(effect && effect->ApplyEditorSettings(m_lightning_baseline)) {
            FillLightningParticles(); FillLightningSounds(); SyncLightningControls(true);
            SetStatus("ui_lightning_reverted");
        } else SetStatus("ui_lightning_assets_error");
        return;
    }
    SDescriptorState* state = FindSnapshot(m_descriptor);
    if (!state)
        return;
    RestoreSnapshot(*state);
    if (g_pGamePersistent->Environment().eff_Rain) {
        g_pGamePersistent->Environment().eff_Rain->SetWindTiltMultiplier(m_rain_tilt_baseline);
        g_pGamePersistent->Environment().eff_Rain->SetWindSpeedMultiplier(m_rain_speed_baseline);
        g_pGamePersistent->Environment().eff_Rain->RefreshTrajectories();
    }
    RefreshTextureSelection();
    RefreshDefinitionSelection();
    SyncPropertyControls();
    SetStatus("ui_weather_editor_status_reverted");
}

void CUIWeatherEditor::OnClose(CUIWindow*, void*) { m_keep_session = false; m_close_requested = true; }

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

    if (force_show && weather_editor->IsPreviewing()) {
        weather_editor->LeavePreview();
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


bool WeatherEditorPreviewAllowsInput(const CUIDialogWnd* receiver, int key) {
    if (!weather_editor || receiver != weather_editor || !weather_editor->IsPreviewing())
        return false;
    if (key < 0)
        return true; // Mouse look.
    if (key == DIK_F7 || key == DIK_ESCAPE)
        return false;
    switch (get_binded_action(key)) {
    case kFWD: case kBACK: case kL_STRAFE: case kR_STRAFE:
    case kJUMP: case kCROUCH: case kACCEL: case kSPRINT_TOGGLE:
    case kL_LOOKOUT: case kR_LOOKOUT:
    case kWPN_1: case kWPN_2: case kWPN_3: case kWPN_4: case kWPN_5: case kWPN_6:
    case kWPN_FIRE: case kWPN_ZOOM: case kWPN_RELOAD: case kWPN_FUNC:
    case kWPN_FIREMODE_PREV: case kWPN_FIREMODE_NEXT: case kWPN_NEXT:
    case kWPN_ZOOM_INC: case kWPN_ZOOM_DEC: case kWPN_ZOOM_ALTER: case kWPN_INSPECT:
    case kNEXT_SLOT: case kPREV_SLOT: case kQUICK_GRENADE: case kQUICK_KICK: case kDROP:
        return true;
    default:
        return false;
    }
}
