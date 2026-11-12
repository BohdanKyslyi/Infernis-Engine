#pragma once

#include "CustomDetector.h"

class CParticlesObject;

class CFlashlight : public CCustomDetector {
    typedef CCustomDetector inherited;

public:
    explicit CFlashlight(bool native_flashlight = true);
    ~CFlashlight() override;

    void Load(LPCSTR section) override;
    void net_Destroy() override;
    void UpdateCL() override;
    void OnStateSwitch(u32 state) override;
    void OnH_B_Independent(bool just_before_destroy) override;
    void OnMoveToRuck(const SInvItemPlace& prev) override;
    void on_b_hud_detach() override;

protected:
    bool HasHandheldLight() const { return !!m_light; }
    bool AllowsZoomWithDetector() const override { return HasHandheldLight(); }
    void TurnDetectorInternal(bool working) override;

private:
    u32 MotionLightDelay(LPCSTR prefix, u32 fallback) const;
    void UpdateConeBones(bool visible);
    void UpdateEffects(bool lighting_allowed);
    void UpdateParticles(attachable_hud_item* hud);
    void StopEffects();
    void StopParticles();

    ref_light m_light;
    ref_light m_omni;
    ref_glow m_glow;
    CParticlesObject* m_particles = nullptr;

    shared_str m_light_bone;
    shared_str m_direction_bone;
    shared_str m_cone_bones;
    shared_str m_particles_name;
    shared_str m_particles_bone;
    Fvector m_light_offset;
    Fvector m_light_orientation;
    Fvector m_omni_offset;
    Fvector m_aim_offset;
    Fvector m_world_offset;
    Fvector m_omni_world_offset;
    Fvector m_particles_offset;
    Fvector m_particles_orientation;

    u32 m_light_start_time = 0;
    u32 m_transition_time = 0;
    u32 m_transition_delay = 0;

    const bool m_native_flashlight;
    bool m_legacy_light = false;
    bool m_legacy_direction = false;
    bool m_has_native_delay = false;
    bool m_light_hud_mode = false;
    bool m_effects_active = false;
};
