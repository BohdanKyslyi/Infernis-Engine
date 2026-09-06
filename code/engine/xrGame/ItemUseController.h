#pragma once

#include "HudSound.h"

class CActor;
class CInventoryItem;
class CParticlesObject;

class CItemUseController {
public:
    explicit CItemUseController(CActor* actor);
    ~CItemUseController();

    bool Start(CInventoryItem* item);

    // Persistent controller-owned HUD lifecycle used by interfaces such as
    // PDA and backpack. Connecting those interfaces is intentionally kept
    // outside the controller.
    bool StartHudAnimation(const shared_str& hud_section, bool allow_inventory = false);
    void RequestHudAnimationHide();
    bool IsHudAnimationActive() const;
    bool IsHudAnimationIdle() const;
    bool CanUseConsumables() const;
    bool TryQueueConsumable(CInventoryItem* item);

    void Update(float dt);
    void Cancel();
    void Finish();

    bool IsActive() const { return m_active; }
    bool IsWeaponLocked() const { return m_active; }

private:
    enum EControllerMode {
        eControllerModeNone,
        eControllerModeConsumable,
        eControllerModeHudAnimation,
    };

    enum EHudAnimationPhase {
        eHudAnimationNone,
        eHudAnimationShow,
        eHudAnimationIdle,
        eHudAnimationHide,
    };

    void Reset();

    bool ResolveConsumableAnimation(CInventoryItem* item, shared_str& item_section,
                                    shared_str& use_section, shared_str& state_section,
                                    shared_str& hud_section) const;

    bool CanStartAnimation();
    void BeginAnimation();

    bool PlayHudAnimationMotion(LPCSTR motion_name, EHudAnimationPhase phase, BOOL mix_in);
    void BeginHudAnimationIdle();
    void BeginHudAnimationHide();
    void UpdateHudAnimation();

    void LockActor();
    void UnlockActor();

    void LoadAnimSound();
    void PlayHudAnimationSound(LPCSTR sound_line);
    void PlayAnimSound();
    void StopAnimSound();
    void DestroyAnimSound();

    shared_str FindConfigSection(LPCSTR line) const;

    void StartCameraEffector(const shared_str& played_motion_name);
    void StopCameraEffector();

    void LoadUseParticles();
    void StartUseParticles();
    void UpdateUseParticles();
    void StopUseParticles();

    void SpawnTrash();

private:
    CActor* m_actor;
    CInventoryItem* m_item;

    shared_str m_item_section;
    shared_str m_use_section;
    shared_str m_state_section;
    shared_str m_hud_section;

    u32 m_start_time;
    u32 m_action_time;
    u32 m_animation_duration;

    bool m_active;
    bool m_effect_applied;

    EControllerMode m_controller_mode;
    EHudAnimationPhase m_hud_animation_phase;
    bool m_hud_animation_hide_requested;
    bool m_hud_animation_allow_inventory;
    u16 m_queued_consumable_id;

    //
    // Item use state.
    //
    bool m_waiting_for_weapon_hide;
    bool m_actor_locked;

    //
    // Не хочемо випадково розблокувати inventory,
    // якщо його до нас заблокувала інша система.
    //
    bool m_prev_inventory_disabled;

    //
    // Cached trash recipe of the CURRENT use.
    //
    // Must survive after ApplyEat(), because the source item
    // can already be empty/destroying before animation Finish().
    //
    shared_str m_trash_section;
    u32 m_trash_count;
    bool m_trash_spawned;

    HUD_SOUND_ITEM m_anim_sound;
    bool m_anim_sound_loaded;

    // Optional camera animation played together with the consumable HUD motion.
    bool m_camera_effector_started;

    // Optional HUD particle attached to the temporary item (for example,
    // cigarette smoke). Times are relative to the consumable animation start.
    shared_str m_use_particles_name;
    shared_str m_use_particles_bone;
    Fvector m_use_particles_offset;
    Fvector m_use_particles_orientation;
    u32 m_use_particles_start_time;
    u32 m_use_particles_stop_time;
    CParticlesObject* m_use_particles;
    bool m_use_particles_started;
};
