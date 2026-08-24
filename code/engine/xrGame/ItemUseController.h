#pragma once

#include "HudSound.h"

class CActor;
class CInventoryItem;

class CItemUseController {
public:
    explicit CItemUseController(CActor* actor);
    ~CItemUseController();

    bool Start(CInventoryItem* item);

    void Update(float dt);
    void Cancel();
    void Finish();

    bool IsActive() const { return m_active; }
    bool IsWeaponLocked() const { return m_active; }

private:
    void Reset();

    void LoadAnimSound();
    void PlayAnimSound();
    void StopAnimSound();
    void DestroyAnimSound();

private:
    CActor* m_actor;
    CInventoryItem* m_item;

    shared_str m_item_section;
    shared_str m_use_section;
    shared_str m_hud_section;

    u32 m_start_time;
    u32 m_action_time;
    u32 m_animation_duration;

    bool m_active;
    bool m_effect_applied;

    HUD_SOUND_ITEM m_anim_sound;
    bool m_anim_sound_loaded;
};