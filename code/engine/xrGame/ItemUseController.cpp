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
#include "HudItem.h"
#include "player_hud.h"
#include "inventory.h"

#include "CustomDetector.h"
#include "UIGameCustom.h"
#include "ActorEffector.h"
#include "ParticlesObject.h"

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
    m_state_section = NULL;

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

        m_state_section = state;

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
    m_waiting_for_weapon_hide = false;

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
    StopCameraEffector();
    StopUseParticles();

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
    m_state_section = NULL;
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
