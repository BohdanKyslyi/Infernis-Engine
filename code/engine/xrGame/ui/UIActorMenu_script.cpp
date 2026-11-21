////////////////////////////////////////////////////////////////////////////
//	Module 		: UIActorMenu_script.cpp
//	Created 	: 18.04.2008
//	Author		: Evgeniy Sokolov
//	Description : UI ActorMenu script implementation
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "UIActorMenu.h"

#include "../actor.h"
#include "../inventory_item.h"
#include "UICellItem.h"
#include "UIMessageBoxEx.h"
#include "../ai_space.h"
#include "../script_game_object.h"
#include "../../xrServerEntities/script_engine.h"

using namespace luabind;

void CUIActorMenu::TryRepairItem(CUIWindow* w, void* d) {
    PIItem item = get_upgrade_item();
    if (!item) {
        return;
    }
    if (item->GetCondition() > 0.99f) {
        return;
    }
    LPCSTR item_name = item->m_section_id.c_str();
    LPCSTR partner = m_pPartnerInvOwner->CharacterInfo().Profile().c_str();

    luabind::functor<bool> funct;
    R_ASSERT2(ai().script_engine().functor("inventory_upgrades.can_repair_item", funct),
              make_string("Failed to get functor <inventory_upgrades.can_repair_item>, item = %s",
                          item_name));
    bool can_repair = funct(item_name, item->GetCondition(), partner);

    luabind::functor<LPCSTR> funct2;
    R_ASSERT2(
        ai().script_engine().functor("inventory_upgrades.question_repair_item", funct2),
        make_string("Failed to get functor <inventory_upgrades.question_repair_item>, item = %s",
                    item_name));
    LPCSTR question = funct2(item_name, item->GetCondition(), can_repair, partner);

    if (can_repair) {
        m_repair_mode = true;
        CallMessageBoxYesNo(question);
    } else
        CallMessageBoxOK(question);
}

void CUIActorMenu::RepairEffect_CurItem() {
    PIItem item = CurrentIItem();
    if (!item) {
        return;
    }
    LPCSTR item_name = item->m_section_id.c_str();

    luabind::functor<void> funct;
    R_ASSERT(ai().script_engine().functor("inventory_upgrades.effect_repair_item", funct));
    funct(item_name, item->GetCondition());

    item->SetCondition(1.0f);
    UpdateConditionProgressBars();
    SeparateUpgradeItem();
    CUICellItem* itm = CurrentItem();
    if (itm)
        itm->UpdateConditionProgressBar();
}

bool CUIActorMenu::CanUpgradeItem(PIItem item) {
    VERIFY(item && m_pPartnerInvOwner);
    LPCSTR item_name = item->m_section_id.c_str();
    LPCSTR partner = m_pPartnerInvOwner->CharacterInfo().Profile().c_str();

    luabind::functor<bool> funct;
    R_ASSERT2(
        ai().script_engine().functor("inventory_upgrades.can_upgrade_item", funct),
        make_string(
            "Failed to get functor <inventory_upgrades.can_upgrade_item>, item = %s, mechanic = %s",
            item_name, partner));

    luabind::functor<void> set_mechanic;
    if (ai().script_engine().functor("inventory_upgrades.need_victim", set_mechanic)) {
        if (auto* mechanic = smart_cast<CGameObject*>(m_pPartnerInvOwner))
            set_mechanic(mechanic->lua_game_object());
    }

    return funct(item_name, partner);
}

bool CUIActorMenu::CanDisassembleItem(PIItem item) {
    if (!item || !m_pActorInvOwner || !m_pPartnerInvOwner || item->object().getDestroy() ||
        item->parent_id() != m_pActorInvOwner->object_id())
        return false;

    luabind::functor<bool> can;
    if (!ai().script_engine().functor("inventory_upgrades.gunsl_can_disassemble_item", can))
        return false;
    return can(item->m_section_id.c_str(), item->GetCondition(),
               m_pPartnerInvOwner->CharacterInfo().Profile().c_str());
}

void CUIActorMenu::TryDisassembleItem(CUIWindow*, void*) {
    PIItem item = get_upgrade_item();
    if (!CanDisassembleItem(item))
        return;

    luabind::functor<LPCSTR> question;
    if (!ai().script_engine().functor("inventory_upgrades.gunsl_question_disassemble_item", question))
        return;

    const shared_str partner = m_pPartnerInvOwner->CharacterInfo().Profile();
    m_disassemble_item = item->object_id();
    m_disassemble_mechanic = m_pPartnerInvOwner->object_id();
    CallMessageBoxYesNo(question(item->m_section_id.c_str(), item->GetCondition(), true, partner.c_str()));
    m_message_box_yes_no->func_on_ok =
        CUIWndCallback::void_function(this, &CUIActorMenu::DisassembleItem);
}

void CUIActorMenu::DisassembleItem(CUIWindow*, void*) {
    const u16 item_id = m_disassemble_item;
    const u16 mechanic_id = m_disassemble_mechanic;
    m_disassemble_item = m_disassemble_mechanic = u16(-1);

    PIItem item = get_upgrade_item();
    if (m_currMenuMode != mmUpgrade || !item || item->object_id() != item_id ||
        !m_pPartnerInvOwner || m_pPartnerInvOwner->object_id() != mechanic_id ||
        !CanDisassembleItem(item))
        return;

    luabind::functor<void> effect;
    if (!ai().script_engine().functor("inventory_upgrades.gunsl_effect_disassemble", effect))
        return;

    const shared_str partner = m_pPartnerInvOwner->CharacterInfo().Profile();
    effect(item->m_section_id.c_str(), item->GetCondition(), partner.c_str());
    SetCurrentItem(nullptr);
    m_upgrade_selected = nullptr;
    item->object().DestroyObject();
    UpdateActor();
    UpdateItemsPlace();
}

void CUIActorMenu::CurModeToScript() {
    int mode = (int)m_currMenuMode;
    luabind::functor<void> funct;
    R_ASSERT(ai().script_engine().functor("actor_menu.actor_menu_mode", funct));
    funct(mode);
}
