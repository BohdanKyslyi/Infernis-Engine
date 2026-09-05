////////////////////////////////////////////////////////////////////////////
//	Module 		: ItemUseController.cpp
//	Created 	: 23.08.2026
//  Modified 	: 25.08.2026
//	Author		: Bohdan «Infernis» Kyslyi
//	Description : Item use controller
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ItemUseController.h"

#include "Actor.h"
#include "HudItem.h"
#include "player_hud.h"
#include "inventory.h"

#include "CustomDetector.h"
#include "UIGameCustom.h"

#include "../xrPhysics/ElevatorState.h"
#include "eatable_item.h"

static bool ConsumableAnimationsEnabled() {
    static bool initialized = false;
    static bool enabled = true;

    if (!initialized) {
        initialized = true;

        if (pSettings->section_exist("items_animations") &&
            pSettings->line_exist("items_animations", "enable_consumables_animations")) {
            enabled = !!pSettings->r_bool("items_animations", "enable_consumables_animations");
        }

        Msg("* Consumable animations: [%s]", enabled ? "enabled" : "disabled");
    }

    return enabled;
}

CItemUseController::CItemUseController(CActor* actor)
    : m_actor(actor),
      m_item(NULL),
      m_start_time(0),
      m_action_time(0),
      m_animation_duration(0),
      m_active(false),
      m_effect_applied(false),
      m_waiting_for_weapon_hide(false),
      m_actor_locked(false),
      m_prev_inventory_disabled(false),

      m_trash_count(0), m_trash_spawned(false),

      m_anim_sound_loaded(false) {
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

    //
    // Global engine_external switch.
    //
    // Returning false tells CInventory::Eat()
    // to use the normal immediate ApplyEat() path.
    //
    if (!ConsumableAnimationsEnabled())
        return false;

    m_item = item;
    m_item_section = item->object().cNameSect();

    m_trash_section = NULL;
    m_trash_count = 0;
    m_trash_spawned = false;

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
    // 2. Перевіряємо, що це саме animated consumable.
    //
    if (!pSettings->line_exist(m_use_section, "timing")) {
        Reset();
        return false;
    }

    m_hud_section = NULL;

    //
    // First try portion-specific HUD.
    //
    CEatableItem* eatable =
        smart_cast<CEatableItem*>(item);

    //
    // Cache physical waste for THIS exact portion/state.
    //
    if (eatable && eatable->HasTrash()) {
        m_trash_section = eatable->TrashObject();

        m_trash_count = eatable->TrashCount();
    }

    if (eatable)
    {
        const shared_str& state =
            eatable->PortionStateSection();

        if (state.size() &&
            pSettings->line_exist(
                state.c_str(),
                "hud"))
        {
            m_hud_section =
                pSettings->r_string(
                    state.c_str(),
                    "hud"
                );
        }
    }

    //
    // Backward-compatible fallback:
    //
    // old animated consumables can still use
    // [use_section] hud = ...
    //
    if (!m_hud_section.size() &&
        pSettings->line_exist(
            m_use_section,
            "hud"))
    {
        m_hud_section =
            pSettings->r_string(
                m_use_section,
                "hud"
            );
    }

    if (!m_hud_section.size())
    {
        Reset();
        return false;
    }

    if (!pSettings->section_exist(m_hud_section)) {
        Reset();
        return false;
    }

    //
    // 3. HUD-секція повинна мати анімацію використання.
    //
    if (!pSettings->line_exist(m_hud_section, "anm_show")) {
        Reset();
        return false;
    }

    if (eatable) {
        Msg("* ItemUse portion: [%d/%d], use index [%d], state [%s]", eatable->PortionsNum(),
            eatable->TotalPortions(), eatable->PortionIndex(),
            eatable->PortionStateSection().size() ? eatable->PortionStateSection().c_str()
                                                  : "default");
    }

    m_action_time = pSettings->r_u32(m_use_section, "timing");

    if (!g_player_hud) {
        Reset();
        return false;
    }

    //
    // ВАЖЛИВО:
    // consumable HUD тут більше НЕ запускаємо.
    // Спочатку починаємо штатне ховання зброї.
    //
    m_active = true;
    m_effect_applied = false;
    m_waiting_for_weapon_hide = true;

    m_start_time = 0;
    m_animation_duration = 0;

    LockActor();

    if (!m_actor_locked) {
        Reset();
        return false;
    }

    Msg("* ItemUse waiting for weapon hide: [%s]", m_item_section.c_str());

    return true;
}

void CItemUseController::LockActor()
{
    if (!m_actor || m_actor_locked)
        return;

    m_prev_inventory_disabled =
        m_actor->inventory_disabled();

    if (CurrentGameUI())
        CurrentGameUI()->HideActorMenu();

    m_actor->set_inventory_disabled(true);

    //
    // From this moment the actor cannot attach to ladders.
    //
    LockActorLadder();

    m_actor->SetWeaponHideState(
        INV_STATE_BLOCK_ALL,
        true
    );

    m_actor_locked = true;

    Msg("* ItemUse actor locked");
}

void CItemUseController::UnlockActor()
{
    if (!m_actor_locked)
        return;

    if (m_actor)
    {
        m_actor->SetWeaponHideState(
            INV_STATE_BLOCK_ALL,
            false
        );

        m_actor->set_inventory_disabled(
            m_prev_inventory_disabled
        );
    }

    //
    // Always release our ladder lock.
    //
    UnlockActorLadder();

    m_actor_locked = false;
    m_prev_inventory_disabled = false;

    Msg("* ItemUse actor unlocked");
}

bool CItemUseController::CanStartAnimation()
{
    if (!m_actor)
        return false;

    CInventory& inv = m_actor->inventory();

    //
    // Чекаємо завершення штатної hide-анімації.
    //
    if (inv.GetActiveSlot() != NO_ACTIVE_SLOT)
        return false;

    if (inv.GetNextActiveSlot() != NO_ACTIVE_SLOT)
        return false;

    //
    // Detector — окремий HUD item, тому active slot
    // сам по собі його не гарантує.
    //
    CCustomDetector* detector = smart_cast<CCustomDetector*>(inv.ItemFromSlot(DETECTOR_SLOT));

    if (detector && !detector->IsHidden()) {
        //
        // Повторний виклик безпечний.
        // Якщо detector ще showing/hiding —
        // наступного кадру перевіримо знову.
        //
        detector->HideDetector(true);
        return false;
    }

    return true;
}

void CItemUseController::BeginAnimation()
{
    if (!m_active)
        return;

    if (!m_actor || !g_player_hud) {
        Cancel();
        return;
    }

    if (!g_player_hud->attach_controller_item(m_hud_section)) {
        Msg("! ItemUse: failed to attach HUD [%s]", m_hud_section.c_str());

        Cancel();
        return;
    }

    LoadAnimSound();

    //
    // FALSE — наш уже перевірений фікс
    // "руки прилітають з іншого виміру".
    //
    m_animation_duration = g_player_hud->play_controller_motion("anm_show", FALSE);

    if (m_animation_duration == 0) {
        Msg("! ItemUse: failed to play animation [%s]", m_hud_section.c_str());

        Cancel();
        return;
    }

    //
    // Effect timing не може бути довшим
    // за саму animation.
    //
    if (m_action_time > m_animation_duration)
        m_action_time = m_animation_duration;

    //
    // Звук запускається саме разом із consumable animation,
    // а не під час holster weapon.
    //
    PlayAnimSound();

    m_start_time = Device.dwTimeGlobal;
    m_waiting_for_weapon_hide = false;

    Msg("* ItemUse started: [%s], HUD [%s], duration [%u], effect [%u], sound [%s]",
        m_item_section.c_str(), m_hud_section.c_str(), m_animation_duration, m_action_time,
        m_anim_sound_loaded ? "yes" : "no");
}

void CItemUseController::Update(float dt)
{
    (void)dt;

    if (!m_active)
        return;

    //
    // Actor зник / помер — abort без застосування item effect.
    //
    if (!m_actor || !m_actor->g_Alive()) {
        Cancel();
        return;
    }

    //
    // Якщо HUD subsystem раптом недоступна —
    // не залишаємо actor заблокованим назавжди.
    //
    if (!g_player_hud) {
        Cancel();
        return;
    }

    //
    // Фаза 1:
    // чекаємо weapon + detector hide.
    //
    if (m_waiting_for_weapon_hide) {
        if (CanStartAnimation())
            BeginAnimation();

        return;
    }

    //
    // Фаза 2:
    // consumable animation уже йде.
    //
    const u32 elapsed = Device.dwTimeGlobal - m_start_time;

    //
    // Реальний effect moment.
    //
    if (!m_effect_applied && elapsed >= m_action_time) {
        if (!m_item) {
            Msg("! ItemUse: source item is NULL");
            Cancel();
            return;
        }

        bool became_empty = false;

        // Controller will spawn physical waste exactly at animation end.
        if (!m_actor->inventory().ApplyEat(m_item, became_empty, false)) {
            Msg("! ItemUse: failed to apply effect for [%s]", m_item_section.c_str());

            Cancel();
            return;
        }

        m_effect_applied = true;

        Msg("* ItemUse effect applied: [%s]", m_item_section.c_str());

        //
        // Після останньої порції item уже
        // позначений SetDropManual(TRUE).
        //
        if (became_empty)
            m_item = NULL;
    }

    //
    // Завершення animation.
    //
    if (m_animation_duration > 0 && elapsed >= m_animation_duration) {
        Finish();
    }
}

void CItemUseController::Cancel() {
    if (!m_active)
        return;

    //
    // If effect has already happened,
    // physical waste must not magically disappear.
    //
    if (m_effect_applied && !m_trash_spawned) {
        SpawnTrash();
    }

    DestroyAnimSound();

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    UnlockActor();

    Msg("* ItemUse cancelled: [%s]", m_item_section.c_str());

    Reset();
}

void CItemUseController::Finish() {
    if (!m_active)
        return;

    //
    // Normal physical trash moment:
    // real end of consumable animation.
    //
    if (m_effect_applied && !m_trash_spawned) {
        SpawnTrash();
    }

    DestroyAnimSound();

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    UnlockActor();

    Msg("* ItemUse finished: [%s]", m_item_section.c_str());

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

    m_waiting_for_weapon_hide = false;
    m_actor_locked = false;
    m_prev_inventory_disabled = false;

    m_trash_section = NULL;
    m_trash_count = 0;
    m_trash_spawned = false;
}

void CItemUseController::LoadAnimSound() {
    DestroyAnimSound();

    if (!pSettings->line_exist(m_use_section, "snd_using_anim"))
        return;

    HUD_SOUND_ITEM::LoadSound(m_use_section.c_str(), "snd_using_anim", m_anim_sound, sg_SourceType);

    m_anim_sound_loaded = true;
}

void CItemUseController::PlayAnimSound() {
    if (!m_anim_sound_loaded)
        return;

    if (!m_actor)
        return;

    HUD_SOUND_ITEM::PlaySound(m_anim_sound, m_actor->Position(), m_actor,
                              true, // HUD mode -> sm_2D
                              false // not looped
    );
}

void CItemUseController::StopAnimSound() {
    if (!m_anim_sound_loaded)
        return;

    HUD_SOUND_ITEM::StopSound(m_anim_sound);
}

void CItemUseController::DestroyAnimSound() {
    if (!m_anim_sound_loaded)
        return;

    HUD_SOUND_ITEM::StopSound(m_anim_sound);
    HUD_SOUND_ITEM::DestroySound(m_anim_sound);

    m_anim_sound_loaded = false;
}

void CItemUseController::SpawnTrash() {
    if (m_trash_spawned)
        return;

    if (!m_actor)
        return;

    if (!m_trash_section.size() || m_trash_count == 0) {
        return;
    }

    SpawnConsumableTrash(m_actor, m_trash_section, m_trash_count);

    m_trash_spawned = true;
}
