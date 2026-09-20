#include "stdafx.h"
#include "Flashlight.h"

#include "InventoryOwner.h"
#include "ParticlesObject.h"
#include "player_hud.h"

CFlashlight::CFlashlight()
    : m_particles(NULL), m_light_range(12.f), m_light_cone_angle(55.f),
      m_light_start_time(0), m_light_request_time(0), m_light_is_spot(true),
      m_light_shadow(true), m_light_hud_mode(true), m_effects_requested(false),
      m_effects_active(false), m_transform_error_reported(false) {
    m_light = ::Render->light_create();
    m_light->set_active(false);

    m_light_color.set(1.f, 1.f, 1.f, 1.f);
    m_light_offset.set(0.f, 0.f, 0.f);
    m_light_orientation.set(0.f, 0.f, 0.f);
    m_particles_offset.set(0.f, 0.f, 0.f);
    m_particles_orientation.set(0.f, 0.f, 0.f);
}

CFlashlight::~CFlashlight() {
    StopEffects();
    m_light.destroy();
}

void CFlashlight::Load(LPCSTR section) {
    inherited::Load(section);

    m_light_bone = READ_IF_EXISTS(pSettings, r_string, section, "light_bone", "");
    m_light_texture = READ_IF_EXISTS(pSettings, r_string, section, "light_texture", "");
    m_particles_name = READ_IF_EXISTS(pSettings, r_string, section, "light_particles", "");
    m_particles_bone =
        READ_IF_EXISTS(pSettings, r_string, section, "light_particles_bone",
                       m_light_bone.c_str());

    if (pSettings->line_exist(section, "light_color"))
        m_light_color = pSettings->r_fcolor(section, "light_color");
    if (pSettings->line_exist(section, "light_offset"))
        m_light_offset = pSettings->r_fvector3(section, "light_offset");
    if (pSettings->line_exist(section, "light_orientation"))
        m_light_orientation = pSettings->r_fvector3(section, "light_orientation");
    if (pSettings->line_exist(section, "light_particles_offset"))
        m_particles_offset = pSettings->r_fvector3(section, "light_particles_offset");
    if (pSettings->line_exist(section, "light_particles_orientation"))
        m_particles_orientation =
            pSettings->r_fvector3(section, "light_particles_orientation");

    m_light_range = pSettings->line_exist(section, "light_radius")
        ? pSettings->r_float(section, "light_radius")
        : READ_IF_EXISTS(pSettings, r_float, section, "light_range", 12.f);
    m_light_cone_angle =
        READ_IF_EXISTS(pSettings, r_float, section, "light_cone_angle", 55.f);
    m_light_start_time =
        READ_IF_EXISTS(pSettings, r_u32, section, "light_start_time", 0);
    m_light_shadow = READ_IF_EXISTS(pSettings, r_bool, section, "light_shadow", true);
    m_light_hud_mode = READ_IF_EXISTS(pSettings, r_bool, section, "light_hud_mode", true);

    LPCSTR light_type =
        READ_IF_EXISTS(pSettings, r_string, section, "light_type", "spot");
    m_light_is_spot = _stricmp(light_type, "point") != 0;

    clamp(m_light_range, EPS_S, 1000.f);
    clamp(m_light_cone_angle, 1.f, 179.f);

    m_light->set_type(m_light_is_spot ? IRender_Light::SPOT : IRender_Light::POINT);
    m_light->set_shadow(m_light_shadow);
    m_light->set_hud_mode(m_light_hud_mode);
    m_light->set_color(m_light_color);
    m_light->set_range(m_light_range);
    if (m_light_is_spot) {
        m_light->set_cone(deg2rad(m_light_cone_angle));
        if (m_light_texture.size())
            m_light->set_texture(m_light_texture.c_str());
    }
}

void CFlashlight::UpdateCL() {
    inherited::UpdateCL();

    if (!IsWorking() || !HudItemData())
        StopEffects();
}

void CFlashlight::OnStateSwitch(u32 S) {
    if (S == eShowing) {
        m_effects_requested = true;
        m_light_request_time = Device.dwTimeGlobal;
        m_transform_error_reported = false;
    } else if (S == eHiding || S == eHidden) {
        m_effects_requested = false;
        StopEffects();
    }

    inherited::OnStateSwitch(S);
}

void CFlashlight::OnH_B_Independent(bool just_before_destroy) {
    m_effects_requested = false;
    StopEffects();
    inherited::OnH_B_Independent(just_before_destroy);
}

void CFlashlight::OnMoveToRuck(const SInvItemPlace& prev) {
    m_effects_requested = false;
    StopEffects();
    inherited::OnMoveToRuck(prev);
}

void CFlashlight::CreateUI() {}

void CFlashlight::UpfateWork() { UpdateEffects(); }

void CFlashlight::TurnDetectorInternal(bool b) {
    inherited::TurnDetectorInternal(b);

    if (!b) {
        m_effects_requested = false;
        StopEffects();
    }
}

bool CFlashlight::CanUseDynamicLights() {
    if (!H_Parent())
        return true;

    CInventoryOwner* owner = smart_cast<CInventoryOwner*>(H_Parent());
    return !owner || owner->can_use_dynamic_lights();
}

bool CFlashlight::HudBoneTransform(Fmatrix& result, const shared_str& bone,
                                   const Fvector& offset,
                                   const Fvector& orientation) {
    attachable_hud_item* item = HudItemData();
    if (!item)
        return false;

    item->update(true);

    Fmatrix base_transform;
    base_transform.set(item->m_item_transform);

    if (bone.size()) {
        const u16 bone_id = item->m_model->LL_BoneID(bone.c_str());
        if (bone_id == BI_NONE)
            return false;

        base_transform.mul_43(item->m_item_transform,
                              item->m_model->LL_GetTransform(bone_id));
    }

    Fvector position;
    base_transform.transform_tiny(position, offset);

    Fvector angles = orientation;
    angles.mul(PI / 180.f);

    Fmatrix rotation;
    rotation.setHPB(angles.x, angles.y, angles.z);

    result.mul_43(base_transform, rotation);
    result.c.set(position);
    return true;
}

void CFlashlight::StartEffects() {
    if (m_effects_active)
        return;

    Fmatrix light_transform;
    if (!HudBoneTransform(light_transform, m_light_bone, m_light_offset,
                          m_light_orientation)) {
        if (!m_transform_error_reported) {
            Msg("! Flashlight [%s]: light bone [%s] was not found in HUD [%s]",
                cNameSect().c_str(), m_light_bone.size() ? m_light_bone.c_str() : "root",
                HudItemData() ? HudItemData()->m_sect_name.c_str() : "none");
            m_transform_error_reported = true;
        }
        return;
    }

    m_light->set_position(light_transform.c);
    m_light->set_rotation(light_transform.k, light_transform.i);
    if (CanUseDynamicLights())
        m_light->set_active(true);

    if (m_particles_name.size() && xr_strcmp(m_particles_name.c_str(), "none")) {
        Fmatrix particle_transform;
        if (HudBoneTransform(particle_transform, m_particles_bone, m_particles_offset,
                             m_particles_orientation)) {
            m_particles = CParticlesObject::Create(m_particles_name.c_str(), FALSE);
            m_particles->UpdateParent(particle_transform, zero_vel);
            m_particles->Play(true);
        } else {
            Msg("! Flashlight [%s]: particle bone [%s] was not found in HUD [%s]",
                cNameSect().c_str(),
                m_particles_bone.size() ? m_particles_bone.c_str() : "root",
                HudItemData() ? HudItemData()->m_sect_name.c_str() : "none");
        }
    }

    m_effects_active = true;
}

void CFlashlight::UpdateEffects() {
    if (!m_effects_requested)
        return;

    if (!m_effects_active) {
        if (Device.dwTimeGlobal - m_light_request_time < m_light_start_time)
            return;
        StartEffects();
    }

    if (!m_effects_active)
        return;

    Fmatrix light_transform;
    if (!HudBoneTransform(light_transform, m_light_bone, m_light_offset,
                          m_light_orientation)) {
        StopEffects();
        return;
    }

    m_light->set_position(light_transform.c);
    m_light->set_rotation(light_transform.k, light_transform.i);

    if (m_particles) {
        if (!m_particles->IsPlaying()) {
            CParticlesObject::Destroy(m_particles);
        } else {
            Fmatrix particle_transform;
            if (HudBoneTransform(particle_transform, m_particles_bone,
                                 m_particles_offset, m_particles_orientation))
                m_particles->UpdateParent(particle_transform, zero_vel);
            else
                CParticlesObject::Destroy(m_particles);
        }
    }
}

void CFlashlight::StopEffects() {
    if (m_light)
        m_light->set_active(false);

    if (m_particles) {
        m_particles->Stop(FALSE);
        CParticlesObject::Destroy(m_particles);
    }

    m_effects_active = false;
}
