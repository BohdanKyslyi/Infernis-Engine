#include "stdafx.h"
#include "Flashlight.h"

#include "Actor.h"
#include "Inventory.h"
#include "ParticlesObject.h"
#include "Weapon.h"
#include "hudmanager.h"
#include "level.h"
#include "player_hud.h"
#include "xrRender/Kinematics.h"

#include <algorithm>
#include <cmath>

extern ENGINE_API float psHUD_FOV;

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

Fmatrix BoneTransform(const Fmatrix& item, IKinematics* model, const shared_str& bone,
                      u16 fallback = BI_NONE) {
    u16 id = model && bone.size() ? model->LL_BoneID(bone.c_str()) : BI_NONE;
    if (id == BI_NONE)
        id = fallback;
    Fmatrix transform = item;
    if (model && id != BI_NONE)
        transform.mul_43(item, model->LL_GetTransform(id));
    return transform;
}

Fmatrix LightRotation(const Fmatrix& mount, const Fvector& orientation) {
    Fmatrix rotation, transform;
    rotation.setHPB(deg2rad(orientation.x), deg2rad(orientation.y), deg2rad(orientation.z));
    transform.mul_43(mount, rotation);
    return transform;
}

// Preserve the HUD emitter's screen position under the world projection.
void ProjectHudLightToWorld(Fvector& position, Fvector& omni_position, Fvector& direction) {
    const float half_fov = deg2rad(Device.fFOV * 0.5f);
    const float scale = std::tan(half_fov * std::max(psHUD_FOV, 0.1f)) /
        std::max(std::tan(half_fov), EPS_S);
    const Fvector& forward = Device.vCameraDirection;
    const auto project = [&](Fvector& v) {
        const float depth = v.dotproduct(forward);
        v.mad(forward, -depth).mul(scale).mad(forward, depth);
    };

    position.sub(Device.vCameraPosition);
    project(position);
    position.add(Device.vCameraPosition);

    omni_position.sub(Device.vCameraPosition);
    project(omni_position);
    omni_position.add(Device.vCameraPosition);

    project(direction);
}
}

CFlashlight::CFlashlight(bool native_flashlight) : m_native_flashlight(native_flashlight) {}

CFlashlight::~CFlashlight() { StopEffects(); }

void CFlashlight::Load(LPCSTR section) {
    inherited::Load(section);

    StopEffects();
    m_light.destroy();
    m_omni.destroy();
    m_glow.destroy();
    m_legacy_light = READ_IF_EXISTS(pSettings, r_bool, section, "torch_installed", false);
    if (!m_native_flashlight && !m_legacy_light)
        return;

    m_fAfDetectRadius = 0.f;
    const bool deferred = psDeviceFlags.test(rsR2) || psDeviceFlags.test(rsR3) ||
        psDeviceFlags.test(rsR4);
    const auto native_section = [&](LPCSTR key) {
        return !pSettings->line_exist(section, key) && HudSection().size() &&
            pSettings->section_exist(HudSection().c_str()) ? HudSection().c_str() : section;
    };
    const LPCSTR legacy_bone = m_legacy_light ?
        READ_IF_EXISTS(pSettings, r_string, section, "torch_light_bone", "") : "";
    m_light_bone = READ_IF_EXISTS(pSettings, r_string, native_section("light_bone"),
        "light_bone", legacy_bone);
    m_direction_bone = nullptr;
    if (m_legacy_light && READ_IF_EXISTS(pSettings, r_bool, section, "light_directions_by_bones", false)) {
        m_direction_bone = READ_IF_EXISTS(pSettings, r_string, section, "light_dir_bone",
            READ_IF_EXISTS(pSettings, r_string, section, "torch_dir_bone", ""));
    }
    m_cone_bones = m_legacy_light ? READ_IF_EXISTS(pSettings, r_string, section, "torch_cone_bones", "") : "";
    const LPCSTR legacy_texture = m_legacy_light ?
        READ_IF_EXISTS(pSettings, r_string, section, "torch_spot_texture", "") : "";
    const LPCSTR texture = READ_IF_EXISTS(pSettings, r_string, native_section("light_texture"),
        "light_texture", legacy_texture);

    Fvector zero;
    zero.set(0.f, 0.f, 0.f);
    const Fvector legacy_offset = m_legacy_light ?
        ReadTorchOffset(section, "torch_attach_offset", zero) : zero;
    m_light_offset = READ_IF_EXISTS(pSettings, r_fvector3, native_section("light_offset"),
        "light_offset", legacy_offset);
    m_light_orientation = READ_IF_EXISTS(pSettings, r_fvector3, native_section("light_orientation"),
        "light_orientation", zero);
    m_legacy_direction = m_legacy_light &&
        !pSettings->line_exist(native_section("light_orientation"), "light_orientation");

    m_particles_name = READ_IF_EXISTS(pSettings, r_string, native_section("light_particles"),
        "light_particles", "");
    m_particles_bone = READ_IF_EXISTS(pSettings, r_string, native_section("light_particles_bone"),
        "light_particles_bone", m_light_bone.c_str());
    m_particles_offset = READ_IF_EXISTS(pSettings, r_fvector3, native_section("light_particles_offset"),
        "light_particles_offset", zero);
    m_particles_orientation = READ_IF_EXISTS(pSettings, r_fvector3,
        native_section("light_particles_orientation"), "light_particles_orientation", zero);

    Fcolor color;
    color.set(1.f, 1.f, 1.f, 1.f);
    if (m_legacy_light) {
        if (deferred)
            color.set(0.6f, 0.55f, 0.55f, 0.8f);
        else
            color.a = 0.2f;
        color = ReadTorchColor(section, deferred ? "torch_r2_color" : "torch_color", color);
    }
    color = READ_IF_EXISTS(pSettings, r_fcolor, native_section("light_color"), "light_color", color);

    const float legacy_range = m_legacy_light ?
        READ_IF_EXISTS(pSettings, r_float, section, deferred ? "torch_r2_range" : "torch_range", 15.f)
        : 12.f;
    const float range = READ_IF_EXISTS(pSettings, r_float, native_section("light_range"), "light_range",
        READ_IF_EXISTS(pSettings, r_float, native_section("light_radius"), "light_radius", legacy_range));
    const float angle = READ_IF_EXISTS(pSettings, r_float, native_section("light_cone_angle"),
        "light_cone_angle", m_legacy_light ?
        READ_IF_EXISTS(pSettings, r_float, section, "torch_spot_angle", 75.f) : 55.f);
    const bool shadow = READ_IF_EXISTS(pSettings, r_bool, native_section("light_shadow"),
        "light_shadow", !m_legacy_light ||
        READ_IF_EXISTS(pSettings, r_bool, section, "torch_render_shadow", true));
    m_light_hud_mode = READ_IF_EXISTS(pSettings, r_bool, native_section("light_hud_mode"),
        "light_hud_mode", !m_legacy_light);
    m_has_native_delay = pSettings->line_exist(native_section("light_start_time"), "light_start_time");
    m_light_start_time = READ_IF_EXISTS(pSettings, r_u32, native_section("light_start_time"),
        "light_start_time", 0);

    const LPCSTR type = READ_IF_EXISTS(pSettings, r_string, native_section("light_type"), "light_type", nullptr);
    const bool spot = type ? _stricmp(type, "point") != 0 :
        !m_legacy_light || READ_IF_EXISTS(pSettings, r_s32, section, "torch_render_type",
            int(IRender_Light::SPOT)) != IRender_Light::POINT;
    const float cone = deg2rad(clampr(angle, 1.f, 120.f));
    const auto create_light = [&](ref_light& light, bool spot, bool shadow, float range, const Fcolor& color) {
        light = ::Render->light_create();
        light->set_type(spot ? IRender_Light::SPOT : IRender_Light::POINT);
        light->set_shadow(shadow);
        light->set_color(color);
        light->set_range(clampr(range, EPS_S, 1000.f));
        if (spot)
            light->set_cone(cone);
    };
    create_light(m_light, spot, shadow, range, color);
    if (spot && texture && texture[0] && _stricmp(texture, "none") != 0)
        m_light->set_texture(texture);

    if (!m_legacy_light)
        return;

    Fcolor fill;
    fill.set(1.f, 1.f, 1.f, 0.1f);
    fill = ReadTorchColor(section, deferred ? "torch_r2_omni_color" : "torch_omni_color", fill);
    create_light(m_omni, READ_IF_EXISTS(pSettings, r_s32, section, "torch_omni_type",
        int(IRender_Light::POINT)) == IRender_Light::SPOT,
        READ_IF_EXISTS(pSettings, r_bool, section, "torch_omni_shadow", false),
        READ_IF_EXISTS(pSettings, r_float, section, deferred ? "torch_r2_omni_range" : "torch_omni_range",
            deferred ? 0.75f : 1.5f), fill);
    m_omni_offset = ReadTorchOffset(section, "torch_omni_attach_offset", m_light_offset);
    m_aim_offset = ReadTorchOffset(section, "torch_aim_attach_offset", zero);
    m_world_offset = ReadTorchOffset(section, "torch_world_attach_offset", zero);
    m_omni_world_offset = ReadTorchOffset(section, "torch_omni_world_attach_offset", m_world_offset);

    const LPCSTR glow_texture = READ_IF_EXISTS(pSettings, r_string, section, "torch_glow_texture", nullptr);
    if (READ_IF_EXISTS(pSettings, r_bool, section, "create_glow", false) &&
        glow_texture && glow_texture[0] && _stricmp(glow_texture, "none") != 0) {
        m_glow = ::Render->glow_create();
        m_glow->set_texture(glow_texture);
        m_glow->set_color(color);
        m_glow->set_radius(READ_IF_EXISTS(pSettings, r_float, section, "torch_glow_radius", 0.3f));
    }
}

u32 CFlashlight::MotionLightDelay(LPCSTR prefix, u32 fallback) const {
    if (!m_legacy_light || !m_current_motion.size())
        return fallback;

    string128 key;
    xr_sprintf(key, "%s%s", prefix, m_current_motion.c_str());
    const float seconds = READ_IF_EXISTS(pSettings, r_float, HudSection().c_str(), key, 0.f);
    if (!std::isfinite(seconds))
        return fallback;

    const u32 delay = u32(clampr(std::abs(seconds), 0.f, 60.f) * 1000.f + 0.5f);
    if (seconds >= 0.f)
        return delay;

    const u32 duration = m_bStopAtEndAnimIsRunning ? m_dwMotionEndTm - m_dwMotionStartTm : 0;
    return duration - std::min(duration, delay);
}

void CFlashlight::OnStateSwitch(u32 state) {
    inherited::OnStateSwitch(state);
    if (!m_light)
        return;

    if (state == eShowing) {
        m_transition_time = Device.dwTimeGlobal;
        m_transition_delay = m_has_native_delay ? m_light_start_time :
            MotionLightDelay("torch_enable_time_", m_light_start_time);
        StopEffects();
        UpdateConeBones(false);
    } else if (state == eHiding) {
        m_transition_time = Device.dwTimeGlobal;
        m_transition_delay = MotionLightDelay("torch_disable_time_", 0);
        if (!m_transition_delay)
            StopEffects();
    } else if (state == eHidden) {
        StopEffects();
    }
}

void CFlashlight::UpdateCL() {
    inherited::UpdateCL();
    if (!m_light)
        return;

    auto* actor = smart_cast<CActor*>(H_Parent());
    const u32 elapsed = Device.dwTimeGlobal - m_transition_time;
    const u32 state = GetState();
    const bool should_light =
        (state == eShowing && elapsed >= m_transition_delay) ||
        (state == eIdle && (!m_has_native_delay || elapsed >= m_transition_delay)) ||
        (state == eHiding && m_effects_active && elapsed < m_transition_delay);
    if (!actor || actor != Level().CurrentViewEntity() || !actor->g_Alive() || !IsWorking() ||
        !should_light || (actor->HUDview() && (!GetHUDmode() ||
            !psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2)))) {
        StopEffects();
        return;
    }

    UpdateEffects(actor->can_use_dynamic_lights());
}

void CFlashlight::net_Destroy() {
    StopEffects();
    inherited::net_Destroy();
}

void CFlashlight::OnH_B_Independent(bool just_before_destroy) {
    StopEffects();
    inherited::OnH_B_Independent(just_before_destroy);
}

void CFlashlight::OnMoveToRuck(const SInvItemPlace& prev) {
    StopEffects();
    inherited::OnMoveToRuck(prev);
}

void CFlashlight::on_b_hud_detach() {
    StopEffects();
    inherited::on_b_hud_detach();
}

void CFlashlight::TurnDetectorInternal(bool working) {
    inherited::TurnDetectorInternal(working);
    if (!working)
        StopEffects();
}

void CFlashlight::UpdateConeBones(bool visible) {
    if (!m_cone_bones.size())
        return;

    auto* hud = g_player_hud ? HudItemData() : nullptr;
    auto* world = smart_cast<IKinematics*>(Visual());
    string128 bone;
    const LPCSTR bones = m_cone_bones.c_str();
    for (int i = 0, count = _GetItemCount(bones); i < count; ++i) {
        _GetItem(bones, i, bone);
        if (!bone[0])
            continue;
        if (hud)
            hud->set_bone_visible(bone, visible, TRUE);
        if (world) {
            const u16 id = world->LL_BoneID(bone);
            if (id != BI_NONE && !!world->LL_GetBoneVisible(id) != visible)
                world->LL_SetBoneVisible(id, visible, TRUE);
        }
    }
}

void CFlashlight::UpdateEffects(bool lighting_allowed) {
    auto* hud = GetHUDmode() ? HudItemData() : nullptr;
    if (!hud && !m_legacy_light) {
        StopEffects();
        return;
    }

    IKinematics* model;
    Fmatrix item;
    Fvector offset, omni_offset;
    u16 fallback = BI_NONE;
    if (hud) {
        hud->update(false);
        model = hud->m_model;
        item = hud->m_item_transform;
        if (hud->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
            fallback = hud->m_measures.m_fire_bone;

        offset = m_light_offset;
        omni_offset = m_omni ? m_omni_offset : offset;
        if (m_legacy_light && m_pInventory) {
            const auto* weapon = smart_cast<CWeapon*>(m_pInventory->ActiveItem());
            if (weapon) {
                const float aim = weapon->GetZoomRotationFactor();
                offset.mad(m_aim_offset, aim);
                omni_offset.mad(m_aim_offset, aim);
            }
        }
    } else {
        UpdateXForm();
        item = XFORM();
        model = smart_cast<IKinematics*>(Visual());
        if (model)
            model->CalculateBones();
        offset = m_world_offset;
        omni_offset = m_omni_world_offset;
    }

    const Fmatrix mount = BoneTransform(item, model, m_light_bone, fallback);
    Fvector position, omni_position;
    mount.transform_tiny(position, offset);
    mount.transform_tiny(omni_position, omni_offset);

    const bool legacy_axes = m_legacy_direction && (hud || m_direction_bone.size());
    Fvector direction = LightRotation(legacy_axes ? item : mount, m_light_orientation).k;
    const u16 target = legacy_axes && model && m_direction_bone.size() ?
        model->LL_BoneID(m_direction_bone.c_str()) : BI_NONE;
    if (target != BI_NONE) {
        Fvector target_position, target_direction;
        item.transform_tiny(target_position, model->LL_GetTransform(target).c);
        target_direction.sub(target_position, position);
        if (target_direction.square_magnitude() > EPS_S)
            direction = target_direction;
    }
    if (hud)
        ProjectHudLightToWorld(position, omni_position, direction);
    direction.normalize_safe();
    Fvector up, right;
    Fvector::generate_orthonormal_basis_normalized(direction, up, right);

    m_light->set_hud_mode(hud && m_light_hud_mode);
    m_light->set_position(position);
    m_light->set_rotation(direction, right);
    m_light->set_active(lighting_allowed);
    if (m_omni) {
        m_omni->set_hud_mode(hud && m_light_hud_mode);
        m_omni->set_position(omni_position);
        m_omni->set_rotation(direction, right);
        m_omni->set_active(lighting_allowed);
    }
    if (m_glow) {
        m_glow->set_position(position);
        m_glow->set_direction(direction);
        m_glow->set_active(lighting_allowed);
    }
    UpdateConeBones(lighting_allowed);
    m_effects_active = true;
    UpdateParticles(hud);
}

void CFlashlight::UpdateParticles(attachable_hud_item* hud) {
    if (!hud) {
        StopParticles();
        return;
    }
    if (!m_particles_name.size() || _stricmp(m_particles_name.c_str(), "none") == 0)
        return;

    const u16 fallback = hud->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point) ?
        hud->m_measures.m_fire_bone : BI_NONE;
    const Fmatrix mount = BoneTransform(hud->m_item_transform, hud->m_model, m_particles_bone, fallback);
    Fmatrix transform = LightRotation(mount, m_particles_orientation);
    mount.transform_tiny(transform.c, m_particles_offset);
    if (!m_particles) {
        m_particles = CParticlesObject::Create(m_particles_name.c_str(), FALSE);
        m_particles->UpdateParent(transform, zero_vel);
        m_particles->Play(true);
    } else if (m_particles->IsPlaying()) {
        m_particles->UpdateParent(transform, zero_vel);
    } else {
        CParticlesObject::Destroy(m_particles);
    }
}

void CFlashlight::StopParticles() {
    if (m_particles) {
        m_particles->Stop(FALSE);
        CParticlesObject::Destroy(m_particles);
    }
}

void CFlashlight::StopEffects() {
    if (m_light)
        m_light->set_active(false);
    if (m_omni)
        m_omni->set_active(false);
    if (m_glow)
        m_glow->set_active(false);
    if (m_effects_active)
        UpdateConeBones(false);
    StopParticles();
    m_effects_active = false;
}
