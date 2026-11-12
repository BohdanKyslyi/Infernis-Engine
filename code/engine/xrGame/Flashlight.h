#pragma once

#include "CustomDetector.h"

class CParticlesObject;

class CFlashlight : public CCustomDetector {
    typedef CCustomDetector inherited;

public:
    CFlashlight();
    virtual ~CFlashlight();

    virtual void Load(LPCSTR section);
    virtual void UpdateCL();
    virtual void OnStateSwitch(u32 S);
    virtual void OnH_B_Independent(bool just_before_destroy);
    virtual void OnMoveToRuck(const SInvItemPlace& prev);

protected:
    virtual void CreateUI();
    virtual void TurnDetectorInternal(bool b);

private:
    bool CanUseDynamicLights();
    bool HudBoneTransform(Fmatrix& result, const shared_str& bone,
                          const Fvector& offset, const Fvector& orientation);
    void StartParticles();
    void UpdateConeBones(bool visible);
    void UpdateEffects();
    void StopEffects();

private:
    ref_light m_light;
    ref_light m_omni;
    shared_str m_cone_bones;
    Fvector m_omni_offset;
    bool m_legacy_light = false;
    bool m_has_native_delay = false;
    u32 m_show_light_delay = 0;
    CParticlesObject* m_particles;

    shared_str m_light_bone;
    shared_str m_light_texture;
    shared_str m_particles_name;
    shared_str m_particles_bone;

    Fcolor m_light_color;
    Fvector m_light_offset;
    Fvector m_light_orientation;
    Fvector m_particles_offset;
    Fvector m_particles_orientation;

    float m_light_range;
    float m_light_cone_angle;
    u32 m_light_start_time;
    u32 m_light_request_time;

    bool m_light_is_spot;
    bool m_light_shadow;
    bool m_light_hud_mode;
    bool m_effects_requested;
    bool m_effects_active;
    bool m_transform_error_reported;
};
