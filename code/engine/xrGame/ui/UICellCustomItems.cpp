#include "stdafx.h"
#include "UICellCustomItems.h"
#include "UIInventoryUtilities.h"
#include "../Weapon.h"
#include "UIDragDropListEx.h"
#include "UIProgressBar.h"

#include <cstring>

#define INV_GRID_WIDTHF 50.0f
#define INV_GRID_HEIGHTF 50.0f

static void SetItemIconShader(CUIStaticItem& item, LPCSTR section) {
    if (pSettings->line_exist(section, "icons_texture")) {
        ui_shader shader;
        shader->create("hud\\default", pSettings->r_string(section, "icons_texture"));
        item.SetShader(shader);
        return;
    }

    item.SetShader(InventoryUtilities::GetEquipmentIconsShader());
}

static void SetItemIconShader(CUIStatic* item, LPCSTR section) {
    if (pSettings->line_exist(section, "icons_texture")) {
        item->CreateShader(pSettings->r_string(section, "icons_texture"), "hud\\default");
        return;
    }

    item->SetShader(InventoryUtilities::GetEquipmentIconsShader());
}

namespace detail {

struct is_helper_pred {
    bool operator()(CUICellItem* child) { return child->IsHelper(); }

}; // struct is_helper_pred

} // namespace detail

CUIInventoryCellItem::CUIInventoryCellItem(CInventoryItem* itm)
    : m_last_condition(-1.f), m_last_portions(-1) {
    m_pData = (void*)itm;

    SetItemIconShader(GetUIStaticItem(), itm->object().cNameSect().c_str());

    m_grid_size.set(itm->GetInvGridRect().rb);
    Frect rect;
    rect.lt.set(INV_GRID_WIDTHF * itm->GetInvGridRect().x1,
                INV_GRID_HEIGHTF * itm->GetInvGridRect().y1);

    rect.rb.set(rect.lt.x + INV_GRID_WIDTHF * m_grid_size.x,
                rect.lt.y + INV_GRID_HEIGHTF * m_grid_size.y);

    inherited::SetTextureRect(rect);
    inherited::SetStretchTexture(true);
}

bool CUIInventoryCellItem::EqualTo(CUICellItem* itm) {
    CUIInventoryCellItem* ci = smart_cast<CUIInventoryCellItem*>(itm);

    if (!ci)
        return false;

    //
    // Different sections are never equal.
    //
    if (object()->object().cNameSect() != ci->object()->object().cNameSect()) {
        return false;
    }

    //
    // Portion-aware stacking.
    //
    // Items with different remaining portions
    // must be displayed separately.
    //
    CEatableItem* eatable_a = smart_cast<CEatableItem*>(object());

    CEatableItem* eatable_b = smart_cast<CEatableItem*>(ci->object());

    if (eatable_a || eatable_b) {
        //
        // Safety: one eatable and one non-eatable
        // should never become one stack.
        //
        if (!eatable_a || !eatable_b)
            return false;

        if (eatable_a->PortionsNum() != eatable_b->PortionsNum()) {
            return false;
        }
    }

    //
    // Existing vanilla checks.
    //
    if (!fsimilar(object()->GetCondition(), ci->object()->GetCondition(), 0.01f)) {
        return false;
    }

    if (!object()->equal_upgrades(ci->object()->upgardes())) {
        return false;
    }

    return true;
}

bool CUIInventoryCellItem::IsHelperOrHasHelperChild() {
    return std::count_if(m_childs.begin(), m_childs.end(), detail::is_helper_pred()) > 0 ||
           IsHelper();
}

CUIDragItem* CUIInventoryCellItem::CreateDragItem() {
    return IsHelperOrHasHelperChild() ? NULL : inherited::CreateDragItem();
}

bool CUIInventoryCellItem::IsHelper() { return object()->is_helper_item(); }

void CUIInventoryCellItem::SetIsHelper(bool is_helper) { object()->set_is_helper(is_helper); }

void CUIInventoryCellItem::Update() {
    inherited::Update();

    // UseBy() changes the item after its cell has already been placed in a list.
    // Refresh the existing cell when the condition or remaining portions change.
    CEatableItem* eatable = smart_cast<CEatableItem*>(object());
    const float condition = object()->GetCondition();
    const int portions = eatable ? eatable->PortionsNum() : -1;
    if (m_last_condition != condition || m_last_portions != portions) {
        m_last_condition = condition;
        m_last_portions = portions;
        UpdateConditionProgressBar();
    }

    UpdateItemText();

    u32 color = GetTextureColor();
    if (IsHelper() && !ChildsCount()) {
        color = 0xbbbbbbbb;
    } else if (IsHelperOrHasHelperChild()) {
        color = 0xffffffff;
    }

    SetTextureColor(color);
}

void CUIInventoryCellItem::UpdateItemText() {
    const u32 helper_count =
        (u32)std::count_if(m_childs.begin(), m_childs.end(), detail::is_helper_pred()) + IsHelper()
            ? 1
            : 0;

    const u32 count = ChildsCount() + 1 - helper_count;

    string32 str;

    if (count > 1 || helper_count) {
        xr_sprintf(str, "x%d", count);
        m_text->TextItemControl()->SetText(str);
        m_text->Show(true);
    } else {
        xr_sprintf(str, "");
        m_text->TextItemControl()->SetText(str);
        m_text->Show(false);
    }
}

CUIAmmoCellItem::CUIAmmoCellItem(CWeaponAmmo* itm) : inherited(itm) {}

bool CUIAmmoCellItem::EqualTo(CUICellItem* itm) {
    if (!inherited::EqualTo(itm))
        return false;

    CUIAmmoCellItem* ci = smart_cast<CUIAmmoCellItem*>(itm);
    if (!ci)
        return false;

    return ((object()->cNameSect() == ci->object()->cNameSect()));
}

CUIDragItem* CUIAmmoCellItem::CreateDragItem() {
    return IsHelper() ? NULL : inherited::CreateDragItem();
}

u32 CUIAmmoCellItem::CalculateAmmoCount() {
    xr_vector<CUICellItem*>::iterator it = m_childs.begin();
    xr_vector<CUICellItem*>::iterator it_e = m_childs.end();

    u32 total = IsHelper() ? 0 : object()->m_boxCurr;
    for (; it != it_e; ++it) {
        CUICellItem* child = *it;

        if (!child->IsHelper()) {
            total += ((CUIAmmoCellItem*)(*it))->object()->m_boxCurr;
        }
    }

    return total;
}

void CUIAmmoCellItem::UpdateItemText() {
    m_text->Show(false);
    if (!m_custom_draw) {
        const u32 total = CalculateAmmoCount();

        string32 str;
        xr_sprintf(str, "%d", total);
        m_text->TextItemControl()->SetText(str);
        m_text->Show(true);
    }
}

CUIWeaponCellItem::CUIWeaponCellItem(CWeapon* itm) : inherited(itm) {
    m_addons[eSilencer] = NULL;
    m_addons[eScope] = NULL;
    m_addons[eLauncher] = NULL;

    RefreshOffset();
}

#include "../xrServerEntities/object_broker.h"
CUIWeaponCellItem::~CUIWeaponCellItem() {}

bool CUIWeaponCellItem::is_scope() {
    return object()->ScopeAttachable() && object()->IsScopeAttached();
}

bool CUIWeaponCellItem::is_silencer() {
    return object()->SilencerAttachable() && object()->IsSilencerAttached();
}

bool CUIWeaponCellItem::is_launcher() {
    return object()->GrenadeLauncherAttachable() && object()->IsGrenadeLauncherAttached();
}

void CUIWeaponCellItem::CreateIcon(eAddonType t) {
    if (m_addons[t])
        return;
    m_addons[t] = xr_new<CUIStatic>();
    m_addons[t]->SetAutoDelete(true);
    AttachChild(m_addons[t]);
    m_addons[t]->SetShader(InventoryUtilities::GetEquipmentIconsShader());

    u32 color = GetTextureColor();
    m_addons[t]->SetTextureColor(color);
}

void CUIWeaponCellItem::DestroyIcon(eAddonType t) {
    DetachChild(m_addons[t]);
    m_addons[t] = NULL;
}

CUIStatic* CUIWeaponCellItem::GetIcon(eAddonType t) { return m_addons[t]; }
void CUIWeaponCellItem::RefreshOffset() {
    if (object()->SilencerAttachable())
        m_addon_offset[eSilencer].set(object()->GetSilencerX(), object()->GetSilencerY());

    if (object()->ScopeAttachable())
        m_addon_offset[eScope].set(object()->GetScopeX(), object()->GetScopeY());

    if (object()->GrenadeLauncherAttachable())
        m_addon_offset[eLauncher].set(object()->GetGrenadeLauncherX(),
                                      object()->GetGrenadeLauncherY());

    const Fvector2 correction = UpgradeIconCorrection();
    if (object()->SilencerAttachable())
        m_addon_offset[eSilencer].add(correction);
    if (object()->ScopeAttachable())
        m_addon_offset[eScope].add(correction);
    if (object()->GrenadeLauncherAttachable())
        m_addon_offset[eLauncher].add(correction);
}

Fvector2 CUIWeaponCellItem::UpgradeIconCorrection() {
    Fvector2 correction;
    correction.set(0.f, 0.f);
    for (const shared_str& effect : object()->UpgradeIconSections()) {
        correction.x += READ_IF_EXISTS(pSettings, r_float, effect, "inv_addons_correction_x", 0.f);
        correction.y += READ_IF_EXISTS(pSettings, r_float, effect, "inv_addons_correction_y", 0.f);
    }
    return correction;
}

void CUIWeaponCellItem::RefreshWeaponIcon() {
    const Irect grid = object()->GetInvGridRect();
    m_grid_size.set(grid.rb);
    Frect rect;
    rect.set(grid.x1 * INV_GRID_WIDTHF, grid.y1 * INV_GRID_HEIGHTF,
             (grid.x1 + grid.x2) * INV_GRID_WIDTHF, (grid.y1 + grid.y2) * INV_GRID_HEIGHTF);
    SetTextureRect(rect);
    m_icon_weapon = nullptr;
}

namespace {
struct WeaponIconLayer {
    LPCSTR section;
    Fvector2 offset;
    bool front;
    u32 banned_addons;
};

bool ValidIconSection(LPCSTR section) {
    return section && section[0] && pSettings->section_exist(section) &&
        READ_IF_EXISTS(pSettings, r_u32, section, "inv_grid_width", 0) > 0 &&
        READ_IF_EXISTS(pSettings, r_u32, section, "inv_grid_height", 0) > 0 &&
        pSettings->line_exist(section, "inv_grid_x") &&
        pSettings->line_exist(section, "inv_grid_y");
}

void AddHiddenIcons(xr_vector<shared_str>& hidden, LPCSTR names) {
    if (!names || !names[0])
        return;

    string128 name;
    for (int i = 0, count = _GetItemCount(names); i < count; ++i) {
        _GetItem(names, i, name);
        hidden.emplace_back(name);
    }
}

xr_vector<LPCSTR> ElementIconIndices(LPCSTR section) {
    constexpr char prefix[] = "element_icon_";
    xr_vector<LPCSTR> indices;
    for (const auto& line : pSettings->r_section(section).Data) {
        const LPCSTR key = line.first.c_str();
        if (!key || std::strncmp(key, prefix, sizeof(prefix) - 1) != 0)
            continue;

        const LPCSTR index = key + sizeof(prefix) - 1;
        if (index[0] && !(index[0] == '0' && index[1]) &&
            index[std::strspn(index, "0123456789")] == 0)
            indices.push_back(index);
    }
    // Numeric order without converting the suffix to a fixed-width integer.
    std::sort(indices.begin(), indices.end(), [](LPCSTR a, LPCSTR b) {
        const auto a_size = std::strlen(a);
        const auto b_size = std::strlen(b);
        return a_size == b_size ? std::strcmp(a, b) < 0 : a_size < b_size;
    });
    return indices;
}
} // namespace

void CUIWeaponCellItem::CreateLayeredIcons(CUIStatic* parent, bool rotate, u32 color) {
    const shared_str weapon = object()->cNameSect();
    const LPCSTR weapon_section = weapon.c_str();
    const Fvector2 correction = UpgradeIconCorrection();
    xr_vector<shared_str> hidden;
    xr_vector<WeaponIconLayer> elements;
    xr_vector<WeaponIconLayer> upgrades;

    for (LPCSTR index : ElementIconIndices(weapon_section)) {
        const auto key = [index](LPCSTR prefix) {
            xr_string name(prefix);
            name += index;
            return name;
        };
        const LPCSTR section = pSettings->r_string(weapon_section, key("element_icon_").c_str());
        if (!ValidIconSection(section))
            continue;

        Fvector2 offset = correction;
        offset.x += READ_IF_EXISTS(pSettings, r_float, weapon_section,
            key("element_icon_offset_x_").c_str(), 0.f);
        offset.y += READ_IF_EXISTS(pSettings, r_float, weapon_section,
            key("element_icon_offset_y_").c_str(), 0.f);
        elements.push_back({section, offset,
            0 != READ_IF_EXISTS(pSettings, r_bool, weapon_section, key("element_icon_always_front_").c_str(), false),
            READ_IF_EXISTS(pSettings, r_u32, weapon_section, key("element_icon_banned_addons_mask_").c_str(), 0)});
        AddHiddenIcons(hidden, READ_IF_EXISTS(pSettings, r_string, weapon_section,
            key("element_icon_hide_").c_str(), nullptr));
    }

    for (const shared_str& effect : object()->UpgradeIconSections()) {
        AddHiddenIcons(hidden, READ_IF_EXISTS(pSettings, r_string, effect, "upgrade_addon_icons_hide", nullptr));
        const LPCSTR section = READ_IF_EXISTS(pSettings, r_string, effect, "upgrade_addon_icon", nullptr);
        if (!ValidIconSection(section))
            continue;

        Fvector2 offset = correction;
        offset.x += READ_IF_EXISTS(pSettings, r_float, effect, "upgrade_addon_icon_offset_x", 0.f);
        offset.y += READ_IF_EXISTS(pSettings, r_float, effect, "upgrade_addon_icon_offset_y", 0.f);
        upgrades.push_back({section, offset,
            0 != READ_IF_EXISTS(pSettings, r_bool, effect, "upgrade_addon_always_front", false),
            READ_IF_EXISTS(pSettings, r_u32, effect, "upgrade_addon_icon_banned_addons_mask", 0)});
    }

    if (elements.empty())
        parent->TextureOn();
    else
        parent->TextureOff();

    const u8 addons = object()->GetAddonsState();
    const auto draw_layers = [&](const xr_vector<WeaponIconLayer>& layers, bool front, bool base) {
        CUIStatic* first = nullptr;
        for (const auto& layer : layers) {
            if (layer.front != front || (addons & layer.banned_addons) ||
                std::find(hidden.begin(), hidden.end(), shared_str(layer.section)) != hidden.end())
                continue;

            auto* icon = xr_new<CUIStatic>();
            icon->SetAutoDelete(true);
            const LPCSTR shader_section = base && !pSettings->line_exist(layer.section, "icons_texture")
                ? weapon_section : layer.section;
            InitAddon(icon, layer.section, layer.offset, rotate, shader_section);
            icon->SetTextureColor(color);
            parent->AttachChild(icon);
            if (!first)
                first = icon;
            if (parent == this)
                m_layered_icons.push_back(icon);
        }
        return first;
    };

    auto& children = parent->GetChildWndList();
    if (auto* first = draw_layers(elements, false, true)) {
        // Base elements belong behind already attached scope, silencer and launcher icons.
        std::rotate(children.begin(), std::find(children.begin(), children.end(), first), children.end());
    }
    draw_layers(upgrades, false, false);
    draw_layers(elements, true, true);
    draw_layers(upgrades, true, false);

    if (parent == this && !elements.empty()) {
        // Keep cell text, condition and upgrade markers above the assembled icon.
        for (CUIWindow* overlay : {static_cast<CUIWindow*>(m_text),
                                  static_cast<CUIWindow*>(m_pConditionState),
                                  static_cast<CUIWindow*>(m_upgrade)}) {
            const auto it = std::find(children.begin(), children.end(), overlay);
            if (it != children.end()) {
                auto next = it;
                std::rotate(it, ++next, children.end());
            }
        }
    }
}

void CUIWeaponCellItem::Draw() {
    inherited::Draw();

    if (m_upgrade && m_upgrade->IsShown())
        m_upgrade->Draw();
};

void CUIWeaponCellItem::Update() {
    bool b = Heading();
    inherited::Update();

    const size_t upgrade_count = object()->UpgradeIconSections().size();
    const u8 addons = object()->GetAddonsState();
    const Fvector2 size = GetWndSize();
    const bool bForceReInitAddons = b != Heading() || m_icon_weapon != object() ||
        m_icon_upgrade_count != upgrade_count || m_icon_addons != addons ||
        m_icon_size.x != size.x || m_icon_size.y != size.y;

    if (object()->SilencerAttachable()) {
        if (object()->IsSilencerAttached()) {
            if (!GetIcon(eSilencer) || bForceReInitAddons) {
                CreateIcon(eSilencer);
                RefreshOffset();
                InitAddon(GetIcon(eSilencer), *object()->GetSilencerName(),
                          m_addon_offset[eSilencer], Heading());
            }
        } else {
            if (m_addons[eSilencer])
                DestroyIcon(eSilencer);
        }
    }

    if (object()->ScopeAttachable()) {
        if (object()->IsScopeAttached()) {
            if (!GetIcon(eScope) || bForceReInitAddons) {
                CreateIcon(eScope);
                RefreshOffset();
                InitAddon(GetIcon(eScope), *object()->GetScopeName(), m_addon_offset[eScope],
                          Heading());
            }
        } else {
            if (m_addons[eScope])
                DestroyIcon(eScope);
        }
    }

    if (object()->GrenadeLauncherAttachable()) {
        if (object()->IsGrenadeLauncherAttached()) {
            if (!GetIcon(eLauncher) || bForceReInitAddons) {
                CreateIcon(eLauncher);
                RefreshOffset();
                InitAddon(GetIcon(eLauncher), *object()->GetGrenadeLauncherName(),
                          m_addon_offset[eLauncher], Heading());
            }
        } else {
            if (m_addons[eLauncher])
                DestroyIcon(eLauncher);
        }
    }

    if (bForceReInitAddons) {
        for (CUIStatic* icon : m_layered_icons)
            DetachChild(icon);
        m_layered_icons.clear();
        CreateLayeredIcons(this, Heading(), GetTextureColor());
        m_icon_weapon = object();
        m_icon_upgrade_count = upgrade_count;
        m_icon_addons = addons;
        m_icon_size = size;
    }
}

void CUIWeaponCellItem::SetTextureColor(u32 color) {
    inherited::SetTextureColor(color);
    for (CUIStatic* icon : m_layered_icons)
        icon->SetTextureColor(color);
    if (m_addons[eSilencer]) {
        m_addons[eSilencer]->SetTextureColor(color);
    }
    if (m_addons[eScope]) {
        m_addons[eScope]->SetTextureColor(color);
    }
    if (m_addons[eLauncher]) {
        m_addons[eLauncher]->SetTextureColor(color);
    }
}

void CUIWeaponCellItem::OnAfterChild(CUIDragDropListEx* parent_list) {
    if (is_silencer() && GetIcon(eSilencer))
        InitAddon(GetIcon(eSilencer), *object()->GetSilencerName(), m_addon_offset[eSilencer],
                  parent_list->GetVerticalPlacement());

    if (is_scope() && GetIcon(eScope))
        InitAddon(GetIcon(eScope), *object()->GetScopeName(), m_addon_offset[eScope],
                  parent_list->GetVerticalPlacement());

    if (is_launcher() && GetIcon(eLauncher))
        InitAddon(GetIcon(eLauncher), *object()->GetGrenadeLauncherName(),
                  m_addon_offset[eLauncher], parent_list->GetVerticalPlacement());
}

void CUIWeaponCellItem::InitAddon(CUIStatic* s, LPCSTR section, Fvector2 addon_offset,
                                  bool b_rotate, LPCSTR shader_section) {

    Frect tex_rect;
    Fvector2 base_scale;

    if (Heading()) {
        base_scale.x = GetHeight() / (INV_GRID_WIDTHF * m_grid_size.x);
        base_scale.y = GetWidth() / (INV_GRID_HEIGHTF * m_grid_size.y);
    } else {
        base_scale.x = GetWidth() / (INV_GRID_WIDTHF * m_grid_size.x);
        base_scale.y = GetHeight() / (INV_GRID_HEIGHTF * m_grid_size.y);
    }
    Fvector2 cell_size;
    cell_size.x = pSettings->r_u32(section, "inv_grid_width") * INV_GRID_WIDTHF;
    cell_size.y = pSettings->r_u32(section, "inv_grid_height") * INV_GRID_HEIGHTF;

    tex_rect.x1 = pSettings->r_u32(section, "inv_grid_x") * INV_GRID_WIDTHF;
    tex_rect.y1 = pSettings->r_u32(section, "inv_grid_y") * INV_GRID_HEIGHTF;

    tex_rect.rb.add(tex_rect.lt, cell_size);

    cell_size.mul(base_scale);

    if (b_rotate) {
        s->SetWndSize(Fvector2().set(cell_size.y, cell_size.x));
        Fvector2 new_offset;
        new_offset.x = addon_offset.y * base_scale.x;
        new_offset.y = GetHeight() - addon_offset.x * base_scale.x - cell_size.x;
        addon_offset = new_offset;
        addon_offset.x *= UI().get_current_kx();
    } else {
        s->SetWndSize(cell_size);
        addon_offset.mul(base_scale);
    }

    s->SetWndPos(addon_offset);
    SetItemIconShader(s, shader_section ? shader_section : section);
    s->SetTextureRect(tex_rect);
    s->TextureOn();
    s->SetStretchTexture(true);

    s->EnableHeading(b_rotate);

    if (b_rotate) {
        s->SetHeading(GetHeading());
        Fvector2 offs;
        offs.set(0.0f, s->GetWndSize().y);
        s->SetHeadingPivot(Fvector2().set(0.0f, 0.0f), /*Fvector2().set(0.0f,0.0f)*/ offs, true);
    }
}

CUIDragItem* CUIWeaponCellItem::CreateDragItem() {
    CUIDragItem* i = inherited::CreateDragItem();
    if (!i)
        return nullptr;
    CUIStatic* s = NULL;

    if (GetIcon(eSilencer)) {
        s = xr_new<CUIStatic>();
        s->SetAutoDelete(true);
        s->SetShader(InventoryUtilities::GetEquipmentIconsShader());
        InitAddon(s, *object()->GetSilencerName(), m_addon_offset[eSilencer], false);
        s->SetTextureColor(i->wnd()->GetTextureColor());
        i->wnd()->AttachChild(s);
    }

    if (GetIcon(eScope)) {
        s = xr_new<CUIStatic>();
        s->SetAutoDelete(true);
        s->SetShader(InventoryUtilities::GetEquipmentIconsShader());
        InitAddon(s, *object()->GetScopeName(), m_addon_offset[eScope], false);
        s->SetTextureColor(i->wnd()->GetTextureColor());
        i->wnd()->AttachChild(s);
    }

    if (GetIcon(eLauncher)) {
        s = xr_new<CUIStatic>();
        s->SetAutoDelete(true);
        s->SetShader(InventoryUtilities::GetEquipmentIconsShader());
        InitAddon(s, *object()->GetGrenadeLauncherName(), m_addon_offset[eLauncher], false);
        s->SetTextureColor(i->wnd()->GetTextureColor());
        i->wnd()->AttachChild(s);
    }
    CreateLayeredIcons(i->wnd(), false, i->wnd()->GetTextureColor());
    return i;
}

bool CUIWeaponCellItem::EqualTo(CUICellItem* itm) {
    if (!inherited::EqualTo(itm))
        return false;

    CUIWeaponCellItem* ci = smart_cast<CUIWeaponCellItem*>(itm);
    if (!ci)
        return false;

    //	bool b_addons					= ( (object()->GetAddonsState() == ci->object()->GetAddonsState())
    //);
    if (object()->GetAddonsState() != ci->object()->GetAddonsState()) {
        return false;
    }
    if (this->is_scope() && ci->is_scope()) {
        if (object()->GetScopeName() != ci->object()->GetScopeName()) {
            return false;
        }
    }
    //	bool b_place					= ( (object()->m_eItemCurrPlace == ci->object()->m_eItemCurrPlace)
    //);

    return true;
}

CBuyItemCustomDrawCell::CBuyItemCustomDrawCell(LPCSTR str, CGameFont* pFont) {
    m_pFont = pFont;
    VERIFY(xr_strlen(str) < 16);
    xr_strcpy(m_string, str);
}

void CBuyItemCustomDrawCell::OnDraw(CUICellItem* cell) {
    Fvector2 pos;
    cell->GetAbsolutePos(pos);
    UI().ClientToScreenScaled(pos, pos.x, pos.y);
    m_pFont->Out(pos.x, pos.y, m_string);
    m_pFont->OnRender();
}
