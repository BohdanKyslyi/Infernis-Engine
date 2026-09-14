#include "StdAfx.h"
#include "UIComboBox.h"
#include "UITextureMaster.h"
#include "UIScrollBar.h"
#include "uilistboxitem.h"
#include "../string_table.h"

#define CB_HEIGHT 20.0f

namespace {
xr_string SoundDeviceDisplayName(LPCSTR name, bool keepEndpointType = false) {
    xr_string text = name ? name : "";
    static const char prefix[] = "OpenAL Soft on ";
    if (text.compare(0, sizeof(prefix) - 1, prefix) == 0)
        text.erase(0, sizeof(prefix) - 1);

    if (keepEndpointType)
        return text;

    // Strip only recognized endpoint labels, keeping parentheses in actual
    // hardware names (e.g. "USB DAC (Studio)") intact.
    static const char* wrappers[] = {
        "Speakers (", "Headphones (", "Headset (", "Line Out (",
        "Digital Audio (S/PDIF) (", "Digital Output ("
    };
    for (const char* wrapper : wrappers) {
        const size_t length = xr_strlen(wrapper);
        if (text.size() <= length + 1 || text.compare(0, length, wrapper) != 0 || text.back() != ')')
            continue;
        int depth = 0;
        bool outerPair = true;
        for (size_t i = length - 1; i < text.size(); ++i) {
            if (text[i] == '(') ++depth;
            if (text[i] == ')') --depth;
            if (depth <= 0 && i != text.size() - 1) {
                outerPair = false;
                break;
            }
        }
        if (outerPair && depth == 0)
            return text.substr(length, text.size() - length - 1);
    }
    return text;
}
} // namespace

CUIComboBox::CUIComboBox() {
    AttachChild(&m_frameLine);
    AttachChild(&m_text);

    AttachChild(&m_list_frame);
    m_list_frame.AttachChild(&m_list_box);

    m_iListHeight = 0;
    m_bInited = false;
    m_eState = LIST_FONDED;
    m_textColor[0] = 0xff00ff00;
}

CUIComboBox::~CUIComboBox() {}

void CUIComboBox::SetListLength(int length) {
    R_ASSERT(0 == m_iListHeight);
    m_iListHeight = length;
}

void CUIComboBox::InitComboBox(Fvector2 pos, float width) {
    float lb_text_offset = 5.0f;

    m_bInited = true;
    if (0 == m_iListHeight)
        m_iListHeight = 4;

    CUIWindow::SetWndPos(pos);
    CUIWindow::SetWndSize(Fvector2().set(width, CB_HEIGHT));

    m_frameLine.InitIB(Fvector2().set(0, 0), Fvector2().set(width, CB_HEIGHT));

    m_frameLine.InitState(S_Enabled, "ui_inGame2_combobox_linetext"); // horizontal by default
    m_frameLine.InitState(S_Highlighted, "ui_inGame2_combobox_linetext");

    // Edit Box on left side of frame line
    m_text.SetWndPos(Fvector2().set(lb_text_offset, 0.0f));
    m_text.SetWndSize(Fvector2().set(width - lb_text_offset, CB_HEIGHT));

    m_text.SetVTextAlignment(valCenter);
    m_text.SetTextColor(m_textColor[0]);
    m_text.Enable(false);

    // height of list equal to height of ONE element
    float item_height = CUITextureMaster::GetTextureHeight("ui_inGame2_combobox_line_b");

    m_list_box.SetWndPos(Fvector2().set(lb_text_offset, 0.0f));
    m_list_box.SetWndSize(Fvector2().set(width - lb_text_offset, item_height * m_iListHeight));
    m_list_box.InitScrollView();
    m_list_box.SetTextColor(m_textColor[0]);
    m_list_box.SetSelectionTexture("ui_inGame2_combobox_line");
    m_list_box.SetItemHeight(CUITextureMaster::GetTextureHeight("ui_inGame2_combobox_line_b"));
    // frame(texture) for list
    m_list_frame.InitTexture("ui_inGame2_combobox");
    m_list_frame.SetWndSize(Fvector2().set(width, m_list_box.GetItemHeight() * m_iListHeight));
    m_list_frame.SetWndPos(Fvector2().set(0.0f, CB_HEIGHT));

    m_list_box.Show(true);
    m_list_frame.Show(false);
    m_list_box.SetMessageTarget(this);
}

CUIListBoxItem* CUIComboBox::AddItem_(LPCSTR str, int _data) {
    R_ASSERT2(m_bInited, "Can't add item to ComboBox before Initialization");
    CUIListBoxItem* itm = m_list_box.AddTextItem(str);
    itm->SetData((void*)(__int64)_data);
    return itm;
}

void CUIComboBox::OnListItemSelect() {
    m_text.SetText(m_list_box.GetSelectedText());
    CUIListBoxItem* itm = m_list_box.GetSelectedItem();

    int bk_itoken_id = m_itoken_id;

    m_itoken_id = (int)(__int64)itm->GetData();
    ShowList(false);

    if (bk_itoken_id != m_itoken_id)
        GetMessageTarget()->SendMessage(this, LIST_ITEM_SELECT, NULL);
}

void CUIComboBox::SetText(LPCSTR text) {
    if (!text)
        return;

    m_text.SetText(text);
}

void CUIComboBox::disable_id(int id) {
    if (m_disabled.end() == std::find(m_disabled.begin(), m_disabled.end(), id))
        m_disabled.push_back(id);
}

void CUIComboBox::enable_id(int id) {
    xr_vector<int>::iterator it = std::find(m_disabled.begin(), m_disabled.end(), id);

    if (m_disabled.end() != it)
        m_disabled.erase(it);
}

void CUIComboBox::SetCurrentOptValue() {
    CUIOptionsItem::SetCurrentOptValue();

    m_list_box.Clear();
    xr_token* tok = GetOptToken();

    if (m_entry == "snd_device") {
        const xr_string current = GetOptTokenValue();
        CUIListBoxItem* selected = nullptr;
        for (xr_token* device = tok; device && device->name; ++device) {
            if (std::find(m_disabled.begin(), m_disabled.end(), device->id) != m_disabled.end())
                continue;

            xr_string display = SoundDeviceDisplayName(device->name);
            // Two endpoints can belong to the same sound card. Keep their type
            // when shortening would make the choices indistinguishable.
            for (const xr_token* other = tok; other && other->name; ++other) {
                if (other->id != device->id && SoundDeviceDisplayName(other->name) == display) {
                    display = SoundDeviceDisplayName(device->name, true);
                    break;
                }
            }
            CUIListBoxItem* item = AddItem_(device->name, device->id);
            item->SetText(display.c_str());
            item->SetTAG(device->id); // SetItemToken/UndoOptValue select by TAG.
            if (current == device->name)
                selected = item;
        }
        // Match the original token, never the potentially shortened label.
        m_list_box.SetSelected(selected);
        m_text.SetText(selected ? selected->GetText() : SoundDeviceDisplayName(current.c_str()).c_str());
        m_itoken_id = selected ? (int)(__int64)selected->GetData() : 0;
        return;
    }

    while (tok->name) {
        if (m_disabled.end() == std::find(m_disabled.begin(), m_disabled.end(), tok->id)) {
            AddItem_(tok->name, tok->id);
        }
        tok++;
    }

    LPCSTR cur_val = *CStringTable().translate(GetOptTokenValue());
    m_text.SetText(cur_val);
    m_list_box.SetSelectedText(cur_val);

    CUIListBoxItem* itm = m_list_box.GetSelectedItem();
    if (itm)
        m_itoken_id = (int)(__int64)itm->GetData();
    else
        m_itoken_id = 1; // first
}

void CUIComboBox::SaveBackUpOptValue() {
    CUIOptionsItem::SaveBackUpOptValue();
    m_opt_backup_value = m_itoken_id;
}

void CUIComboBox::UndoOptValue() {
    m_itoken_id = m_opt_backup_value;
    OnChangedOptValue();
    SetItemToken(m_itoken_id);
    CUIOptionsItem::UndoOptValue();
}

void CUIComboBox::SaveOptValue() {
    CUIOptionsItem::SaveOptValue();
    xr_token* tok = GetOptToken();
    if (tok) {
        LPCSTR cur_val = get_token_name(tok, m_itoken_id);
        SaveOptStringValue(cur_val);
    }
}

bool CUIComboBox::IsChangedOptValue() const { return (m_opt_backup_value != m_itoken_id); }

LPCSTR CUIComboBox::GetText() { return m_text.GetText(); }

u32 CUIComboBox::GetSize() { return m_list_box.GetSize(); }

LPCSTR CUIComboBox::GetTextOf(int index) {
    if (u32(index) >= GetSize())
        return "";

    return m_list_box.GetText(index);
}

void CUIComboBox::SetItemIDX(int idx) {
    m_list_box.SetSelectedIDX(idx);
    CUIListBoxItem* itm = m_list_box.GetSelectedItem();
    m_itoken_id = (int)(__int64)itm->GetData();

    m_text.SetText(m_list_box.GetSelectedText());

    OnChangedOptValue();
}

void CUIComboBox::SetItemToken(int tok_id) {
    int idx = m_list_box.GetIdxByTAG(tok_id);
    SetItemIDX(idx);
}

void CUIComboBox::OnBtnClicked() { ShowList(!m_list_frame.IsShown()); }

void CUIComboBox::ShowList(bool bShow) {
    if (bShow) {
        SetHeight(m_text.GetHeight() + m_list_box.GetHeight());
        m_list_frame.Show(true);
        m_eState = LIST_EXPANDED;
        GetParent()->SetCapture(this, true);
    } else {
        m_list_frame.Show(false);
        SetHeight(m_frameLine.GetHeight());
        m_eState = LIST_FONDED;
        GetParent()->SetCapture(this, false);
    }
}

void CUIComboBox::Update() {
    CUIWindow::Update();
    if (!m_bIsEnabled) {
        m_frameLine.SetCurrentState(S_Disabled);
        m_text.SetTextColor(m_textColor[1]);
    } else {
        m_text.SetTextColor(m_textColor[0]);

        if (m_list_frame.IsShown()) {
            Device.seqRender.Remove(this);
            Device.seqRender.Add(this, 3);
        }
    }
}

void CUIComboBox::OnFocusLost() {
    CUIWindow::OnFocusLost();
    if (m_bIsEnabled)
        m_frameLine.SetCurrentState(S_Enabled);
}

void CUIComboBox::OnFocusReceive() {
    CUIWindow::OnFocusReceive();
    if (m_bIsEnabled)
        m_frameLine.SetCurrentState(S_Highlighted);
}

bool CUIComboBox::OnMouseAction(float x, float y, EUIMessages mouse_action) {
    if (CUIWindow::OnMouseAction(x, y, mouse_action))
        return true;

    bool bCursorOverScb = false;
    bCursorOverScb = m_list_box.ScrollBar()->CursorOverWindow();
    switch (m_eState) {
    case LIST_EXPANDED:

        if ((!bCursorOverScb) && mouse_action == WINDOW_LBUTTON_DOWN) {
            ShowList(false);
            return true;
        }
        break;
    case LIST_FONDED:
        if (mouse_action == WINDOW_LBUTTON_DOWN) {
            OnBtnClicked();
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

void CUIComboBox::SendMessage(CUIWindow* pWnd, s16 msg, void* pData) {
    CUIWindow::SendMessage(pWnd, msg, pData);

    switch (msg) {
    case LIST_ITEM_CLICKED:
        if (pWnd == &m_list_box)
            OnListItemSelect();
        break;
    default:
        break;
    }
}

void CUIComboBox::OnRender() {
    if (IsShown()) {
        if (m_list_frame.IsShown()) {
            m_list_frame.Draw();
            Device.seqRender.Remove(this);
        }
    }
}

void CUIComboBox::Draw() { CUIWindow::Draw(); }

void CUIComboBox::ClearList() {
    m_list_box.Clear();
    m_text.SetText("");
    m_itoken_id = 0;
    ShowList(false);
    m_disabled.clear();
}
