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

bool CItemUseController::Start(CInventoryItem* item)
{
    if (!item)
        return false;

    if (m_active)
        return false;

    m_item = item;
    m_item_section = item->object().cNameSect();

    //
    // Немає HUD-анімації — controller цей предмет
    // не обробляє.
    //
    if (!pSettings->line_exist(m_item_section, "hud"))
    {
        Reset();
        return false;
    }

    //
    // [conserva]
    // hud = conserva_beef_hud_model
    //
    m_use_section =
        pSettings->r_string(m_item_section, "hud");

    if (!pSettings->section_exist(m_use_section))
    {
        Msg("! ItemUse: missing use section [%s]",
            m_use_section.c_str());

        Reset();
        return false;
    }

    //
    // [conserva_beef_hud_model]
    // hud = anm_conserva_hud
    //
    if (!pSettings->line_exist(m_use_section, "hud"))
    {
        Msg("! ItemUse: section [%s] has no HUD section",
            m_use_section.c_str());

        Reset();
        return false;
    }

    m_hud_section =
        pSettings->r_string(m_use_section, "hud");

    //
    // GWR timing is milliseconds.
    //
    m_action_time =
        pSettings->r_u32(m_use_section, "timing");

    if (!g_player_hud)
    {
        Reset();
        return false;
    }

    if (!g_player_hud->attach_controller_item(m_hud_section))
    {
        Reset();
        return false;
    }

    //
    // "anm_show" alias resolves to:
    //
    // anm_show = canned_beef_animation
    //
    m_animation_duration =
        g_player_hud->play_controller_motion(
            "anm_show",
            TRUE
        );

    m_start_time = Device.dwTimeGlobal;
    m_active = true;
    m_effect_applied = false;

    Msg("* ItemUse started: [%s], HUD [%s], duration [%u], effect [%u]",
        m_item_section.c_str(),
        m_hud_section.c_str(),
        m_animation_duration,
        m_action_time);

    return true;
}

void CItemUseController::Update(float dt)
{
    if (!m_active)
        return;

    const u32 elapsed =
        Device.dwTimeGlobal - m_start_time;

    if (!m_effect_applied &&
        elapsed >= m_action_time)
    {
        m_effect_applied = true;

        Msg("* ItemUse effect moment: [%s]",
            m_item_section.c_str());

        //
        // Наступний етап:
        // тут викличемо CEatableItem::UseBy()
        //
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