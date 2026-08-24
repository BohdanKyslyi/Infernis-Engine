#include "stdafx.h"
#include "ItemUseController.h"

#include "Actor.h"
#include "HudItem.h"
#include "player_hud.h"
#include "inventory.h"

CItemUseController::CItemUseController(CActor* actor)
    : m_actor(actor),
      m_item(NULL),
      m_start_time(0),
      m_action_time(0),
      m_animation_duration(0),
      m_active(false),
      m_effect_applied(false)
{
}

CItemUseController::~CItemUseController()
{
    Cancel();
}

bool CItemUseController::Start(CInventoryItem* item) {
    if (!item)
        return false;

    if (m_active)
        return false;

    m_item = item;
    m_item_section = item->object().cNameSect();

    //
    // 1. Сам предмет повинен посилатися на use-section.
    //
    if (!pSettings->line_exist(m_item_section, "hud")) {
        Reset();
        return false;
    }

    m_use_section = pSettings->r_string(m_item_section, "hud");

    if (!pSettings->section_exist(m_use_section)) {
        Reset();
        return false;
    }

    //
    // 2. Це повинна бути саме секція animated consumable.
    // Простого параметра "hud" недостатньо.
    //
    if (!pSettings->line_exist(m_use_section, "timing") ||
        !pSettings->line_exist(m_use_section, "hud")) {
        Reset();
        return false;
    }

    m_hud_section = pSettings->r_string(m_use_section, "hud");

    if (!pSettings->section_exist(m_hud_section)) {
        Reset();
        return false;
    }

    //
    // 3. HUD-секція повинна мати нашу стартову анімацію.
    //
    if (!pSettings->line_exist(m_hud_section, "anm_show")) {
        Reset();
        return false;
    }

    m_action_time = pSettings->r_u32(m_use_section, "timing");

    if (!g_player_hud) {
        Reset();
        return false;
    }

    if (!g_player_hud->attach_controller_item(m_hud_section)) {
        Reset();
        return false;
    }

    m_animation_duration = g_player_hud->play_controller_motion("anm_show", TRUE);

    if (m_animation_duration == 0) {
        g_player_hud->detach_controller_item();
        Reset();
        return false;
    }

    m_start_time = Device.dwTimeGlobal;
    m_active = true;
    m_effect_applied = false;

    Msg("* ItemUse started: [%s], HUD [%s], duration [%u], effect [%u]", m_item_section.c_str(),
        m_hud_section.c_str(), m_animation_duration, m_action_time);

    return true;
}

void CItemUseController::Update(float dt)
{
    if (!m_active)
        return;

    const u32 elapsed =
        Device.dwTimeGlobal - m_start_time;

if (!m_effect_applied && elapsed >= m_action_time) {
        if (!m_item) {
            Msg("! ItemUse: source item is NULL");
            Cancel();
            return;
        }

        if (!m_actor) {
            Msg("! ItemUse: actor is NULL");
            Cancel();
            return;
        }

        bool became_empty = false;

        if (!m_actor->inventory().ApplyEat(m_item, became_empty)) {
            Msg("! ItemUse: failed to apply effect for [%s]", m_item_section.c_str());

            Cancel();
            return;
        }

        m_effect_applied = true;

        Msg("* ItemUse effect applied: [%s]", m_item_section.c_str());

        //
        // Предмет уже позначений SetDropManual(TRUE).
        // Більше Controller'у pointer не потрібен.
        //
        if (became_empty)
            m_item = NULL;
    }

    //
    // Для першого тесту закінчуємо по реальній
    // довжині HUD animation.
    //
    if (m_animation_duration > 0 &&
        elapsed >= m_animation_duration)
    {
        Finish();
    }
}

void CItemUseController::Cancel()
{
    if (!m_active)
        return;

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    Msg("* ItemUse cancelled: [%s]",
        m_item_section.c_str());

    Reset();
}

void CItemUseController::Finish()
{
    if (!m_active)
        return;

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    Msg("* ItemUse finished: [%s]",
        m_item_section.c_str());

    Reset();
}

void CItemUseController::Reset()
{
    m_item = NULL;

    m_item_section = NULL;
    m_use_section = NULL;
    m_hud_section = NULL;

    m_start_time = 0;
    m_action_time = 0;
    m_animation_duration = 0;

    m_active = false;
    m_effect_applied = false;
}