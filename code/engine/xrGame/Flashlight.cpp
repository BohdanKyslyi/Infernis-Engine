#include "stdafx.h"
#include "Flashlight.h"

#include "InventoryOwner.h"
#include "level.h"
#include "xrRender/Kinematics.h"

#include <algorithm>
#include <cmath>
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
    m_omni.destroy();
}

namespace {
float ReadTorchComponent(LPCSTR section, LPCSTR prefix, LPCSTR component, float fallback) {
    string64 key;
    xr_sprintf(key, "%s_%s", prefix, component);
    return READ_IF_EXISTS(pSettings, r_float, section, key, fallback);
}

Fvector ReadTorchOffset(LPCSTR section, LPCSTR prefix, const Fvector& fallback) {
    Fvector result;
    result.set(ReadTorchComponent(section, prefix, "x", fallback.x),
        ReadTorchComponent(section, prefix, "y", fallback.y),
        ReadTorchComponent(section, prefix, "z", fallback.z));
    return result;
}

Fcolor ReadTorchColor(LPCSTR section, LPCSTR prefix, const Fcolor& fallback) {
    Fcolor result;
    result.set(ReadTorchComponent(section, prefix, "r", fallback.r),
        ReadTorchComponent(section, prefix, "g", fallback.g),
        ReadTorchComponent(section, prefix, "b", fallback.b),
        ReadTorchComponent(section, prefix, "a", fallback.a));
    return result;
}
}

void CFlashlight::Load(LPCSTR section) {
    inherited::Load(section);

    StopEffects();
    m_omni.destroy();
    m_legacy_light = READ_IF_EXISTS(pSettings, r_bool, section, "torch_installed", false);
    const bool deferred = psDeviceFlags.test(rsR2) || psDeviceFlags.test(rsR3) ||
        psDeviceFlags.test(rsR4);
    const auto native_section = [&](LPCSTR key) {
        return HudSection().size() && !pSettings->line_exist(section, key) ?
            HudSection().c_str() : section;
    };
    m_cone_bones = READ_IF_EXISTS(pSettings, r_string, section, "torch_cone_bones", "");
    m_light_bone = READ_IF_EXISTS(pSettings, r_string, native_section("light_bone"), "light_bone",
        m_legacy_light ? READ_IF_EXISTS(pSettings, r_string, section, "torch_light_bone", "") : "");
    m_light_texture = READ_IF_EXISTS(pSettings, r_string, native_section("light_texture"), "light_texture",
        m_legacy_light ? READ_IF_EXISTS(pSettings, r_string, section, "torch_spot_texture", "") : "");
    m_particles_name = READ_IF_EXISTS(pSettings, r_string, native_section("light_particles"), "light_particles", "");
    m_particles_bone =
        READ_IF_EXISTS(pSettings, r_string, native_section("light_particles_bone"), "light_particles_bone",
                       m_light_bone.c_str());

    m_light_color.set(1.f, 1.f, 1.f, 1.f);
    m_light_offset.set(0.f, 0.f, 0.f);
    m_light_orientation.set(0.f, 0.f, 0.f);
    m_particles_offset.set(0.f, 0.f, 0.f);
    m_particles_orientation.set(0.f, 0.f, 0.f);
    if (m_legacy_light) {
        m_light_color = ReadTorchColor(section, deferred ? "torch_r2_color" : "torch_color",
            m_light_color);
        m_light_offset = ReadTorchOffset(section, "torch_attach_offset", m_light_offset);
    }
    if (pSettings->line_exist(native_section("light_color"), "light_color"))
        m_light_color = pSettings->r_fcolor(native_section("light_color"), "light_color");
    if (pSettings->line_exist(native_section("light_offset"), "light_offset"))
        m_light_offset = pSettings->r_fvector3(native_section("light_offset"), "light_offset");
    if (pSettings->line_exist(native_section("light_orientation"), "light_orientation"))
        m_light_orientation = pSettings->r_fvector3(native_section("light_orientation"), "light_orientation");
    if (pSettings->line_exist(native_section("light_particles_offset"), "light_particles_offset"))
        m_particles_offset = pSettings->r_fvector3(native_section("light_particles_offset"), "light_particles_offset");
    if (pSettings->line_exist(native_section("light_particles_orientation"), "light_particles_orientation"))
        m_particles_orientation =
            pSettings->r_fvector3(native_section("light_particles_orientation"), "light_particles_orientation");

    const float legacy_range = m_legacy_light ?
        READ_IF_EXISTS(pSettings, r_float, section, deferred ? "torch_r2_range" : "torch_range", 12.f)
        : 12.f;
    m_light_range = READ_IF_EXISTS(pSettings, r_float, native_section("light_range"), "light_range",
        READ_IF_EXISTS(pSettings, r_float, native_section("light_radius"), "light_radius", legacy_range));
    m_light_cone_angle = READ_IF_EXISTS(pSettings, r_float, native_section("light_cone_angle"), "light_cone_angle",
        m_legacy_light ? READ_IF_EXISTS(pSettings, r_float, section, "torch_spot_angle", 55.f) : 55.f);
    m_has_native_delay = pSettings->line_exist(native_section("light_start_time"), "light_start_time");
    m_light_start_time = READ_IF_EXISTS(pSettings, r_u32, native_section("light_start_time"), "light_start_time", 0);
    m_light_shadow = READ_IF_EXISTS(pSettings, r_bool, native_section("light_shadow"), "light_shadow",
        !m_legacy_light || READ_IF_EXISTS(pSettings, r_bool, section, "torch_render_shadow", true));
    m_light_hud_mode = READ_IF_EXISTS(pSettings, r_bool, native_section("light_hud_mode"), "light_hud_mode", true);

    const LPCSTR light_type = READ_IF_EXISTS(pSettings, r_string, native_section("light_type"), "light_type", nullptr);
    m_light_is_spot = light_type ? _stricmp(light_type, "point") != 0 :
        !m_legacy_light || READ_IF_EXISTS(pSettings, r_s32, section, "torch_render_type",
            int(IRender_Light::SPOT)) != IRender_Light::POINT;

    clamp(m_light_range, EPS_S, 1000.f);
    clamp(m_light_cone_angle, 1.f, 120.f);

    m_light->set_type(m_light_is_spot ? IRender_Light::SPOT : IRender_Light::POINT);
    m_light->set_shadow(m_light_shadow);
    m_light->set_hud_mode(m_light_hud_mode);
    m_light->set_color(m_light_color);
    m_light->set_range(m_light_range);
    if (m_light_is_spot) {
        m_light->set_cone(deg2rad(m_light_cone_angle));
        if (m_light_texture.size() && xr_strcmp(m_light_texture.c_str(), "none") != 0)
            m_light->set_texture(m_light_texture.c_str());
    }

    if (m_legacy_light) {
        m_omni = ::Render->light_create();
        const bool spot = READ_IF_EXISTS(pSettings, r_s32, section, "torch_omni_type",
            int(IRender_Light::POINT)) == IRender_Light::SPOT;
        m_omni->set_type(spot ? IRender_Light::SPOT : IRender_Light::POINT);
        if (spot)
            m_omni->set_cone(deg2rad(m_light_cone_angle));
        m_omni->set_shadow(READ_IF_EXISTS(pSettings, r_bool, section, "torch_omni_shadow", false));
        m_omni->set_hud_mode(m_light_hud_mode);
        Fcolor fill;
        fill.set(1.f, 1.f, 1.f, 0.1f);
        m_omni->set_color(ReadTorchColor(section,
            deferred ? "torch_r2_omni_color" : "torch_omni_color", fill));
        m_omni->set_range(clampr(READ_IF_EXISTS(pSettings, r_float, section,
            deferred ? "torch_r2_omni_range" : "torch_omni_range", 0.25f), EPS_S, 1000.f));
        m_omni_offset = ReadTorchOffset(section, "torch_omni_attach_offset", m_light_offset);
    }
}

void CFlashlight::UpdateCL() {
    inherited::UpdateCL();

    if (m_effects_requested && H_Parent() && H_Parent() == Level().CurrentViewEntity() &&
        GetHUDmode()) {
        UpdateEffects();
    } else if (m_effects_active) {
        UpdateConeBones(false);
        StopEffects();
    }
}

void CFlashlight::OnStateSwitch(u32 S) {
    if (S == eShowing) {
        m_effects_requested = true;
        m_light_request_time = Device.dwTimeGlobal;
        m_transform_error_reported = false;
    } else if (S == eHiding || S == eHidden) {
        m_effects_requested = false;
        UpdateConeBones(false);
        StopEffects();
    }

    inherited::OnStateSwitch(S);

    if (S == eShowing) {
        m_show_light_delay = m_light_start_time;
        if (m_legacy_light && !m_has_native_delay && m_current_motion.size()) {
            string128 key;
            xr_sprintf(key, "torch_enable_time_%s", m_current_motion.c_str());
            const float seconds = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, 0.f);
            if (std::isfinite(seconds)) {
                const u32 delay = u32(clampr(std::abs(seconds), 0.f, 60.f) * 1000.f + 0.5f);
                const u32 duration = m_dwMotionEndTm - m_dwMotionStartTm;
                m_show_light_delay = seconds < 0.f ? duration - std::min(duration, delay) : delay;
            }
        }
        UpdateConeBones(false);
    }
}

void CFlashlight::OnH_B_Independent(bool just_before_destroy) {
    m_effects_requested = false;
    StopEffects();
    inherited::OnH_B_Independent(just_before_destroy);
}

void CFlashlight::OnMoveToRuck(const SInvItemPlace& prev) {
    m_effects_requested = false;
    UpdateConeBones(false);
    StopEffects();
    inherited::OnMoveToRuck(prev);
}

void CFlashlight::CreateUI() {}

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

    item->update(false);

    Fmatrix base_transform = item->m_item_transform;
    if (bone.size()) {
        u16 bone_id = item->m_model->LL_BoneID(bone.c_str());
        if (bone_id == BI_NONE) {
            if (!m_transform_error_reported) {
                Msg("! Flashlight [%s]: bone [%s] missing in HUD [%s]; using fire bone or HUD origin",
                    cNameSect().c_str(), bone.c_str(), item->m_sect_name.c_str());
                m_transform_error_reported = true;
            }
            if (item->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
                bone_id = item->m_measures.m_fire_bone;
        }

        if (bone_id != BI_NONE)
            base_transform.mul_43(item->m_item_transform, item->m_model->LL_GetTransform(bone_id));
    }

    Fvector position;
    base_transform.transform_tiny(position, offset);

    Fvector angles = orientation;
    angles.mul(PI / 180.f);

    Fmatrix rotation;
    rotation.setHPB(angles.x, angles.y, angles.z);

    result.mul_43(m_legacy_light ? item->m_item_transform : base_transform, rotation);
    result.c.set(position);
    return true;
}

void CFlashlight::StartParticles() {
    if (!m_particles_name.size() || xr_strcmp(m_particles_name.c_str(), "none") == 0)
        return;

    Fmatrix transform;
    if (!HudBoneTransform(transform, m_particles_bone, m_particles_offset, m_particles_orientation))
        return;

    m_particles = CParticlesObject::Create(m_particles_name.c_str(), FALSE);
    m_particles->UpdateParent(transform, zero_vel);
    m_particles->Play(true);
}

void CFlashlight::UpdateConeBones(bool visible) {
    auto* hud = g_player_hud && m_cone_bones.size() ? HudItemData() : nullptr;
    if (!hud)
        return;

    string128 bone;
    const LPCSTR bones = m_cone_bones.c_str();
    for (int i = 0, count = _GetItemCount(bones); i < count; ++i) {
        _GetItem(bones, i, bone);
        if (bone[0])
            hud->set_bone_visible(bone, visible, TRUE);
    }
}

void CFlashlight::UpdateEffects() {
    if (Device.dwTimeGlobal - m_light_request_time < m_show_light_delay)
        return;

    Fmatrix transform;
    if (!HudBoneTransform(transform, m_light_bone, m_light_offset, m_light_orientation)) {
        StopEffects();
        return;
    }

    m_light->set_position(transform.c);
    m_light->set_rotation(transform.k, transform.i);
    if (m_omni) {
        Fmatrix fill;
        if (HudBoneTransform(fill, m_light_bone, m_omni_offset, m_light_orientation)) {
            m_omni->set_position(fill.c);
            m_omni->set_rotation(fill.k, fill.i);
        }
    }

    const bool lighting_allowed = CanUseDynamicLights();
    m_light->set_active(lighting_allowed);
    if (m_omni)
        m_omni->set_active(lighting_allowed);
    UpdateConeBones(lighting_allowed);

    if (!m_effects_active) {
        m_effects_active = true;
        StartParticles();
    } else if (m_particles) {
        Fmatrix particle_transform;
        if (m_particles->IsPlaying() &&
            HudBoneTransform(particle_transform, m_particles_bone,
                m_particles_offset, m_particles_orientation)) {
            m_particles->UpdateParent(particle_transform, zero_vel);
        } else {
            CParticlesObject::Destroy(m_particles);
        }
    }
}

void CFlashlight::StopEffects() {
    if (m_light)
        m_light->set_active(false);
    if (m_omni)
        m_omni->set_active(false);

    if (m_particles) {
        m_particles->Stop(FALSE);
        CParticlesObject::Destroy(m_particles);
    }

    m_effects_active = false;
}
