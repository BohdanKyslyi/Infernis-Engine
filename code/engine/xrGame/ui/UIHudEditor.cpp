#include "stdafx.h"

#include "UIHudEditor.h"
#include "UI3tButton.h"
#include "UIBtnHint.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UICursor.h"
#include "UIEditBox.h"
#include "UIHelper.h"
#include "UIStatic.h"
#include "UITrackBar.h"
#include "UIXmlInit.h"
#include "xrUIXmlParser.h"
#include "../Actor.h"
#include "../Inventory.h"
#include "../Level.h"
#include "../Weapon.h"
#include "../game_cl_base.h"
#include "../player_hud.h"
#include "../string_table.h"
#include "../xr_level_controller.h"
#include "../../xrEngine/xr_input.h"
#include "../ui_base.h"

namespace {
class CHudEditorTrackBar : public CUITrackBar {
public:
    void SetEditorValue(float value) {
        clamp(value, m_f_min, m_f_max);
        m_f_val = value;
        UpdatePos();
    }
};

class CHudEditorComboBox : public CUIComboBox {
public:
    void CloseEditorList() {
        ShowList(false);
        Device.seqRender.Remove(this);
    }
};

struct SRange {
    float minimum;
    float maximum;
    float step;
};

static const SRange ranges[] = {
    {-2.f, 2.f, .0001f}, {-2.f, 2.f, .0001f}, {-2.f, 2.f, .0001f},
    {-360.f, 360.f, .01f}, {-360.f, 360.f, .01f}, {-360.f, 360.f, .01f},
    {-2.f, 2.f, .0001f}, {-2.f, 2.f, .0001f}, {-2.f, 2.f, .0001f},
    {-PI, PI, .0001f}, {-PI, PI, .0001f}, {-PI, PI, .0001f},
};

static LPCSTR const group_names[] = {
    "hands_position", "hands_orientation", "aim_hud_offset_pos", "aim_hud_offset_rot",
};

static LPCSTR const group_hint_ids[] = {
    "ui_hud_editor_hint_hands_position", "ui_hud_editor_hint_hands_orientation",
    "ui_hud_editor_hint_aim_position", "ui_hud_editor_hint_aim_rotation",
};

CUIHudEditor* hud_editor = nullptr;
CWeapon* crosshair_override_weapon = nullptr;
bool crosshair_override_active = false;
bool crosshair_override_hide = true;

std::string trim_copy(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool line_is_section(const std::string& line, const std::string& section) {
    const std::string value = trim_copy(line);
    if (value.empty() || value[0] != '[')
        return false;
    const size_t close = value.find(']');
    return close != std::string::npos && trim_copy(value.substr(1, close - 1)) == section;
}

bool line_is_any_section(const std::string& line) {
    const std::string value = trim_copy(line);
    return !value.empty() && value[0] == '[' && value.find(']') != std::string::npos;
}

bool line_is_key(const std::string& line, const std::string& key) {
    const std::string value = trim_copy(line);
    if (value.empty() || value[0] == ';' || value[0] == '#')
        return false;
    const size_t equal = value.find('=');
    return equal != std::string::npos && trim_copy(value.substr(0, equal)) == key;
}

bool find_section(const std::string& text, const std::string& section, size_t& begin,
                  size_t& end) {
    size_t cursor = 0;
    bool inside = false;
    while (cursor <= text.size()) {
        const size_t next = text.find('\n', cursor);
        const size_t line_end = next == std::string::npos ? text.size() : next + 1;
        const std::string line = text.substr(cursor, line_end - cursor);
        if (!inside && line_is_section(line, section)) {
            begin = cursor;
            inside = true;
        } else if (inside && line_is_any_section(line)) {
            end = cursor;
            return true;
        }
        if (next == std::string::npos)
            break;
        cursor = next + 1;
    }
    if (!inside)
        return false;
    end = text.size();
    return true;
}

void write_setting(std::string& text, const std::string& section, const std::string& key,
                   const std::string& value) {
    size_t section_begin = 0;
    size_t section_end = 0;
    if (!find_section(text, section, section_begin, section_end))
        return;

    const char* newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    size_t cursor = text.find('\n', section_begin);
    cursor = cursor == std::string::npos ? section_end : cursor + 1;
    while (cursor < section_end) {
        const size_t next = text.find('\n', cursor);
        const size_t line_end = next == std::string::npos ? text.size() : next + 1;
        if (line_is_key(text.substr(cursor, line_end - cursor), key)) {
            text.replace(cursor, line_end - cursor, key + " = " + value + newline);
            return;
        }
        if (next == std::string::npos)
            break;
        cursor = next + 1;
    }
    text.insert(section_end, key + " = " + value + newline);
}

bool read_config_file(LPCSTR relative, std::string& text) {
    IReader* reader = FS.r_open("$game_config$", relative);
    if (!reader)
        return false;
    text.assign(static_cast<const char*>(reader->pointer()), reader->length());
    FS.r_close(reader);
    return true;
}

bool find_section_file(LPCSTR section, std::string& relative, std::string& text) {
    FS_FileSet files;
    FS.file_list(files, "$game_config$", FS_ListFiles);
    for (const FS_File& file : files) {
        const std::string file_name = file.name.c_str();
        if (file_name.size() < 4 ||
            _stricmp(file_name.c_str() + file_name.size() - 4, ".ltx"))
            continue;
        std::string candidate;
        if (!read_config_file(file.name.c_str(), candidate))
            continue;
        size_t begin = 0;
        size_t end = 0;
        if (find_section(candidate, section, begin, end)) {
            relative = file.name.c_str();
            text.swap(candidate);
            return true;
        }
    }
    return false;
}

std::string vector_text(const Fvector& value) {
    string128 buffer;
    xr_sprintf(buffer, "%.6f, %.6f, %.6f", value.x, value.y, value.z);
    return buffer;
}

bool write_file(LPCSTR path, const std::string& text) {
    IWriter* writer = FS.w_open_ex(path);
    if (!writer || !writer->valid()) {
        FS.w_close(writer);
        return false;
    }
    writer->w(text.data(), text.size());
    FS.w_close(writer);
    return true;
}

bool save_profiles(LPCSTR section, const CUIHudEditor::SProfile* profiles,
                   bool hide_crosshair, std::string& saved_path) {
    std::string relative;
    std::string text;
    if (!find_section_file(section, relative, text)) {
        Msg("! HUD editor: cannot find source .ltx for section [%s]", section);
        return false;
    }
    const std::string original_text = text;

    static LPCSTR suffixes[] = {"", "_16x9"};
    for (u32 i = 0; i < 2; ++i) {
        const std::string suffix = suffixes[i];
        write_setting(text, section, "hands_position" + suffix,
                      vector_text(profiles[i].hands_position));
        write_setting(text, section, "hands_orientation" + suffix,
                      vector_text(profiles[i].hands_orientation));
        write_setting(text, section, "aim_hud_offset_pos" + suffix,
                      vector_text(profiles[i].aim_position));
        write_setting(text, section, "aim_hud_offset_rot" + suffix,
                      vector_text(profiles[i].aim_rotation));
    }
    write_setting(text, section, "zoom_hide_crosshair", hide_crosshair ? "true" : "false");

    string_path path;
    FS.update_path(path, "$game_config$", relative.c_str());
    string_path temporary;
    xr_strcpy(temporary, path);
    xr_strcat(temporary, ".hud_editor.tmp");
    if (!write_file(temporary, text))
        return false;

    string_path backup;
    xr_strcpy(backup, path);
    xr_strcat(backup, ".hud_editor.bak");
    if (!FS.exist(backup) && !write_file(backup, original_text)) {
        FS.file_delete(temporary);
        return false;
    }

    const DWORD attributes = GetFileAttributes(path);
    if (attributes != DWORD(-1) && (attributes & FILE_ATTRIBUTE_READONLY))
        SetFileAttributes(path, attributes & ~FILE_ATTRIBUTE_READONLY);
    FS.file_rename(temporary, path, true);
    if (_access(path, 0) != 0 || _access(temporary, 0) == 0)
        return false;

    saved_path = path;
    Msg("* HUD editor: saved section [%s] to [%s]", section, path);
    return true;
}

Fvector read_vector(LPCSTR section, LPCSTR key, const Fvector& fallback) {
    return pSettings->line_exist(section, key) ? pSettings->r_fvector3(section, key) : fallback;
}
} // namespace

CUIHudEditor::CUIHudEditor()
    : m_title(nullptr), m_weapon_name(nullptr), m_hud_section(nullptr), m_status(nullptr),
      m_profile(nullptr), m_force_aim(nullptr), m_hide_crosshair(nullptr),
      m_learning_mode(nullptr), m_learning_mode_label(nullptr), m_preview(nullptr),
      m_fire(nullptr), m_save(nullptr), m_revert(nullptr), m_close(nullptr),
      m_hud_item(nullptr), m_weapon(nullptr), m_snapshot_hide_crosshair(true),
      m_was_zoomed(false), m_synchronizing(false), m_session_active(false),
      m_previewing(false), m_close_requested(false), m_fire_release_frame(0),
      m_learning_hint_owner(nullptr), m_learning_hint_start(0) {
    for (u32 i = 0; i < 4; ++i)
        m_group_headers[i] = nullptr;
}

CUIHudEditor::~CUIHudEditor() {}

void CUIHudEditor::Init() {
    CUIXml xml;
    xml.Load(CONFIG_PATH, UI_PATH, "hud_editor.xml");
    CUIXmlInit::InitWindow(xml, "main", 0, this);

    m_title = UIHelper::CreateTextWnd(xml, "main:title", this);
    m_weapon_name = UIHelper::CreateTextWnd(xml, "main:weapon_value", this);
    m_hud_section = UIHelper::CreateTextWnd(xml, "main:section_value", this);
    m_status = UIHelper::CreateTextWnd(xml, "main:status", this);

    m_profile = xr_new<CHudEditorComboBox>();
    m_profile->SetAutoDelete(true);
    AttachChild(m_profile);
    CUIXmlInit::InitComboBox(xml, "main:profile", 0, m_profile);
    m_profile->AddItem_(CStringTable().translate("ui_hud_editor_profile_standard").c_str(), 0);
    m_profile->AddItem_(CStringTable().translate("ui_hud_editor_profile_wide").c_str(), 1);

    m_force_aim = UIHelper::CreateCheck(xml, "main:force_aim", this);
    m_hide_crosshair = UIHelper::CreateCheck(xml, "main:hide_crosshair", this);
    m_learning_mode = UIHelper::CreateCheck(xml, "main:learning_mode", this);
    m_learning_mode->SetCheck(false);
    m_learning_mode_label = UIHelper::CreateTextWnd(xml, "main:learning_mode_label", this);
    m_preview = UIHelper::Create3tButton(xml, "main:preview", this);
    m_fire = UIHelper::Create3tButton(xml, "main:fire", this);
    m_save = UIHelper::Create3tButton(xml, "main:save", this);
    m_revert = UIHelper::Create3tButton(xml, "main:revert", this);
    m_close = UIHelper::Create3tButton(xml, "main:close", this);

    for (u32 group = 0; group < 4; ++group) {
        const bool right = group >= 2;
        const float base_x = right ? 590.f : 18.f;
        const float base_y = (group % 2) ? 316.f : 150.f;

        CUITextWnd* header = xr_new<CUITextWnd>();
        header->SetAutoDelete(true);
        AttachChild(header);
        CUIXmlInit::InitTextWnd(xml, "main:group_header_template", 0, header);
        header->SetWndPos(Fvector2().set(base_x, base_y));
        header->SetText(group_names[group]);
        m_group_headers[group] = header;

        for (u32 axis = 0; axis < 3; ++axis) {
            const u32 index = group * 3 + axis;
            const float row_y = base_y + 30.f + axis * 34.f;

            CUITextWnd* axis_label = xr_new<CUITextWnd>();
            axis_label->SetAutoDelete(true);
            AttachChild(axis_label);
            CUIXmlInit::InitTextWnd(xml, "main:axis_label_template", 0, axis_label);
            axis_label->SetWndPos(Fvector2().set(base_x, row_y - 3.f));
            axis_label->SetText(axis == 0 ? "X" : axis == 1 ? "Y" : "Z");

            CUI3tButton* decrease = xr_new<CUI3tButton>();
            decrease->SetAutoDelete(true);
            AttachChild(decrease);
            CUIXmlInit::Init3tButton(xml, "main:value_step_button_template", 0, decrease);
            decrease->SetWndPos(Fvector2().set(base_x + 24.f, row_y - 5.f));
            decrease->TextItemControl()->SetText("-");

            CHudEditorTrackBar* slider = xr_new<CHudEditorTrackBar>();
            slider->SetAutoDelete(true);
            AttachChild(slider);
            CUIXmlInit::InitTrackBar(xml, "main:value_slider_template", 0, slider);
            slider->SetWndPos(Fvector2().set(base_x + 52.f, row_y));
            slider->SetOptFBounds(ranges[index].minimum, ranges[index].maximum);
            slider->SetStep(ranges[index].step);

            CUI3tButton* increase = xr_new<CUI3tButton>();
            increase->SetAutoDelete(true);
            AttachChild(increase);
            CUIXmlInit::Init3tButton(xml, "main:value_step_button_template", 0, increase);
            increase->SetWndPos(Fvector2().set(base_x + 242.f, row_y - 5.f));
            increase->TextItemControl()->SetText("+");

            CUIEditBox* edit = xr_new<CUIEditBox>();
            edit->SetAutoDelete(true);
            AttachChild(edit);
            CUIXmlInit::InitEditBox(xml, "main:value_edit_template", 0, edit);
            edit->SetWndPos(Fvector2().set(base_x + 280.f, row_y - 6.f));

            SControl control = {index, slider, edit, decrease, increase};
            m_controls.push_back(control);
            Register(decrease);
            Register(slider);
            Register(increase);
            Register(edit);
            AddCallback(decrease, BUTTON_CLICKED,
                        CUIWndCallback::void_function(this, &CUIHudEditor::OnValueStep));
            AddCallback(slider, BUTTON_CLICKED,
                        CUIWndCallback::void_function(this, &CUIHudEditor::OnValueChanged));
            AddCallback(increase, BUTTON_CLICKED,
                        CUIWndCallback::void_function(this, &CUIHudEditor::OnValueStep));
            AddCallback(edit, EDIT_TEXT_COMMIT,
                        CUIWndCallback::void_function(this, &CUIHudEditor::OnValueCommitted));
            AddCallback(edit, EDIT_TEXT_CANCEL,
                        CUIWndCallback::void_function(this, &CUIHudEditor::OnValueCommitted));
        }
    }

    Register(m_profile);
    Register(m_force_aim);
    Register(m_hide_crosshair);
    Register(m_learning_mode);
    Register(m_preview);
    Register(m_fire);
    Register(m_save);
    Register(m_revert);
    Register(m_close);
    AddCallback(m_profile, LIST_ITEM_SELECT,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnProfileChanged));
    AddCallback(m_force_aim, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnForceAimChanged));
    AddCallback(m_hide_crosshair, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnHideCrosshairChanged));
    AddCallback(m_learning_mode, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnLearningModeChanged));
    AddCallback(m_preview, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnPreview));
    AddCallback(m_fire, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnFire));
    AddCallback(m_save, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnSave));
    AddCallback(m_revert, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnRevert));
    AddCallback(m_close, BUTTON_CLICKED,
                CUIWndCallback::void_function(this, &CUIHudEditor::OnClose));
}

void CUIHudEditor::Show(bool status) {
    if (status && !IsShown()) {
        inherited::Show(true);
        m_close_requested = false;
        if (m_session_active && m_previewing) {
            m_previewing = false;
            SelectActiveWeapon(false);
            SyncControls();
            return;
        }
        m_session_active = true;
        m_previewing = false;
        if (!SelectActiveWeapon(true))
            SetStatus("ui_hud_editor_status_no_weapon");
        return;
    }

    if (!status && IsShown()) {
        ClearLearningHint();
        static_cast<CHudEditorComboBox*>(m_profile)->CloseEditorList();
        if (!m_previewing) {
            SetForcedAim(false);
            if (m_hud_item) {
                m_synchronizing = true;
                m_profile->SetItemToken(UI().is_widescreen() ? 1 : 0);
                m_synchronizing = false;
                ApplySelectedProfile();
            }
            crosshair_override_active = false;
            crosshair_override_weapon = nullptr;
            m_session_active = false;
        }
    }
    inherited::Show(status);
}

void CUIHudEditor::Update() {
    inherited::Update();

    if (m_fire_release_frame && Device.dwFrame >= m_fire_release_frame) {
        if (m_weapon)
            m_weapon->Action(kWPN_FIRE, CMD_STOP);
        m_fire_release_frame = 0;
    }
    if (m_close_requested) {
        m_close_requested = false;
        CloseEditorSession();
        return;
    }
    if (!SelectActiveWeapon(false)) {
        UpdateLearningHint();
        return;
    }
    if (m_force_aim->GetCheck() && !m_weapon->IsZoomed())
        m_weapon->OnZoomIn();
    ApplySelectedProfile();
    UpdateLearningHint();
}

void CUIHudEditor::SendMessage(CUIWindow* pWnd, s16 msg, void* pData) {
    inherited::SendMessage(pWnd, msg, pData);
    CUIWndCallback::OnEvent(pWnd, msg, pData);
}

bool CUIHudEditor::OnKeyboardAction(int dik, EUIMessages keyboard_action) {
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
        if (dik == DIK_F8) {
            FireOnce();
            return true;
        }
    }
    return inherited::OnKeyboardAction(dik, keyboard_action);
}

bool CUIHudEditor::SelectActiveWeapon(bool force) {
    CActor* actor = Actor();
    CWeapon* weapon = actor ? smart_cast<CWeapon*>(actor->inventory().ActiveItem()) : nullptr;
    attachable_hud_item* item = g_player_hud ? g_player_hud->attached_item(0) : nullptr;
    if (!weapon || !item || item->m_parent_hud_item != weapon) {
        if (m_weapon) {
            SetForcedAim(false);
            crosshair_override_active = false;
            crosshair_override_weapon = nullptr;
            m_weapon = nullptr;
            m_hud_item = nullptr;
            m_section = shared_str();
            m_weapon_name->SetText("-");
            m_hud_section->SetText("-");
            SetStatus("ui_hud_editor_status_no_weapon");
        }
        return false;
    }
    if (!force && m_weapon == weapon && m_hud_item == item && m_section == item->m_sect_name)
        return true;

    SetForcedAim(false);
    m_weapon = weapon;
    m_hud_item = item;
    m_section = item->m_sect_name;
    m_weapon_name->SetText(weapon->cNameSect().c_str());
    m_hud_section->SetText(m_section.c_str());
    CaptureProfiles();
    m_profile->SetItemToken(UI().is_widescreen() ? 1 : 0);
    m_snapshot_hide_crosshair = !!READ_IF_EXISTS(
        pSettings, r_bool, m_section.c_str(), "zoom_hide_crosshair", true);
    m_hide_crosshair->SetCheck(m_snapshot_hide_crosshair);
    crosshair_override_weapon = weapon;
    crosshair_override_active = true;
    crosshair_override_hide = m_snapshot_hide_crosshair;
    m_force_aim->SetCheck(false);
    m_was_zoomed = weapon->IsZoomed();
    SyncControls();
    ApplySelectedProfile();
    SetStatus("ui_hud_editor_status_ready");
    return true;
}

void CUIHudEditor::CaptureProfiles() {
    const Fvector zero = Fvector().set(0.f, 0.f, 0.f);
    m_profiles[0].hands_position = read_vector(m_section.c_str(), "hands_position", zero);
    m_profiles[0].hands_orientation = read_vector(m_section.c_str(), "hands_orientation", zero);
    m_profiles[0].aim_position = read_vector(m_section.c_str(), "aim_hud_offset_pos", zero);
    m_profiles[0].aim_rotation = read_vector(m_section.c_str(), "aim_hud_offset_rot", zero);
    m_profiles[1].hands_position =
        read_vector(m_section.c_str(), "hands_position_16x9", m_profiles[0].hands_position);
    m_profiles[1].hands_orientation = read_vector(
        m_section.c_str(), "hands_orientation_16x9", m_profiles[0].hands_orientation);
    m_profiles[1].aim_position =
        read_vector(m_section.c_str(), "aim_hud_offset_pos_16x9", m_profiles[0].aim_position);
    m_profiles[1].aim_rotation =
        read_vector(m_section.c_str(), "aim_hud_offset_rot_16x9", m_profiles[0].aim_rotation);
    m_snapshot_profiles[0] = m_profiles[0];
    m_snapshot_profiles[1] = m_profiles[1];
}

void CUIHudEditor::ApplySelectedProfile() {
    if (!m_hud_item)
        return;
    const SProfile& profile = m_profiles[clampr(m_profile->CurrentID(), 0, 1)];
    m_hud_item->m_measures.m_hands_attach[0] = profile.hands_position;
    m_hud_item->m_measures.m_hands_attach[1] = profile.hands_orientation;
    m_hud_item->m_measures.m_hands_offset[0][1] = profile.aim_position;
    m_hud_item->m_measures.m_hands_offset[1][1] = profile.aim_rotation;
}

void CUIHudEditor::SyncControls() {
    m_synchronizing = true;
    for (SControl& control : m_controls)
        SyncControl(control);
    m_synchronizing = false;
}

void CUIHudEditor::SyncControl(SControl& control) {
    static_cast<CHudEditorTrackBar*>(control.slider)->SetEditorValue(GetValue(control.value_index));
    string32 value;
    xr_sprintf(value, "%.6f", GetValue(control.value_index));
    control.edit->SetText(value);
}

void CUIHudEditor::RestoreSnapshot() {
    m_profiles[0] = m_snapshot_profiles[0];
    m_profiles[1] = m_snapshot_profiles[1];
    if (m_weapon) {
        m_hide_crosshair->SetCheck(m_snapshot_hide_crosshair);
        crosshair_override_weapon = m_weapon;
        crosshair_override_active = true;
        crosshair_override_hide = m_snapshot_hide_crosshair;
    }
    SyncControls();
    ApplySelectedProfile();
}

void CUIHudEditor::SetForcedAim(bool enabled) {
    if (!m_force_aim)
        return;
    m_force_aim->SetCheck(enabled);
    if (!m_weapon || !g_pGameLevel)
        return;
    if (enabled) {
        m_was_zoomed = m_weapon->IsZoomed();
        if (!m_weapon->IsZoomed() && m_weapon->IsZoomEnabled())
            m_weapon->OnZoomIn();
    } else if (!m_was_zoomed && m_weapon->IsZoomed()) {
        m_weapon->OnZoomOut();
    }
}

void CUIHudEditor::FireOnce() {
    if (!m_weapon || m_fire_release_frame)
        return;
    m_weapon->Action(kWPN_FIRE, CMD_START);
    m_fire_release_frame = Device.dwFrame + 1;
    SetStatus("ui_hud_editor_status_fired");
}

void CUIHudEditor::ClearLearningHint() {
    if (g_statHint && m_learning_hint_owner && g_statHint->Owner() == m_learning_hint_owner)
        g_statHint->Discard();
    m_learning_hint_owner = nullptr;
    m_learning_hint_start = 0;
}

void CUIHudEditor::UpdateLearningHint() {
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

    use_hint(m_learning_mode, "ui_hud_editor_hint_learning_mode", true);
    use_hint(m_learning_mode_label, "ui_hud_editor_hint_learning_mode", true);
    if (!hovered && m_learning_mode->GetCheck()) {
        use_hint(m_title, "ui_hud_editor_hint_title", false);
        use_hint(m_weapon_name, "ui_hud_editor_hint_weapon", false);
        use_hint(m_hud_section, "ui_hud_editor_hint_section", false);
        use_hint(m_profile, "ui_hud_editor_hint_profile", false);
        use_hint(m_force_aim, "ui_hud_editor_hint_force_aim", false);
        use_hint(m_hide_crosshair, "ui_hud_editor_hint_hide_crosshair", false);
        use_hint(m_preview, "ui_hud_editor_hint_preview", false);
        use_hint(m_fire, "ui_hud_editor_hint_fire", false);
        use_hint(m_save, "ui_hud_editor_hint_save", false);
        use_hint(m_revert, "ui_hud_editor_hint_revert", false);
        use_hint(m_close, "ui_hud_editor_hint_close", false);
        use_hint(m_status, "ui_hud_editor_hint_status", false);
        for (u32 group = 0; group < 4; ++group)
            use_hint(m_group_headers[group], group_hint_ids[group], false);
        for (const SControl& control : m_controls) {
            const LPCSTR id = group_hint_ids[control.value_index / 3];
            use_hint(control.decrease, id, false);
            use_hint(control.slider, id, false);
            use_hint(control.increase, id, false);
            use_hint(control.edit, id, false);
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
    if (g_statHint->Owner() && g_statHint->Owner() != hovered)
        g_statHint->Discard();
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

void CUIHudEditor::SetStatus(LPCSTR string_id, ...) {
    string512 text;
    va_list args;
    va_start(args, string_id);
    const shared_str format = CStringTable().translate(string_id);
    vsprintf(text, format.c_str(), args);
    va_end(args);
    m_status->SetText(text);
}

float CUIHudEditor::GetValue(u32 index) const {
    const SProfile& profile = m_profiles[clampr(m_profile->CurrentID(), 0, 1)];
    const u32 group = index / 3;
    const u32 axis = index % 3;
    const Fvector* vector = group == 0 ? &profile.hands_position
        : group == 1 ? &profile.hands_orientation
        : group == 2 ? &profile.aim_position : &profile.aim_rotation;
    return (*vector)[axis];
}

void CUIHudEditor::SetValue(u32 index, float value) {
    SProfile& profile = m_profiles[clampr(m_profile->CurrentID(), 0, 1)];
    const u32 group = index / 3;
    const u32 axis = index % 3;
    Fvector* vector = group == 0 ? &profile.hands_position
        : group == 1 ? &profile.hands_orientation
        : group == 2 ? &profile.aim_position : &profile.aim_rotation;
    (*vector)[axis] = value;
}

void CUIHudEditor::OnProfileChanged(CUIWindow*, void*) {
    if (m_synchronizing)
        return;
    SyncControls();
    ApplySelectedProfile();
    SetStatus(m_profile->CurrentID() ? "ui_hud_editor_status_profile_wide"
                                     : "ui_hud_editor_status_profile_standard");
}

void CUIHudEditor::OnValueChanged(CUIWindow* window, void*) {
    if (m_synchronizing)
        return;
    for (SControl& control : m_controls) {
        if (control.slider != window)
            continue;
        SetValue(control.value_index, control.slider->GetFValue());
        m_synchronizing = true;
        SyncControl(control);
        m_synchronizing = false;
        ApplySelectedProfile();
        return;
    }
}

void CUIHudEditor::OnValueCommitted(CUIWindow* window, void*) {
    if (m_synchronizing)
        return;
    for (SControl& control : m_controls) {
        if (control.edit != window)
            continue;
        LPCSTR text = control.edit->GetText();
        char* end = nullptr;
        const double parsed = strtod(text, &end);
        while (end && (*end == ' ' || *end == '\t'))
            ++end;
        if (!text[0] || end == text || (end && *end) || !std::isfinite(parsed)) {
            m_synchronizing = true;
            SyncControl(control);
            m_synchronizing = false;
            SetStatus("ui_hud_editor_status_invalid_number");
            return;
        }
        const SRange& range = ranges[control.value_index];
        const float value = clampr((float)parsed, range.minimum, range.maximum);
        SetValue(control.value_index, value);
        m_synchronizing = true;
        SyncControl(control);
        m_synchronizing = false;
        ApplySelectedProfile();
        SetStatus("ui_hud_editor_status_number_applied");
        return;
    }
}

void CUIHudEditor::OnValueStep(CUIWindow* window, void*) {
    if (m_synchronizing)
        return;
    for (SControl& control : m_controls) {
        const bool decrease = control.decrease == window;
        if (!decrease && control.increase != window)
            continue;
        const SRange& range = ranges[control.value_index];
        const float direction = decrease ? -1.f : 1.f;
        const float value = clampr(GetValue(control.value_index) + direction * range.step,
                                   range.minimum, range.maximum);
        SetValue(control.value_index, value);
        m_synchronizing = true;
        SyncControl(control);
        m_synchronizing = false;
        ApplySelectedProfile();
        return;
    }
}

void CUIHudEditor::OnForceAimChanged(CUIWindow*, void*) {
    SetForcedAim(m_force_aim->GetCheck());
}

void CUIHudEditor::OnHideCrosshairChanged(CUIWindow*, void*) {
    crosshair_override_weapon = m_weapon;
    crosshair_override_active = m_weapon != nullptr;
    crosshair_override_hide = m_hide_crosshair->GetCheck();
}

void CUIHudEditor::OnLearningModeChanged(CUIWindow*, void*) {
    if (!m_learning_mode->GetCheck())
        ClearLearningHint();
}

void CUIHudEditor::OnPreview(CUIWindow*, void*) { EnterPreview(); }

void CUIHudEditor::OnFire(CUIWindow*, void*) { FireOnce(); }

void CUIHudEditor::OnSave(CUIWindow*, void*) {
    if (!m_weapon || !m_hud_item)
        return;
    std::string path;
    if (!save_profiles(m_section.c_str(), m_profiles, m_hide_crosshair->GetCheck(), path)) {
        SetStatus("ui_hud_editor_status_save_error");
        return;
    }
    m_snapshot_profiles[0] = m_profiles[0];
    m_snapshot_profiles[1] = m_profiles[1];
    m_snapshot_hide_crosshair = m_hide_crosshair->GetCheck();
    SetStatus("ui_hud_editor_status_saved", path.c_str());
}

void CUIHudEditor::OnRevert(CUIWindow*, void*) {
    RestoreSnapshot();
    SetStatus("ui_hud_editor_status_reverted");
}

void CUIHudEditor::OnClose(CUIWindow*, void*) { m_close_requested = true; }

void CUIHudEditor::EnterPreview() {
    if (!IsShown())
        return;
    m_previewing = true;
    SetStatus("ui_hud_editor_status_preview");
    HideDialog();
}

void CUIHudEditor::CloseEditorSession() {
    if (m_fire_release_frame) {
        if (m_weapon)
            m_weapon->Action(kWPN_FIRE, CMD_STOP);
        m_fire_release_frame = 0;
    }
    if (IsShown()) {
        m_previewing = false;
        HideDialog();
        return;
    }
    m_previewing = false;
    SetForcedAim(false);
    if (m_hud_item) {
        m_synchronizing = true;
        m_profile->SetItemToken(UI().is_widescreen() ? 1 : 0);
        m_synchronizing = false;
        ApplySelectedProfile();
    }
    crosshair_override_active = false;
    crosshair_override_weapon = nullptr;
    m_session_active = false;
}

void ToggleHudEditor(bool force_show, bool force_hide) {
    if (!g_pGameLevel || !CurrentGameUI())
        return;
    if (!hud_editor) {
        hud_editor = xr_new<CUIHudEditor>();
        hud_editor->Init();
    }
    if (force_hide) {
        hud_editor->CloseEditorSession();
        return;
    }
    const bool show = force_show || !hud_editor->IsShown();
    if (show && !hud_editor->IsShown())
        hud_editor->ShowDialog(false);
    else if (!force_show && hud_editor->IsShown())
        hud_editor->CloseEditorSession();
}

void PreviewHudEditor() {
    if (hud_editor)
        hud_editor->EnterPreview();
}

void DestroyHudEditor() {
    if (!hud_editor)
        return;
    hud_editor->CloseEditorSession();
    xr_delete(hud_editor);
}

bool HudEditorShown() { return hud_editor && hud_editor->IsSessionActive(); }

bool HudEditorCrosshairOverride(const CWeapon* weapon, bool& visible) {
    if (!crosshair_override_active || !weapon || weapon != crosshair_override_weapon)
        return false;
    visible = !weapon->IsZoomed() || !crosshair_override_hide;
    return true;
}
