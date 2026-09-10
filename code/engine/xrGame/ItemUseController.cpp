////////////////////////////////////////////////////////////////////////////
//	Module 		: ItemUseController.cpp
//	Created 	: 23.08.2026
//  Modified 	: 06.09.2026
//	Author		: Bohdan «Infernis» Kyslyi
//	Description : Item use controller
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ItemUseController.h"

#include "Actor.h"
#include "CustomMonster.h"
#include "HudItem.h"
#include "player_hud.h"
#include "inventory.h"
#include "level.h"

#include "CustomDetector.h"
#include "CustomOutfit.h"
#include "UIGameCustom.h"
#include "ui/UIActorMenu.h"
#include "ActorEffector.h"
#include "ParticlesObject.h"

#include "../xrPhysics/ElevatorState.h"
#include "eatable_item.h"
#include "ai_space.h"
#include "script_engine.h"
#include <luabind/functor.hpp>

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

static bool MutantLootAnimationsEnabled() {
    static bool initialized = false;
    static bool enabled = false;

    if (!initialized) {
        initialized = true;

        if (pSettings->section_exist("items_animations") &&
            pSettings->line_exist("items_animations", "enable_mutant_looting_animations")) {
            enabled =
                !!pSettings->r_bool("items_animations", "enable_mutant_looting_animations");
        }

        Msg("* Mutant loot animations: [%s]", enabled ? "enabled" : "disabled");
    }

    return enabled;
}

static bool MutantLootParticlesEnabled() {
    static bool initialized = false;
    static bool enabled = false;

    if (!initialized) {
        initialized = true;

        if (pSettings->section_exist("items_animations") &&
            pSettings->line_exist("items_animations", "enable_mutant_looting_particles")) {
            enabled =
                !!pSettings->r_bool("items_animations", "enable_mutant_looting_particles");
        }

        Msg("* Mutant loot particles: [%s]", enabled ? "enabled" : "disabled");
    }

    return enabled;
}

static u32 MutantLootParticleTiming() {
    if (!pSettings->section_exist("items_animations") ||
        !pSettings->line_exist("items_animations", "mutant_looting_particle_timing")) {
        return 0;
    }

    return pSettings->r_u32("items_animations", "mutant_looting_particle_timing");
}

static bool HudBlocksMovement(const shared_str& hud_section) {
    if (!hud_section.size() || !pSettings->section_exist(hud_section.c_str()))
        return false;

    if (pSettings->line_exist(hud_section.c_str(), "block_movement"))
        return !!pSettings->r_bool(hud_section.c_str(), "block_movement");

    // Compatibility alias used by some configs.
    return pSettings->line_exist(hud_section.c_str(), "block_move") &&
           !!pSettings->r_bool(hud_section.c_str(), "block_move");
}

CItemUseController::CItemUseController(CActor* actor)
    : m_actor(actor),
      m_item(NULL),
      m_start_time(0),
      m_action_time(0),
      m_animation_duration(0),
      m_active(false),
      m_effect_applied(false),
      m_controller_mode(eControllerModeNone),
      m_hud_animation_phase(eHudAnimationNone),
      m_hud_animation_hide_requested(false),
      m_hud_animation_allow_inventory(false),
      m_mutant_loot_target_id(u16(-1)),
      m_mutant_loot_particle_time(0),
      m_mutant_loot_particle_enabled(false),
      m_mutant_loot_particle_started(false),
      m_queued_consumable_id(u16(-1)),
      m_deferred_hud_animation_section(NULL),
      m_queued_hud_animation_section(NULL),
      m_outfit_hud_refresh_pending(false),
      m_block_movement(false),
      m_waiting_for_weapon_hide(false),
      m_actor_locked(false),
      m_prev_inventory_disabled(false),

      m_trash_count(0), m_trash_spawned(false),

      m_anim_sound_loaded(false),
      m_camera_effector_started(false),
      m_use_particles_start_time(0),
      m_use_particles_stop_time(u32(-1)),
      m_use_particles(NULL),
      m_use_particles_started(false) {
    m_use_particles_offset.set(0.f, 0.f, 0.f);
    m_use_particles_orientation.set(0.f, 0.f, 0.f);
}

CItemUseController::~CItemUseController()
{
    Cancel();
}

bool CItemUseController::Start(CInventoryItem* item) {
    if (!item || m_active)
        return false;

    shared_str item_section;
    shared_str use_section;
    shared_str state_section;
    shared_str hud_section;

    // Returning false tells CInventory::Eat() to use the normal immediate
    // ApplyEat() path, preserving non-animated consumables.
    if (!ResolveConsumableAnimation(item, item_section, use_section, state_section,
                                    hud_section)) {
        return false;
    }

    m_item = item;
    m_item_section = item_section;
    m_use_section = use_section;
    m_state_section = state_section;
    m_hud_section = hud_section;
    LoadStopFunction();

    m_trash_section = NULL;
    m_trash_count = 0;
    m_trash_spawned = false;

    CEatableItem* eatable = smart_cast<CEatableItem*>(item);

    //
    // Cache physical waste for THIS exact portion/state.
    //
    if (eatable && eatable->HasTrash()) {
        m_trash_section = eatable->TrashObject();

        m_trash_count = eatable->TrashCount();
    }

    LoadUseParticles();

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
    m_controller_mode = eControllerModeConsumable;
    m_hud_animation_phase = eHudAnimationNone;
    m_hud_animation_hide_requested = false;
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

bool CItemUseController::StartMutantLoot(CCustomMonster* monster) {
    if (!monster || !m_actor || !monster->CanMutantLoot(m_actor))
        return false;

    // A configured corpse must never fall through into Lua/corpse inventory
    // handling while another controller transition is still settling.
    if (m_active || m_deferred_hud_animation_section.size())
        return true;

    shared_str hud_section;
    bool use_animation = MutantLootAnimationsEnabled();

    if (use_animation) {
        if (!pSettings->section_exist("items_animations") ||
            !pSettings->line_exist("items_animations", "mutant_looting_hud")) {
            Msg("! MutantLoot: [items_animations] has no [mutant_looting_hud]; using "
                "immediate fallback");
            use_animation = false;
        } else {
            LPCSTR configured_hud =
                pSettings->r_string("items_animations", "mutant_looting_hud");

            if (!configured_hud || !configured_hud[0] || !xr_strcmp(configured_hud, "none")) {
                Msg("! MutantLoot: [mutant_looting_hud] is empty; using immediate fallback");
                use_animation = false;
            } else {
                hud_section = configured_hud;
            }
        }
    }

    if (use_animation &&
        (!pSettings->section_exist(hud_section.c_str()) ||
         !pSettings->line_exist(hud_section.c_str(), "anm_show") || !g_player_hud ||
         !g_player_hud->can_attach_controller_item(hud_section))) {
        Msg("! MutantLoot: HUD [%s] is missing or invalid; using immediate fallback",
            hud_section.c_str());
        use_animation = false;
    }

    if (!use_animation)
        return CompleteMutantLootImmediately(monster);

    if (!monster->BeginMutantLoot(m_actor))
        return false;

    m_item = NULL;
    m_item_section = monster->cNameSect();
    m_use_section = NULL;
    m_state_section = NULL;
    m_hud_section = hud_section;
    LoadStopFunction();

    m_start_time = 0;
    if (pSettings->line_exist(hud_section.c_str(), "action_timing"))
        m_action_time = pSettings->r_u32(hud_section.c_str(), "action_timing");
    else if (pSettings->line_exist(hud_section.c_str(), "timing"))
        m_action_time = pSettings->r_u32(hud_section.c_str(), "timing");
    else
        m_action_time = u32(-1);
    m_animation_duration = 0;

    m_active = true;
    m_effect_applied = false;
    m_controller_mode = eControllerModeMutantLoot;
    m_hud_animation_phase = eHudAnimationNone;
    m_hud_animation_hide_requested = false;
    m_hud_animation_allow_inventory = false;
    m_mutant_loot_target_id = monster->ID();
    m_mutant_loot_particle_time = MutantLootParticleTiming();
    m_mutant_loot_particle_enabled = MutantLootParticlesEnabled();
    m_mutant_loot_particle_started = false;
    m_waiting_for_weapon_hide = true;

    LockActor();

    if (!m_actor_locked) {
        ReleaseMutantLootReservation();
        Reset();
        return CompleteMutantLootImmediately(monster);
    }

    Msg("* MutantLoot: HUD animation waiting for weapon hide, corpse [%u][%s], HUD [%s]",
        (u32)monster->ID(), monster->cNameSect().c_str(), m_hud_section.c_str());
    return true;
}

bool CItemUseController::ResolveConsumableAnimation(CInventoryItem* item,
                                                    shared_str& item_section,
                                                    shared_str& use_section,
                                                    shared_str& state_section,
                                                    shared_str& hud_section) const {
    if (!item || !ConsumableAnimationsEnabled())
        return false;

    item_section = item->object().cNameSect();
    use_section = NULL;
    state_section = NULL;
    hud_section = NULL;

    // 1. The physical item points to its use-section.
    if (!pSettings->line_exist(item_section, "hud"))
        return false;

    use_section = pSettings->r_string(item_section, "hud");

    if (!pSettings->section_exist(use_section) ||
        !pSettings->line_exist(use_section, "timing")) {
        return false;
    }

    // 2. A portion-specific HUD has priority over the legacy use-section HUD.
    CEatableItem* eatable = smart_cast<CEatableItem*>(item);

    if (eatable) {
        state_section = eatable->PortionStateSection();

        if (state_section.size() && pSettings->line_exist(state_section.c_str(), "hud"))
            hud_section = pSettings->r_string(state_section.c_str(), "hud");
    }

    if (!hud_section.size() && pSettings->line_exist(use_section, "hud"))
        hud_section = pSettings->r_string(use_section, "hud");

    // 3. The resolved HUD section must provide the consumable entry motion.
    return hud_section.size() && pSettings->section_exist(hud_section) &&
           pSettings->line_exist(hud_section, "anm_show");
}

bool CItemUseController::StartHudAnimation(const shared_str& hud_section,
                                           bool allow_inventory) {
    return StartHudAnimationInternal(hud_section, allow_inventory, false);
}

bool CItemUseController::StartHudAnimationOnce(const shared_str& hud_section) {
    return StartHudAnimationInternal(hud_section, false, true);
}

bool CItemUseController::StartHudAnimationInternal(const shared_str& hud_section,
                                                   bool allow_inventory,
                                                   bool one_shot) {
    if (IsBusy() || !m_actor || !g_player_hud || !hud_section.size())
        return false;

    if (!pSettings->section_exist(hud_section.c_str())) {
        Msg("! ItemUse: HUD animation section [%s] does not exist", hud_section.c_str());
        return false;
    }

    // anm_show remains the entry motion for old consumables, persistent HUD
    // sequences and one-shot dressing animations. Only persistent sequences
    // can continue into optional anm_idle and anm_hide motions.
    if (!pSettings->line_exist(hud_section.c_str(), "anm_show")) {
        Msg("! ItemUse: HUD animation section [%s] has no [anm_show]", hud_section.c_str());
        return false;
    }

    // Validate the resolved hand motion before hiding the inventory or weapon.
    // player_hud's regular HUD loader asserts on a missing motion, while an
    // optional controller animation must safely fall back to normal gameplay.
    if (!g_player_hud->can_attach_controller_item(hud_section)) {
        Msg("! ItemUse: HUD animation section [%s] has an invalid item visual or unavailable "
            "motion; check HUD fields, [hands_animations_path] and OMF files",
            hud_section.c_str());
        return false;
    }

    m_item = NULL;
    m_item_section = NULL;
    m_use_section = NULL;
    m_state_section = NULL;
    m_hud_section = hud_section;
    LoadStopFunction();

    m_start_time = 0;
    m_action_time = 0;
    m_animation_duration = 0;

    m_active = true;
    m_effect_applied = false;
    m_controller_mode = one_shot ? eControllerModeHudAnimationOneShot
                                 : eControllerModeHudAnimation;
    m_hud_animation_phase = eHudAnimationNone;
    m_hud_animation_hide_requested = false;
    m_hud_animation_allow_inventory = allow_inventory;
    m_queued_consumable_id = u16(-1);
    m_queued_hud_animation_section = NULL;
    m_waiting_for_weapon_hide = true;

    LockActor();

    if (!m_actor_locked) {
        Reset();
        return false;
    }

    Msg("* ItemUse %s HUD animation waiting for weapon hide: [%s]",
        one_shot ? "one-shot" : "persistent", m_hud_section.c_str());

    return true;
}

void CItemUseController::RequestHudAnimationHide() {
    if (!m_active || m_controller_mode != eControllerModeHudAnimation)
        return;

    m_hud_animation_hide_requested = true;

    // Nothing has reached the screen yet, so there is no hide animation to
    // play. This also releases all locks immediately.
    if (m_waiting_for_weapon_hide) {
        Cancel();
        return;
    }

    // A close request during show is queued, so show is never cut in half.
    if (m_hud_animation_phase == eHudAnimationIdle)
        BeginHudAnimationHide();
}

bool CItemUseController::IsHudAnimationActive() const {
    return m_active && m_controller_mode == eControllerModeHudAnimation;
}

bool CItemUseController::IsHudAnimationIdle() const {
    return IsHudAnimationActive() && m_hud_animation_phase == eHudAnimationIdle;
}

bool CItemUseController::CanUseConsumables() const {
    return CanQueueAfterHudHide();
}

bool CItemUseController::CanQueueAfterHudHide() const {
    return IsHudAnimationIdle() && m_hud_animation_allow_inventory &&
           m_queued_consumable_id == u16(-1) &&
           !m_deferred_hud_animation_section.size() &&
           !m_queued_hud_animation_section.size();
}

bool CItemUseController::TryQueueConsumable(CInventoryItem* item) {
    if (!item || !CanUseConsumables())
        return false;

    shared_str item_section;
    shared_str use_section;
    shared_str state_section;
    shared_str hud_section;

    // Non-animated consumables are intentionally not queued: CInventory::Eat()
    // will apply them immediately while the backpack remains in idle.
    if (!ResolveConsumableAnimation(item, item_section, use_section, state_section,
                                    hud_section)) {
        return false;
    }

    m_queued_consumable_id = item->object().ID();

    Msg("* ItemUse queued after HUD hide: [%s]", item_section.c_str());

    // Closing the actor menu also requests the backpack hide lifecycle. Keep a
    // direct request as a fallback for controller use outside CUIGameCustom.
    if (CurrentGameUI())
        CurrentGameUI()->HideActorMenu();

    RequestHudAnimationHide();
    return true;
}

bool CItemUseController::QueueHudAnimationOnce(const shared_str& hud_section,
                                               bool refresh_outfit_hud) {
    if (!hud_section.size() || m_deferred_hud_animation_section.size())
        return false;

    // Slot placement can happen inside CUIActorMenu::ToSlot(), before its
    // CUICellItem has been removed from the old drag-drop container. Starting
    // immediately would hide/rebuild that UI and leave ToSlot() with a stale
    // cell pointer. Keep the request until the controller's next frame update.
    if (m_active && !CanQueueAfterHudHide())
        return false;

    m_deferred_hud_animation_section = hud_section;

    if (refresh_outfit_hud)
        m_outfit_hud_refresh_pending = true;

    return true;
}

bool CItemUseController::DeferOutfitHudRefresh() {
    if (!m_actor || Level().CurrentViewEntity() != m_actor || !CurrentGameUI() ||
        !CurrentGameUI()->ActorMenu().IsShown() ||
        CurrentGameUI()->ActorMenu().GetMenuMode() != mmInventory ||
        !pSettings->section_exist("items_animations") ||
        !pSettings->line_exist("items_animations", "enable_dressing_animations") ||
        !pSettings->r_bool("items_animations", "enable_dressing_animations")) {
        return false;
    }

    m_outfit_hud_refresh_pending = true;
    return true;
}

void CItemUseController::ApplyPendingOutfitHudRefresh() {
    if (!m_outfit_hud_refresh_pending)
        return;

    m_outfit_hud_refresh_pending = false;

    if (!m_actor || Level().CurrentViewEntity() != m_actor || !g_player_hud)
        return;

    CCustomOutfit* outfit = m_actor->GetOutfit();

    if (outfit)
        outfit->ApplySkinModel(m_actor, true, true);
    else
        g_player_hud->load_default();

    Msg("* ItemUse: actor outfit HUD refreshed at animation transition");
}

bool CItemUseController::TryQueueHudAnimationOnce(const shared_str& hud_section) {
    if (!CanQueueAfterHudHide() || !hud_section.size())
        return false;

    if (!pSettings->section_exist(hud_section.c_str())) {
        Msg("! ItemUse: queued HUD animation section [%s] does not exist",
            hud_section.c_str());
        return false;
    }

    if (!pSettings->line_exist(hud_section.c_str(), "anm_show")) {
        Msg("! ItemUse: queued HUD animation section [%s] has no [anm_show]",
            hud_section.c_str());
        return false;
    }

    // An outfit dressing request is still using the old hands during backpack
    // hide. Validate its motion only after the pending outfit HUD is applied.
    if (!g_player_hud ||
        (!m_outfit_hud_refresh_pending &&
         !g_player_hud->can_attach_controller_item(hud_section))) {
        Msg("! ItemUse: queued HUD animation section [%s] has an invalid item visual or "
            "unavailable motion",
            hud_section.c_str());
        return false;
    }

    m_queued_hud_animation_section = hud_section;

    // A queued dressing HUD owns the whole transition, including the preceding
    // backpack hide. Preserve an existing persistent lock or enable the target
    // HUD's stronger movement policy immediately.
    if (HudBlocksMovement(hud_section)) {
        m_block_movement = true;

        if (m_actor)
            m_actor->StopAnyMove();
    }

    Msg("* ItemUse one-shot HUD queued after persistent HUD hide: [%s]",
        hud_section.c_str());

    if (CurrentGameUI())
        CurrentGameUI()->HideActorMenu();

    RequestHudAnimationHide();
    return true;
}

void CItemUseController::LockActor()
{
    if (!m_actor || m_actor_locked)
        return;

    m_block_movement = HudBlocksMovement(m_hud_section);

    if (m_block_movement)
        m_actor->StopAnyMove();

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

        CCustomMonster* mutant =
            m_controller_mode == eControllerModeMutantLoot ? MutantLootTarget() : NULL;
        Cancel();

        if (mutant)
            CompleteMutantLootImmediately(mutant);

        return;
    }

    m_waiting_for_weapon_hide = false;

    if (m_controller_mode == eControllerModeMutantLoot) {
        shared_str played_motion_name;

        if (!PlayHudAnimationMotion("anm_show", eHudAnimationShow, FALSE,
                                    &played_motion_name)) {
            Msg("! MutantLoot: failed to play HUD animation [%s]; using immediate fallback",
                m_hud_section.c_str());

            CCustomMonster* mutant = MutantLootTarget();
            Cancel();

            if (mutant)
                CompleteMutantLootImmediately(mutant);

            return;
        }

        if (m_action_time == u32(-1) || m_action_time > m_animation_duration)
            m_action_time = m_animation_duration;

        if (m_mutant_loot_particle_time > m_animation_duration)
            m_mutant_loot_particle_time = m_animation_duration;

        PlayHudAnimationSound("snd_show");
        StartCameraEffector(played_motion_name);

        Msg("* MutantLoot: HUD animation started, corpse [%u], HUD [%s], duration [%u], "
            "effect [%u], particle [%s/%u], sound [%s], camera [%s]",
            (u32)m_mutant_loot_target_id, m_hud_section.c_str(), m_animation_duration,
            m_action_time, m_mutant_loot_particle_enabled ? "yes" : "no",
            m_mutant_loot_particle_time, m_anim_sound_loaded ? "yes" : "no",
            m_camera_effector_started ? "yes" : "no");
        return;
    }

    if (m_controller_mode == eControllerModeHudAnimation ||
        m_controller_mode == eControllerModeHudAnimationOneShot) {
        shared_str played_motion_name;

        if (!PlayHudAnimationMotion("anm_show", eHudAnimationShow, FALSE,
                                    &played_motion_name)) {
            Msg("! ItemUse: failed to play %s HUD show animation [%s]",
                m_controller_mode == eControllerModeHudAnimationOneShot ? "one-shot"
                                                                        : "persistent",
                m_hud_section.c_str());
            Cancel();
            return;
        }

        PlayHudAnimationSound("snd_show");
        StartCameraEffector(played_motion_name);

        Msg("* ItemUse %s HUD animation started: [%s], show duration [%u], sound [%s], "
            "camera [%s]",
            m_controller_mode == eControllerModeHudAnimationOneShot ? "one-shot"
                                                                    : "persistent",
            m_hud_section.c_str(), m_animation_duration,
            m_anim_sound_loaded ? "yes" : "no",
            m_camera_effector_started ? "yes" : "no");
        return;
    }

    LoadAnimSound();

    //
    // FALSE — наш уже перевірений фікс
    // "руки прилітають з іншого виміру".
    //
    shared_str played_motion_name;
    m_animation_duration =
        g_player_hud->play_controller_motion("anm_show", FALSE, &played_motion_name);

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
    StartCameraEffector(played_motion_name);

    if (m_use_particles_stop_time == u32(-1) ||
        m_use_particles_stop_time > m_animation_duration) {
        m_use_particles_stop_time = m_animation_duration;
    }

    if (m_use_particles_name.size() &&
        m_use_particles_start_time >= m_use_particles_stop_time) {
        Msg("! ItemUse: invalid particle interval [%u, %u] for [%s]",
            m_use_particles_start_time, m_use_particles_stop_time,
            m_item_section.c_str());
    }

    Msg("* ItemUse started: [%s], HUD [%s], duration [%u], effect [%u], sound [%s], "
        "camera [%s], particles [%s]",
        m_item_section.c_str(), m_hud_section.c_str(), m_animation_duration, m_action_time,
        m_anim_sound_loaded ? "yes" : "no", m_camera_effector_started ? "yes" : "no",
        m_use_particles_name.size() ? "configured" : "no");
}

bool CItemUseController::PlayHudAnimationMotion(LPCSTR motion_name,
                                                EHudAnimationPhase phase,
                                                BOOL mix_in,
                                                shared_str* played_motion_name) {
    if (!g_player_hud || !motion_name || !motion_name[0])
        return false;

    if (!g_player_hud->has_controller_motion(motion_name))
        return false;

    m_animation_duration =
        g_player_hud->play_controller_motion(motion_name, mix_in, played_motion_name);

    if (!m_animation_duration)
        return false;

    m_start_time = Device.dwTimeGlobal;
    m_hud_animation_phase = phase;

    return true;
}

void CItemUseController::BeginHudAnimationIdle() {
    if (!m_active || m_controller_mode != eControllerModeHudAnimation)
        return;

    if (m_hud_animation_hide_requested) {
        BeginHudAnimationHide();
        return;
    }

    // anm_idle is optional. Without it, the show cycle remains on the model
    // while the controller still exposes the logical idle/ready state.
    if (!PlayHudAnimationMotion("anm_idle", eHudAnimationIdle, TRUE)) {
        m_start_time = Device.dwTimeGlobal;
        m_animation_duration = 0;
        m_hud_animation_phase = eHudAnimationIdle;
    }

    // Backpack inventory keeps the weapon/ladders locked, but the menu itself
    // must be able to send item-use requests throughout the idle phase.
    if (m_hud_animation_allow_inventory && m_actor && m_actor_locked)
        m_actor->set_inventory_disabled(m_prev_inventory_disabled);

    Msg("* ItemUse HUD animation idle: [%s], motion [%s]", m_hud_section.c_str(),
        g_player_hud->has_controller_motion("anm_idle") ? "anm_idle" : "show fallback");
}

void CItemUseController::BeginHudAnimationHide() {
    if (!m_active || m_controller_mode != eControllerModeHudAnimation)
        return;

    if (m_hud_animation_allow_inventory && m_actor && m_actor_locked)
        m_actor->set_inventory_disabled(true);

    // anm_hide is optional. If it is absent, closing remains instant and the
    // caller does not need a special fallback path.
    if (!PlayHudAnimationMotion("anm_hide", eHudAnimationHide, TRUE)) {
        Finish();
        return;
    }

    PlayHudAnimationSound("snd_hide");

    Msg("* ItemUse HUD animation hide: [%s], duration [%u], sound [%s]",
        m_hud_section.c_str(), m_animation_duration,
        m_anim_sound_loaded ? "yes" : "no");
}

void CItemUseController::UpdateHudAnimation() {
    if (m_hud_animation_phase == eHudAnimationShow) {
        const u32 elapsed = Device.dwTimeGlobal - m_start_time;

        if (elapsed >= m_animation_duration) {
            if (m_controller_mode == eControllerModeHudAnimationOneShot)
                Finish();
            else
                BeginHudAnimationIdle();
        }

        return;
    }

    if (m_hud_animation_phase == eHudAnimationIdle) {
        if (m_hud_animation_hide_requested)
            BeginHudAnimationHide();

        return;
    }

    if (m_hud_animation_phase == eHudAnimationHide) {
        const u32 elapsed = Device.dwTimeGlobal - m_start_time;

        if (elapsed >= m_animation_duration)
            Finish();
    }
}

CCustomMonster* CItemUseController::MutantLootTarget() const {
    if (!g_pGameLevel || m_mutant_loot_target_id == u16(-1))
        return NULL;

    CObject* object = Level().Objects.net_Find(m_mutant_loot_target_id);
    CGameObject* game_object = smart_cast<CGameObject*>(object);

    return game_object ? game_object->cast_custom_monster() : NULL;
}

bool CItemUseController::ApplyMutantLootEffect() {
    CCustomMonster* monster = MutantLootTarget();

    if (!monster) {
        Msg("! MutantLoot: target corpse [%u] disappeared before effect timing",
            (u32)m_mutant_loot_target_id);
        return false;
    }

    if (!monster->CompleteMutantLoot(m_actor))
        return false;

    m_effect_applied = true;
    return true;
}

void CItemUseController::ApplyMutantLootParticle() {
    if (!m_mutant_loot_particle_enabled || m_mutant_loot_particle_started)
        return;

    CCustomMonster* monster = MutantLootTarget();

    if (monster)
        monster->PlayMutantLootParticle();
    else
        Msg("! MutantLoot: target corpse [%u] disappeared before particle timing",
            (u32)m_mutant_loot_target_id);

    m_mutant_loot_particle_started = true;
}

void CItemUseController::ReleaseMutantLootReservation() {
    CCustomMonster* monster = MutantLootTarget();

    if (monster)
        monster->CancelMutantLoot(m_actor);
}

bool CItemUseController::CompleteMutantLootImmediately(CCustomMonster* monster) {
    if (!monster || !m_actor || !monster->BeginMutantLoot(m_actor))
        return false;

    if (!monster->CompleteMutantLoot(m_actor)) {
        monster->CancelMutantLoot(m_actor);
        Msg("! MutantLoot: immediate harvesting failed for corpse [%u][%s]",
            (u32)monster->ID(), monster->cNameSect().c_str());
    } else {
        Msg("* MutantLoot: immediate fallback completed for corpse [%u][%s]",
            (u32)monster->ID(), monster->cNameSect().c_str());
    }

    // The configured corpse interaction was handled even if a transient
    // server-side condition prevented completion. Never fall through to Lua.
    return true;
}

void CItemUseController::UpdateMutantLootAnimation() {
    if (m_hud_animation_phase != eHudAnimationShow)
        return;

    const u32 elapsed = Device.dwTimeGlobal - m_start_time;

    if (m_mutant_loot_particle_enabled && !m_mutant_loot_particle_started &&
        elapsed >= m_mutant_loot_particle_time) {
        ApplyMutantLootParticle();
    }

    if (!m_effect_applied && elapsed >= m_action_time) {
        if (!ApplyMutantLootEffect()) {
            Msg("! MutantLoot: effect failed for corpse [%u]", (u32)m_mutant_loot_target_id);
            Cancel();
            return;
        }
    }

    if (m_animation_duration > 0 && elapsed >= m_animation_duration)
        Finish();
}

void CItemUseController::Update(float dt)
{
    (void)dt;

    if (m_deferred_hud_animation_section.size()) {
        const shared_str deferred_hud_animation_section =
            m_deferred_hud_animation_section;
        m_deferred_hud_animation_section = NULL;

        if (!m_actor || !m_actor->g_Alive() || !g_player_hud) {
            Msg("! ItemUse: deferred one-shot HUD [%s] discarded because actor/HUD is "
                "unavailable",
                deferred_hud_animation_section.c_str());
        } else if (m_active) {
            if (!TryQueueHudAnimationOnce(deferred_hud_animation_section)) {
                Msg("! ItemUse: deferred one-shot HUD [%s] could not be queued",
                    deferred_hud_animation_section.c_str());
            }
        } else {
            // Outfit placement has already updated the real equipment slot and
            // third-person model. Swap only the first-person hands at the exact
            // transition between a previous HUD hide and dressing anm_show.
            ApplyPendingOutfitHudRefresh();

            if (!StartHudAnimationOnce(deferred_hud_animation_section)) {
                Msg("! ItemUse: deferred one-shot HUD [%s] failed to start",
                    deferred_hud_animation_section.c_str());
            }
        }
    }

    // Removing an outfit without replacing it has no dressing animation, but
    // its delayed default-hands refresh still needs to happen after the UI
    // operation has completed.
    if (!m_active && m_outfit_hud_refresh_pending)
        ApplyPendingOutfitHudRefresh();

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

    if (m_controller_mode == eControllerModeHudAnimation ||
        m_controller_mode == eControllerModeHudAnimationOneShot) {
        UpdateHudAnimation();
        return;
    }

    if (m_controller_mode == eControllerModeMutantLoot) {
        UpdateMutantLootAnimation();
        return;
    }

    //
    // Фаза 2:
    // consumable animation уже йде.
    //
    const u32 elapsed = Device.dwTimeGlobal - m_start_time;

    if (m_use_particles_name.size() && !m_use_particles_started &&
        elapsed >= m_use_particles_start_time && elapsed < m_use_particles_stop_time) {
        StartUseParticles();
    }

    if (m_use_particles) {
        if (elapsed >= m_use_particles_stop_time)
            StopUseParticles();
        else
            UpdateUseParticles();
    }

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

    const bool refresh_outfit_hud = m_outfit_hud_refresh_pending;

    if (m_controller_mode == eControllerModeMutantLoot && !m_effect_applied)
        ReleaseMutantLootReservation();

    //
    // If effect has already happened,
    // physical waste must not magically disappear.
    //
    if (m_effect_applied && !m_trash_spawned) {
        SpawnTrash();
    }

    DestroyAnimSound();
    StopCameraEffector();
    StopUseParticles();

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    UnlockActor();

    if (m_controller_mode == eControllerModeHudAnimation)
        Msg("* ItemUse HUD animation cancelled: [%s]", m_hud_section.c_str());
    else if (m_controller_mode == eControllerModeHudAnimationOneShot)
        Msg("* ItemUse one-shot HUD animation cancelled: [%s]", m_hud_section.c_str());
    else if (m_controller_mode == eControllerModeMutantLoot)
        Msg("* MutantLoot: HUD animation cancelled for corpse [%u]",
            (u32)m_mutant_loot_target_id);
    else
        Msg("* ItemUse cancelled: [%s]", m_item_section.c_str());

    Reset();

    if (refresh_outfit_hud) {
        m_outfit_hud_refresh_pending = true;
        ApplyPendingOutfitHudRefresh();
    }
}

void CItemUseController::Finish() {
    if (!m_active)
        return;

    if (m_controller_mode == eControllerModeMutantLoot) {
        ApplyMutantLootParticle();

        if (!m_effect_applied && !ApplyMutantLootEffect()) {
            Cancel();
            return;
        }
    }

    const bool start_queued_consumable =
        m_controller_mode == eControllerModeHudAnimation &&
        m_queued_consumable_id != u16(-1);
    const u16 queued_consumable_id = m_queued_consumable_id;
    const bool start_queued_hud_animation =
        m_controller_mode == eControllerModeHudAnimation &&
        m_queued_hud_animation_section.size();
    const shared_str queued_hud_animation_section = m_queued_hud_animation_section;
    const bool refresh_outfit_hud = m_outfit_hud_refresh_pending;
    const shared_str function_on_stop = m_function_on_stop;

    //
    // Normal physical trash moment:
    // real end of consumable animation.
    //
    if (m_effect_applied && !m_trash_spawned) {
        SpawnTrash();
    }

    DestroyAnimSound();
    StopCameraEffector();
    StopUseParticles();

    if (g_player_hud)
        g_player_hud->detach_controller_item();

    UnlockActor();

    if (m_controller_mode == eControllerModeHudAnimation)
        Msg("* ItemUse HUD animation finished: [%s]", m_hud_section.c_str());
    else if (m_controller_mode == eControllerModeHudAnimationOneShot)
        Msg("* ItemUse one-shot HUD animation finished: [%s]", m_hud_section.c_str());
    else if (m_controller_mode == eControllerModeMutantLoot)
        Msg("* MutantLoot: HUD animation finished for corpse [%u]",
            (u32)m_mutant_loot_target_id);
    else
        Msg("* ItemUse finished: [%s]", m_item_section.c_str());

    Reset();

    if (refresh_outfit_hud) {
        m_outfit_hud_refresh_pending = true;
        ApplyPendingOutfitHudRefresh();
    }

    // The controller is fully detached and unlocked before calling Lua. This
    // lets a story callback safely start the next HUD sequence immediately.
    CallStopFunction(function_on_stop);

    if (start_queued_hud_animation && m_actor && m_actor->g_Alive()) {
        if (StartHudAnimationOnce(queued_hud_animation_section)) {
            Msg("* ItemUse: persistent HUD hide completed, queued one-shot animation started");
        } else {
            Msg("! ItemUse: failed to start queued one-shot HUD animation [%s]",
                queued_hud_animation_section.c_str());
        }

        return;
    }

    if (!start_queued_consumable || !m_actor || !m_actor->g_Alive())
        return;

    CInventoryItem* queued_item =
        m_actor->inventory().get_object_by_id(queued_consumable_id);

    if (!queued_item) {
        Msg("! ItemUse: queued item [%u] is no longer in actor inventory",
            u32(queued_consumable_id));
        return;
    }

    if (Start(queued_item)) {
        Msg("* ItemUse: backpack hide completed, queued animation started");
        return;
    }

    // The config or HUD can disappear between queueing and the end of hide.
    // Preserve usability by falling back to the regular immediate effect.
    bool became_empty = false;

    if (!m_actor->inventory().ApplyEat(queued_item, became_empty))
        Msg("! ItemUse: failed to apply queued item [%u]", u32(queued_consumable_id));
}

void CItemUseController::Reset()
{
    m_item = NULL;

    m_item_section = NULL;
    m_use_section = NULL;
    m_state_section = NULL;
    m_hud_section = NULL;

    m_start_time = 0;
    m_action_time = 0;
    m_animation_duration = 0;

    m_active = false;
    m_effect_applied = false;

    m_controller_mode = eControllerModeNone;
    m_hud_animation_phase = eHudAnimationNone;
    m_hud_animation_hide_requested = false;
    m_hud_animation_allow_inventory = false;
    m_mutant_loot_target_id = u16(-1);
    m_mutant_loot_particle_time = 0;
    m_mutant_loot_particle_enabled = false;
    m_mutant_loot_particle_started = false;
    m_queued_consumable_id = u16(-1);
    m_deferred_hud_animation_section = NULL;
    m_queued_hud_animation_section = NULL;
    m_outfit_hud_refresh_pending = false;
    m_block_movement = false;
    m_function_on_stop = NULL;

    m_waiting_for_weapon_hide = false;
    m_actor_locked = false;
    m_prev_inventory_disabled = false;

    m_trash_section = NULL;
    m_trash_count = 0;
    m_trash_spawned = false;

    m_camera_effector_started = false;

    m_use_particles_name = NULL;
    m_use_particles_bone = NULL;
    m_use_particles_offset.set(0.f, 0.f, 0.f);
    m_use_particles_orientation.set(0.f, 0.f, 0.f);
    m_use_particles_start_time = 0;
    m_use_particles_stop_time = u32(-1);
    m_use_particles = NULL;
    m_use_particles_started = false;
}

void CItemUseController::LoadStopFunction() {
    m_function_on_stop = NULL;

    if (!m_hud_section.size() || !pSettings->section_exist(m_hud_section.c_str()) ||
        !pSettings->line_exist(m_hud_section.c_str(), "function_on_stop")) {
        return;
    }

    LPCSTR function_name =
        pSettings->r_string(m_hud_section.c_str(), "function_on_stop");

    if (function_name && function_name[0] && xr_strcmp(function_name, "none"))
        m_function_on_stop = function_name;
}

void CItemUseController::CallStopFunction(const shared_str& function_name) {
    if (!function_name.size())
        return;

    luabind::functor<void> function;

    if (!ai().script_engine().functor(function_name.c_str(), function) ||
        !function.is_valid()) {
        Msg("! ItemUse: function_on_stop [%s] was not found; callback skipped",
            function_name.c_str());
        return;
    }

    Msg("* ItemUse: calling function_on_stop [%s]", function_name.c_str());

    try {
        function();
    } catch (...) {
        // Script errors are reported by the script engine. Keep the completed
        // controller lifecycle from turning a broken callback into a native
        // crash.
        Msg("! ItemUse: function_on_stop [%s] failed", function_name.c_str());
    }
}

void CItemUseController::LoadAnimSound() {
    DestroyAnimSound();

    if (!pSettings->line_exist(m_use_section, "snd_using_anim"))
        return;

    HUD_SOUND_ITEM::LoadSound(m_use_section.c_str(), "snd_using_anim", m_anim_sound, sg_SourceType);

    m_anim_sound_loaded = true;
}

void CItemUseController::PlayHudAnimationSound(LPCSTR sound_line) {
    DestroyAnimSound();

    if (!sound_line || !sound_line[0] || !m_hud_section.size())
        return;

    if (!pSettings->line_exist(m_hud_section.c_str(), sound_line))
        return;

    HUD_SOUND_ITEM::LoadSound(m_hud_section.c_str(), sound_line, m_anim_sound, sg_SourceType);
    m_anim_sound_loaded = true;

    PlayAnimSound();
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

shared_str CItemUseController::FindConfigSection(LPCSTR line) const {
    if (!line || !line[0])
        return NULL;

    // The most animation-specific section wins. This also lets portion states
    // override a common use section without duplicating the controller setup.
    const shared_str* sections[] = {
        &m_hud_section,
        &m_state_section,
        &m_use_section,
        &m_item_section,
    };

    for (u32 i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        const shared_str& section = *sections[i];

        if (section.size() && pSettings->section_exist(section.c_str()) &&
            pSettings->line_exist(section.c_str(), line)) {
            return section;
        }
    }

    return NULL;
}

void CItemUseController::StartCameraEffector(const shared_str& played_motion_name) {
    StopCameraEffector();

    if (!m_actor)
        return;

    string_path effector_name;
    effector_name[0] = 0;

    const shared_str config_section = FindConfigSection("cam_eff_name");
    const bool explicitly_configured = config_section.size() != 0;

    if (explicitly_configured) {
        LPCSTR configured_name = pSettings->r_string(config_section.c_str(), "cam_eff_name");

        // Explicit "none" also disables the automatic motion-name fallback.
        if (!configured_name || !configured_name[0] || !xr_strcmp(configured_name, "none"))
            return;

        xr_strcpy(effector_name, configured_name);
    } else if (played_motion_name.size()) {
        // Controller HUD items have no CHudItem parent, so the legacy camera
        // lookup in attachable_hud_item::anim_play() cannot run for them.
        // Keep a convenient convention for consumables:
        // $game_anims$\camera_effects\<played HUD motion>.anm
        strconcat(sizeof(effector_name), effector_name, "camera_effects\\",
                  played_motion_name.c_str(), ".anm");
    } else {
        return;
    }

    if (!strext(effector_name))
        xr_strcat(effector_name, ".anm");

    string_path full_path;
    bool effector_exists = !!FS.exist(full_path, "$game_anims$", effector_name);

    // A randomized HUD motion can be named motion1..motion8. If there is no
    // matching camera file, also try the base motion from the anm_show alias.
    if (!effector_exists && !explicitly_configured && m_hud_section.size() &&
        pSettings->line_exist(m_hud_section.c_str(), "anm_show")) {
        string256 base_motion_name;
        _GetItem(pSettings->r_string(m_hud_section.c_str(), "anm_show"), 0,
                 base_motion_name);

        if (base_motion_name[0]) {
            strconcat(sizeof(effector_name), effector_name, "camera_effects\\",
                      base_motion_name, ".anm");
            effector_exists = !!FS.exist(full_path, "$game_anims$", effector_name);
        }
    }

    if (!effector_exists) {
        if (explicitly_configured) {
            Msg("! ItemUse: camera effector [%s] from [%s] was not found", effector_name,
                config_section.c_str());
        }
        return;
    }

    bool cyclic = false;
    bool hud_affect = false;

    if (explicitly_configured) {
        if (pSettings->line_exist(config_section.c_str(), "cam_eff_cyclic"))
            cyclic = !!pSettings->r_bool(config_section.c_str(), "cam_eff_cyclic");

        if (pSettings->line_exist(config_section.c_str(), "cam_eff_hud_affect"))
            hud_affect = !!pSettings->r_bool(config_section.c_str(), "cam_eff_hud_affect");
    }

    CAnimatorCamEffector* effector = xr_new<CAnimatorCamEffector>();
    effector->SetType(eCEItemUse);
    effector->SetCyclic(cyclic);
    effector->SetHudAffect(hud_affect);
    effector->Start(effector_name);

    m_actor->Cameras().AddCamEffector(effector);
    m_camera_effector_started = true;

    Msg("* ItemUse camera effector started: [%s], cyclic [%s], HUD affect [%s]",
        effector_name, cyclic ? "yes" : "no", hud_affect ? "yes" : "no");
}

void CItemUseController::StopCameraEffector() {
    if (!m_camera_effector_started)
        return;

    if (m_actor)
        m_actor->Cameras().RemoveCamEffector(eCEItemUse);

    m_camera_effector_started = false;
}

void CItemUseController::LoadUseParticles() {
    StopUseParticles();

    m_use_particles_name = NULL;
    m_use_particles_bone = NULL;
    m_use_particles_offset.set(0.f, 0.f, 0.f);
    m_use_particles_orientation.set(0.f, 0.f, 0.f);
    m_use_particles_start_time = 0;
    m_use_particles_stop_time = u32(-1);
    m_use_particles_started = false;

    const shared_str config_section = FindConfigSection("use_particles");

    if (!config_section.size())
        return;

    LPCSTR particles_name = pSettings->r_string(config_section.c_str(), "use_particles");

    if (!particles_name || !particles_name[0] || !xr_strcmp(particles_name, "none"))
        return;

    m_use_particles_name = particles_name;

    if (pSettings->line_exist(config_section.c_str(), "use_particles_bone"))
        m_use_particles_bone =
            pSettings->r_string(config_section.c_str(), "use_particles_bone");

    if (pSettings->line_exist(config_section.c_str(), "use_particles_offset"))
        m_use_particles_offset =
            pSettings->r_fvector3(config_section.c_str(), "use_particles_offset");

    if (pSettings->line_exist(config_section.c_str(), "use_particles_orientation"))
        m_use_particles_orientation =
            pSettings->r_fvector3(config_section.c_str(), "use_particles_orientation");

    if (pSettings->line_exist(config_section.c_str(), "use_particles_start_time"))
        m_use_particles_start_time =
            pSettings->r_u32(config_section.c_str(), "use_particles_start_time");

    if (pSettings->line_exist(config_section.c_str(), "use_particles_stop_time"))
        m_use_particles_stop_time =
            pSettings->r_u32(config_section.c_str(), "use_particles_stop_time");

    Msg("* ItemUse particles configured: [%s], bone [%s], start [%u]",
        m_use_particles_name.c_str(),
        m_use_particles_bone.size() ? m_use_particles_bone.c_str() : "item root",
        m_use_particles_start_time);
}

void CItemUseController::StartUseParticles() {
    if (m_use_particles_started || !m_use_particles_name.size())
        return;

    m_use_particles_started = true;

    if (!g_player_hud)
        return;

    Fmatrix transform;
    if (!g_player_hud->controller_item_transform(
            transform, m_use_particles_bone.size() ? m_use_particles_bone.c_str() : NULL,
            m_use_particles_offset, m_use_particles_orientation)) {
        Msg("! ItemUse: particle bone [%s] was not found in HUD [%s]",
            m_use_particles_bone.size() ? m_use_particles_bone.c_str() : "item root",
            m_hud_section.c_str());
        return;
    }

    // FALSE is required for looped effects: the controller owns and destroys
    // the particle together with the item-use animation.
    m_use_particles = CParticlesObject::Create(m_use_particles_name.c_str(), FALSE);
    m_use_particles->UpdateParent(transform, zero_vel);
    m_use_particles->Play(true);

    Msg("* ItemUse particles started: [%s]", m_use_particles_name.c_str());
}

void CItemUseController::UpdateUseParticles() {
    if (!m_use_particles)
        return;

    if (!m_use_particles->IsPlaying()) {
        StopUseParticles();
        return;
    }

    if (!g_player_hud) {
        StopUseParticles();
        return;
    }

    Fmatrix transform;
    if (!g_player_hud->controller_item_transform(
            transform, m_use_particles_bone.size() ? m_use_particles_bone.c_str() : NULL,
            m_use_particles_offset, m_use_particles_orientation)) {
        StopUseParticles();
        return;
    }

    m_use_particles->UpdateParent(transform, zero_vel);
}

void CItemUseController::StopUseParticles() {
    if (!m_use_particles)
        return;

    m_use_particles->Stop(FALSE);
    CParticlesObject::Destroy(m_use_particles);
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
