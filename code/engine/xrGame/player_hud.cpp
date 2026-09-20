#include "stdafx.h"
#include "player_hud.h"
#include "player_hud_legs.h"
#include "HudItem.h"
#include "Weapon.h"
#include "ui_base.h"
#include "actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "actoreffector.h"
#include "../xrEngine/IGame_Persistent.h"

player_hud* g_player_hud = NULL;
extern ENGINE_API float psHUD_FOV;
extern ENGINE_API float IE_VIEWPORT_NEAR;
Fvector _ancor_pos;
Fvector _wpn_root_pos;

namespace {
constexpr float HUD_FOV_MIN = 0.1f;
constexpr float HUD_FOV_MAX = 1.0f;
constexpr float HUD_FOV_DEGREES_MIN = 5.f;
constexpr float HUD_FOV_DEGREES_MAX = 179.f;
constexpr float HUD_VIEWPORT_NEAR_MIN = 0.001f;
constexpr float HUD_VIEWPORT_NEAR_MAX = 1.f;

void DecodeControllerRotation(const CKeyQR& source, Fquaternion& result) {
    result.x = float(source.x) * KEY_QuantI;
    result.y = float(source.y) * KEY_QuantI;
    result.z = float(source.z) * KEY_QuantI;
    result.w = float(source.w) * KEY_QuantI;
}

void DecodeControllerTranslation(const CKeyQT8& source, const CMotion& motion,
                                 Fvector& result) {
    result.x = float(source.x1) * motion._sizeT.x + motion._initT.x;
    result.y = float(source.y1) * motion._sizeT.y + motion._initT.y;
    result.z = float(source.z1) * motion._sizeT.z + motion._initT.z;
}

void DecodeControllerTranslation(const CKeyQT16& source, const CMotion& motion,
                                 Fvector& result) {
    result.x = float(source.x1) * motion._sizeT.x + motion._initT.x;
    result.y = float(source.y1) * motion._sizeT.y + motion._initT.y;
    result.z = float(source.z1) * motion._sizeT.z + motion._initT.z;
}

bool SampleControllerMotion(CKey& result, const CBlend& blend, const CMotion& motion) {
    const u32 key_count = motion.get_count();
    if (!key_count)
        return false;

    const float key_time = std::max(blend.timeCurrent, 0.f) * SAMPLE_FPS;
    const u32 frame = iFloor(key_time);
    const float delta = clampr(key_time - float(frame), 0.f, 1.f);
    const u32 first_key = frame % key_count;
    const u32 second_key = (frame + 1) % key_count;

    if (motion.test_flag(flRKeyAbsent)) {
        DecodeControllerRotation(motion._keysR[0], result.Q);
    } else {
        Fquaternion first_rotation;
        Fquaternion second_rotation;
        DecodeControllerRotation(motion._keysR[first_key], first_rotation);
        DecodeControllerRotation(motion._keysR[second_key], second_rotation);
        result.Q.slerp(first_rotation, second_rotation, delta);
    }

    if (!motion.test_flag(flTKeyPresent)) {
        result.T.set(motion._initT);
        return true;
    }

    Fvector first_translation;
    Fvector second_translation;
    if (motion.test_flag(flTKey16IsBit)) {
        DecodeControllerTranslation(motion._keysT16[first_key], motion,
                                    first_translation);
        DecodeControllerTranslation(motion._keysT16[second_key], motion,
                                    second_translation);
    } else {
        DecodeControllerTranslation(motion._keysT8[first_key], motion,
                                    first_translation);
        DecodeControllerTranslation(motion._keysT8[second_key], motion,
                                    second_translation);
    }

    result.T.lerp(first_translation, second_translation, delta);
    return true;
}
} // namespace

float CalcMotionSpeed(const shared_str& anim_name) {

    if (!IsGameTypeSingle() && (anim_name == "anm_show" || anim_name == "anm_hide"))
        return 2.0f;
    else
        return 1.0f;
}

static bool ParseMotionSpeed(LPCSTR value, float& result) {
    if (!value || !value[0])
        return false;

    char* end = NULL;
    const float parsed = strtof(value, &end);
    if (end == value)
        return false;

    while (*end && isspace(static_cast<unsigned char>(*end)))
        ++end;

    if (*end)
        return false;

    result = parsed;
    return true;
}

static float ValidateMotionSpeed(float speed, LPCSTR section, LPCSTR alias) {
    if (speed > EPS_S && std::isfinite(speed))
        return speed;

    Msg("! HUD animation speed: invalid multiplier [%g] for [%s]:[%s], using 1.0",
        speed, section, alias);
    return 1.f;
}

player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name) {
    xr_vector<player_hud_motion>::iterator it = m_anims.begin();
    xr_vector<player_hud_motion>::iterator it_e = m_anims.end();
    for (; it != it_e; ++it) {
        const shared_str& s = (true) ? (*it).m_alias_name : (*it).m_base_name;
        if (s == name)
            return &(*it);
    }
    return NULL;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect) {
    CInifile::Sect& _sect = pSettings->r_section(sect);
    auto _b = _sect.Data.cbegin();
    auto _e = _sect.Data.cend();
    player_hud_motion* pm = NULL;

    string512 buff;
    MotionID motion_ID;

    for (; _b != _e; ++_b) {
        if (strstr(_b->first.c_str(), "anm_") == _b->first.c_str()) {
            const shared_str& anm = _b->second;
            m_anims.resize(m_anims.size() + 1);
            pm = &m_anims.back();
            // base and alias name
            pm->m_alias_name = _b->first;

            const u32 item_count = _GetItemCount(anm.c_str());
            R_ASSERT2(item_count >= 1 && item_count <= 3, anm.c_str());

            string512 str_item;
            _GetItem(anm.c_str(), 0, str_item);
            pm->m_base_name = str_item;
            pm->m_additional_name = str_item;

            if (item_count == 2) {
                _GetItem(anm.c_str(), 1, str_item);

                float configured_speed = 1.f;
                if (ParseMotionSpeed(str_item, configured_speed)) {
                    pm->m_anim_speed = ValidateMotionSpeed(
                        configured_speed, sect.c_str(), pm->m_alias_name.c_str());
                } else {
                    pm->m_additional_name = str_item;
                }
            } else if (item_count == 3) {
                _GetItem(anm.c_str(), 1, str_item);
                pm->m_additional_name = str_item;

                _GetItem(anm.c_str(), 2, str_item);
                float configured_speed = 1.f;
                R_ASSERT3(ParseMotionSpeed(str_item, configured_speed),
                          "invalid HUD animation speed multiplier", anm.c_str());
                pm->m_anim_speed = ValidateMotionSpeed(
                    configured_speed, sect.c_str(), pm->m_alias_name.c_str());
            }

            // and load all motions for it

            for (u32 i = 0; i <= 8; ++i) {
                if (i == 0)
                    xr_strcpy(buff, pm->m_base_name.c_str());
                else
                    xr_sprintf(buff, "%s%d", pm->m_base_name.c_str(), i);

                motion_ID = model->ID_Cycle_Safe(buff);
                if (motion_ID.valid()) {
                    pm->m_animations.resize(pm->m_animations.size() + 1);
                    pm->m_animations.back().mid = motion_ID;
                    pm->m_animations.back().name = buff;
#ifdef DEBUG
//					Msg(" alias=[%s] base=[%s] name=[%s]",pm->m_alias_name.c_str(),
//pm->m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
                }
            }
            R_ASSERT2(pm->m_animations.size(),
                      make_string("motion not found [%s]", pm->m_base_name.c_str()).c_str());
        }
    }
}

Fvector& attachable_hud_item::hands_attach_pos() { return m_measures.m_hands_attach[0]; }

Fvector& attachable_hud_item::hands_attach_rot() { return m_measures.m_hands_attach[1]; }

Fvector& attachable_hud_item::hands_offset_pos() {
    u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot() {
    u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[1][idx];
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility,
                                           BOOL bSilent) {
    u16 bone_id;
    BOOL bVisibleNow;
    // TODO: [imdex] remove shared_str
    bone_id = m_model->LL_BoneID(*bone_name);
    if (bone_id == BI_NONE) {
        if (bSilent)
            return;
        R_ASSERT2(0, make_string("model [%s] has no bone [%s]",
                                 pSettings->r_string(m_sect_name, "item_visual"), bone_name.c_str())
                         .c_str());
    }
    bVisibleNow = m_model->LL_GetBoneVisible(bone_id);
    if (bVisibleNow != bVisibility)
        m_model->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce) {
    if (!bForce && m_upd_firedeps_frame == Device.dwFrame)
        return;
    bool is_16x9 = UI().is_widescreen();

    if (!!m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now) != is_16x9)
        m_measures.load(m_sect_name, m_model);

    Fvector ypr = m_measures.m_item_attach[1];
    ypr.mul(PI / 180.f);
    m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
    m_attach_offset.translate_over(m_measures.m_item_attach[0]);

    m_parent->calc_transform(m_attach_place_idx, m_attach_offset, m_item_transform);
    m_upd_firedeps_frame = Device.dwFrame;

    IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();
    if (ka) {
        ka->UpdateTracks();
        ka->dcast_PKinematics()->CalculateBones_Invalidate();
        ka->dcast_PKinematics()->CalculateBones(TRUE);
    }
}

void attachable_hud_item::update_hud_additional(Fmatrix& trans) {
    if (m_parent_hud_item) {
        m_parent_hud_item->UpdateHudAdditonal(trans);
    }
}

void attachable_hud_item::setup_firedeps(firedeps& fd) {
    update(false);
    // fire point&direction
    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point)) {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
        fire_mat.transform_tiny(fd.vLastFP, m_measures.m_fire_point_offset);
        m_item_transform.transform_tiny(fd.vLastFP);

        fd.vLastFD.set(0.f, 0.f, 1.f);
        m_item_transform.transform_dir(fd.vLastFD);
        VERIFY(xr::valid(fd.vLastFD));
        VERIFY(xr::valid(fd.vLastFD));

        fd.m_FireParticlesXForm.identity();
        fd.m_FireParticlesXForm.k.set(fd.vLastFD);
        Fvector::generate_orthonormal_basis_normalized(
            fd.m_FireParticlesXForm.k, fd.m_FireParticlesXForm.j, fd.m_FireParticlesXForm.i);
        VERIFY(xr::valid(fd.m_FireParticlesXForm));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2)) {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone2);
        fire_mat.transform_tiny(fd.vLastFP2, m_measures.m_fire_point2_offset);
        m_item_transform.transform_tiny(fd.vLastFP2);
        VERIFY(xr::valid(fd.vLastFP2));
        VERIFY(xr::valid(fd.vLastFP2));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_shell_point)) {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_shell_bone);
        fire_mat.transform_tiny(fd.vLastSP, m_measures.m_shell_point_offset);
        m_item_transform.transform_tiny(fd.vLastSP);
        VERIFY(xr::valid(fd.vLastSP));
        VERIFY(xr::valid(fd.vLastSP));
    }
}

bool attachable_hud_item::need_renderable() {
    if (m_controller_owned)
        return true;

    return m_parent_hud_item && m_parent_hud_item->need_renderable();
}

void attachable_hud_item::render() {
    ::Render->set_Transform(&m_item_transform);
    ::Render->add_Visual(m_model->dcast_RenderVisual());

    debug_draw_firedeps();

    if (m_parent_hud_item)
        m_parent_hud_item->render_hud_mode();
}

bool attachable_hud_item::render_item_ui_query() {
    if (!m_parent_hud_item)
        return false;

    return m_parent_hud_item->render_item_3d_ui_query();
}

void attachable_hud_item::render_item_ui() {
    if (!m_parent_hud_item)
        return;

    m_parent_hud_item->render_item_3d_ui();
}

void hud_item_measures::load(const shared_str& sect_name, IKinematics* K) {
    bool is_16x9 = UI().is_widescreen();
    string64 _prefix;
    xr_sprintf(_prefix, "%s", is_16x9 ? "_16x9" : "");
    string128 val_name;

    strconcat(sizeof(val_name), val_name, "hands_position", _prefix);
    m_hands_attach[0] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(sizeof(val_name), val_name, "hands_orientation", _prefix);
    m_hands_attach[1] = pSettings->r_fvector3(sect_name, val_name);

    m_item_attach[0] = pSettings->r_fvector3(sect_name, "item_position");
    m_item_attach[1] = pSettings->r_fvector3(sect_name, "item_orientation");

    shared_str bone_name;
    m_prop_flags.set(e_fire_point, pSettings->line_exist(sect_name, "fire_bone"));
    if (m_prop_flags.test(e_fire_point)) {
        bone_name = pSettings->r_string(sect_name, "fire_bone");
        // TODO: [imdex] remove shared_str
        m_fire_bone = K->LL_BoneID(*bone_name);
        m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
    } else
        m_fire_point_offset.set(0, 0, 0);

    m_prop_flags.set(e_fire_point2, pSettings->line_exist(sect_name, "fire_bone2"));
    if (m_prop_flags.test(e_fire_point2)) {
        bone_name = pSettings->r_string(sect_name, "fire_bone2");
        // TODO: [imdex] remove shared_str
        m_fire_bone2 = K->LL_BoneID(*bone_name);
        m_fire_point2_offset = pSettings->r_fvector3(sect_name, "fire_point2");
    } else
        m_fire_point2_offset.set(0, 0, 0);

    m_prop_flags.set(e_shell_point, pSettings->line_exist(sect_name, "shell_bone"));
    if (m_prop_flags.test(e_shell_point)) {
        bone_name = pSettings->r_string(sect_name, "shell_bone");
        // TODO: [imdex] remove shared_str
        m_shell_bone = K->LL_BoneID(*bone_name);
        m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
    } else
        m_shell_point_offset.set(0, 0, 0);

    m_hands_offset[0][0].set(0, 0, 0);
    m_hands_offset[1][0].set(0, 0, 0);

    strconcat(sizeof(val_name), val_name, "aim_hud_offset_pos", _prefix);
    m_hands_offset[0][1] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(sizeof(val_name), val_name, "aim_hud_offset_rot", _prefix);
    m_hands_offset[1][1] = pSettings->r_fvector3(sect_name, val_name);

    strconcat(sizeof(val_name), val_name, "gl_hud_offset_pos", _prefix);
    m_hands_offset[0][2] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(sizeof(val_name), val_name, "gl_hud_offset_rot", _prefix);
    m_hands_offset[1][2] = pSettings->r_fvector3(sect_name, val_name);

    R_ASSERT2(pSettings->line_exist(sect_name, "fire_point") ==
                  pSettings->line_exist(sect_name, "fire_bone"),
              sect_name.c_str());
    R_ASSERT2(pSettings->line_exist(sect_name, "fire_point2") ==
                  pSettings->line_exist(sect_name, "fire_bone2"),
              sect_name.c_str());
    R_ASSERT2(pSettings->line_exist(sect_name, "shell_point") ==
                  pSettings->line_exist(sect_name, "shell_bone"),
              sect_name.c_str());

    m_prop_flags.set(e_16x9_mode_now, is_16x9);
}

attachable_hud_item::~attachable_hud_item() {
    IRenderVisual* v = m_model->dcast_RenderVisual();
    ::Render->model_Delete(v);
    m_model = NULL;
}

void attachable_hud_item::load(const shared_str& sect_name) {
    m_sect_name = sect_name;

    m_preserve_other_hand =
        !!READ_IF_EXISTS(pSettings, r_bool, sect_name, "preserve_other_hand", false);
    m_hud_fov = 0.f;
    m_hud_fov_degrees = 0.f;
    m_viewport_near = 0.f;

    if (pSettings->line_exist(sect_name, "hud_fov")) {
        const float configured_hud_fov = pSettings->r_float(sect_name, "hud_fov");

        if (configured_hud_fov >= HUD_FOV_MIN && configured_hud_fov <= HUD_FOV_MAX) {
            m_hud_fov = configured_hud_fov;
        } else {
            Msg("! HUD FOV: invalid value [%.3f] in section [%s]; expected [%.1f, %.1f], "
                "using user.ltx value",
                configured_hud_fov, sect_name.c_str(), HUD_FOV_MIN, HUD_FOV_MAX);
        }
    }

    if (pSettings->line_exist(sect_name, "hud_fov_degrees")) {
        const float configured_hud_fov_degrees =
            pSettings->r_float(sect_name, "hud_fov_degrees");

        if (configured_hud_fov_degrees >= HUD_FOV_DEGREES_MIN &&
            configured_hud_fov_degrees <= HUD_FOV_DEGREES_MAX) {
            m_hud_fov_degrees = configured_hud_fov_degrees;
        } else {
            Msg("! HUD FOV: invalid degree value [%.3f] in section [%s]; expected "
                "[%.1f, %.1f], falling back to hud_fov/user.ltx",
                configured_hud_fov_degrees, sect_name.c_str(), HUD_FOV_DEGREES_MIN,
                HUD_FOV_DEGREES_MAX);
        }
    }

    if (pSettings->line_exist(sect_name, "viewport_near")) {
        const float configured_viewport_near =
            pSettings->r_float(sect_name, "viewport_near");

        if (configured_viewport_near >= HUD_VIEWPORT_NEAR_MIN &&
            configured_viewport_near <= HUD_VIEWPORT_NEAR_MAX) {
            m_viewport_near = configured_viewport_near;
        } else {
            Msg("! HUD viewport near: invalid value [%.4f] in section [%s]; expected "
                "[%.3f, %.1f], using engine_external.ltx value",
                configured_viewport_near, sect_name.c_str(), HUD_VIEWPORT_NEAR_MIN,
                HUD_VIEWPORT_NEAR_MAX);
        }
    }

    // Visual
    const shared_str& visual_name = pSettings->r_string(sect_name, "item_visual");
    m_model = smart_cast<IKinematics*>(::Render->model_Create(visual_name.c_str()));

    m_attach_place_idx = pSettings->r_u16(sect_name, "attach_place_idx");
    m_measures.load(sect_name, m_model);
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md,
                                   u8& rnd_idx) {
    R_ASSERT(strstr(anm_name_b.c_str(), "anm_") == anm_name_b.c_str());
    string256 anim_name_r;
    bool is_16x9 = UI().is_widescreen();
    xr_sprintf(anim_name_r, "%s%s", anm_name_b.c_str(),
               ((m_attach_place_idx == 1) && is_16x9) ? "_16x9" : "");

    player_hud_motion* anm = m_hand_motions.find_motion(anim_name_r);
    R_ASSERT2(anm, make_string("model [%s] has no motion alias defined [%s]", m_sect_name.c_str(),
                               anim_name_r)
                       .c_str());
    R_ASSERT2(anm->m_animations.size(),
              make_string("model [%s] has no motion defined in motion_alias [%s]",
                          pSettings->r_string(m_sect_name, "item_visual"), anim_name_r)
                  .c_str());

    const float speed = CalcMotionSpeed(anm_name_b) * anm->m_anim_speed;
    m_last_anim_speed = speed;

    rnd_idx = (u8)Random.randI(anm->m_animations.size());
    const motion_descr& M = anm->m_animations[rnd_idx];

    const bool preserve_other_hand = m_controller_owned && m_preserve_other_hand;
    u32 ret = g_player_hud->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed,
                                      preserve_other_hand);

    if (m_model->dcast_PKinematicsAnimated()) {
        IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();

        shared_str item_anm_name;
        if (anm->m_base_name != anm->m_additional_name)
            item_anm_name = anm->m_additional_name;
        else
            item_anm_name = M.name;

        MotionID M2 = ka->ID_Cycle_Safe(item_anm_name);
        if (!M2.valid())
            M2 = ka->ID_Cycle_Safe("idle");
        else if (bDebug)
            Msg("playing item animation [%s]", item_anm_name.c_str());

        R_ASSERT3(M2.valid(), "model has no motion [idle] ",
                  pSettings->r_string(m_sect_name, "item_visual"));

        u16 root_id = m_model->LL_GetBoneRoot();
        CBoneInstance& root_binst = m_model->LL_GetBoneInstance(root_id);
        root_binst.set_callback_overwrite(TRUE);
        root_binst.mTransform.identity();

        u16 pc = ka->partitions().count();
        for (u16 pid = 0; pid < pc; ++pid) {
            CBlend* B = ka->PlayCycle(pid, M2, bMixIn);
            R_ASSERT(B);
            B->speed *= speed;
        }

        m_model->CalculateBones_Invalidate();
    }

    // Controller-owned HUD visuals do not have a CHudItem parent.
    if (m_parent_hud_item) {
        CPhysicItem& parent_object = m_parent_hud_item->object();

        if (IsGameTypeSingle() &&
            parent_object.H_Parent() == Level().CurrentControlEntity()) {
            CActor* current_actor =
                static_cast_checked<CActor*>(Level().CurrentControlEntity());
            VERIFY(current_actor);

            CEffectorCam* ec = current_actor->Cameras().GetCamEffector(eCEWeaponAction);
            if (NULL == ec) {
                string_path ce_path;
                string_path anm_name;
                strconcat(sizeof(anm_name), anm_name, "camera_effects\\weapon\\",
                          M.name.c_str(), ".anm");

                if (FS.exist(ce_path, "$game_anims$", anm_name)) {
                    CAnimatorCamEffector* e = xr_new<CAnimatorCamEffector>();
                    e->SetType(eCEWeaponAction);
                    e->SetHudAffect(false);
                    e->SetCyclic(false);
                    e->Start(anm_name);
                    current_actor->Cameras().AddCamEffector(e);
                }
            }
        }
    }
    return ret;
}

player_hud::player_hud() {
    m_model = NULL;
    m_attached_items[0] = NULL;
    m_attached_items[1] = NULL;
    m_controller_item = NULL;
    m_controller_motion.invalidate();
    m_legs_controller = xr_new<CActorLegsController>();
    m_default_hud_fov = 0.f;
    m_applied_hud_fov = 0.f;
    m_hud_fov_override_active = false;
    m_hud_fov_source = NULL;
    m_default_viewport_near = 0.f;
    m_viewport_near_override_active = false;
    m_viewport_near_source = NULL;
    m_transform.identity();
}

player_hud::~player_hud() {
    m_attached_items[0] = NULL;
    m_attached_items[1] = NULL;
    m_controller_item = NULL;
    UpdateHudProjection();

    xr_delete(m_legs_controller);

    IRenderVisual* v = m_model->dcast_RenderVisual();
    ::Render->model_Delete(v);
    m_model = NULL;

    xr_vector<attachable_hud_item*>::iterator it = m_pool.begin();
    xr_vector<attachable_hud_item*>::iterator it_e = m_pool.end();
    for (; it != it_e; ++it) {
        attachable_hud_item* a = *it;
        xr_delete(a);
    }
    m_pool.clear();
}

attachable_hud_item* player_hud::attach_controller_item(const shared_str& hud_section) {
    // На всякий случай удаляем предыдущий temporary HUD.
    if (m_controller_item)
        detach_controller_item();

    attachable_hud_item* pi = create_hud_item(hud_section);

    if (!pi)
        return NULL;

    const u16 item_idx = pi->m_attach_place_idx;

    // Пока для consumables ожидаем основной HUD slot.
    if (item_idx > 1) {
        Msg("! ItemUse: invalid HUD attach index [%d] for section [%s]",
            item_idx, hud_section.c_str());
        return NULL;
    }

    //
    // Здесь позже перед attach мы будем гарантированно
    // убирать оружие.
    //

    if (m_attached_items[item_idx] && m_attached_items[item_idx] != pi) {
        detach_item_idx(item_idx);
    }

    m_attached_items[item_idx] = pi;

    pi->m_parent_hud_item = NULL;
    pi->m_controller_owned = true;

    m_controller_item = pi;
    m_controller_motion.invalidate();

    UpdateHudProjection();

    Msg("* ItemUse: attached HUD section [%s]", hud_section.c_str());

    return pi;
}

void player_hud::detach_controller_item() {
    if (!m_controller_item)
        return;

    const u16 idx = m_controller_item->m_attach_place_idx;

    if (idx < 2 && m_attached_items[idx] == m_controller_item)
        m_attached_items[idx] = NULL;

    m_controller_item->m_controller_owned = false;
    m_controller_item->m_parent_hud_item = NULL;

    Msg("* ItemUse: detached HUD section [%s]", m_controller_item->m_sect_name.c_str());

    m_controller_item = NULL;
    m_controller_motion.invalidate();

    UpdateHudProjection();

    OnMovementChanged(mcAnyMove);
}

void player_hud::load(const shared_str& player_hud_sect) {
    if (player_hud_sect == m_sect_name)
        return;
    bool b_reload = (m_model != NULL);
    if (m_model) {
        IRenderVisual* v = m_model->dcast_RenderVisual();
        ::Render->model_Delete(v);
    }

    m_sect_name = player_hud_sect;
    const shared_str& model_name = pSettings->r_string(player_hud_sect, "visual");
    m_model = smart_cast<IKinematicsAnimated*>(::Render->model_Create(model_name.c_str()));
    m_controller_motion.invalidate();

    if (pSettings->line_exist("hud_extensions", "hands_animations_path")) {
        LPCSTR hand_animations = pSettings->r_string("hud_extensions", "hands_animations_path");

        const u32 loaded_motions = m_model->LoadAdditionalMotions(hand_animations, true);

        if (loaded_motions) {
            Msg("* HUD: loaded [%u] external hand motion file(s)", loaded_motions);
        }
    }

    CInifile::Sect& _sect = pSettings->r_section(player_hud_sect);
    auto _b = _sect.Data.cbegin();
    auto _e = _sect.Data.cend();
    for (; _b != _e; ++_b) {
        if (strstr(_b->first.c_str(), "ancor_") == _b->first.c_str()) {
            const shared_str& _bone = _b->second;
            // TODO: [imdex] remove shared_str
            m_ancors.push_back(m_model->dcast_PKinematics()->LL_BoneID(*_bone));
        }
    }

    //	Msg("hands visual changed to[%s] [%s] [%s]", model_name.c_str(), b_reload?"R":"",
    //m_attached_items[0]?"Y":"");

    if (!b_reload) {
        m_model->PlayCycle("hand_idle_doun");
    } else {
        if (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item)
            m_attached_items[1]->m_parent_hud_item->on_a_hud_attach();

        if (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item)
            m_attached_items[0]->m_parent_hud_item->on_a_hud_attach();
    }
    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model->dcast_PKinematics()->CalculateBones(TRUE);

    m_legs_controller->Load(player_hud_sect);
}

bool player_hud::render_item_ui_query() {
    bool res = false;
    if (m_attached_items[0])
        res |= m_attached_items[0]->render_item_ui_query();

    if (m_attached_items[1])
        res |= m_attached_items[1]->render_item_ui_query();

    return res;
}

void player_hud::render_item_ui() {
    if (m_attached_items[0])
        m_attached_items[0]->render_item_ui();

    if (m_attached_items[1])
        m_attached_items[1]->render_item_ui();
}

void player_hud::render_hud() {
    bool b_r0 = m_attached_items[0] && m_attached_items[0]->need_renderable();
    bool b_r1 = m_attached_items[1] && m_attached_items[1]->need_renderable();

    if (b_r0 || b_r1) {
        ::Render->set_Transform(&m_transform);
        ::Render->add_Visual(m_model->dcast_RenderVisual());
    }

    if (b_r0)
        m_attached_items[0]->render();

    if (b_r1)
        m_attached_items[1]->render();

    m_legs_controller->Render();
}

#include "../xrEngine/motion.h"

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name,
                              const CMotionDef*& md) {
    attachable_hud_item* pi = create_hud_item(hud_name);
    player_hud_motion* pm = pi->m_hand_motions.find_motion(anim_name);
    if (!pm)
        return 100; // ms TEMPORARY
    R_ASSERT2(pm, make_string("hudItem model [%s] has no motion with alias [%s]", hud_name.c_str(),
                              anim_name.c_str())
                      .c_str());
    const float speed = CalcMotionSpeed(anim_name) * pm->m_anim_speed;
    return motion_length(pm->m_animations[0].mid, md, speed);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed) {
    md = m_model->LL_GetMotionDef(M);
    VERIFY(md);
    if (md->flags & esmStopAtEnd) {
        CMotion* motion = m_model->LL_GetRootMotion(M);
        return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
    }
    return 0;
}
const Fvector& player_hud::attach_rot() const {
    if (m_attached_items[0])
        return m_attached_items[0]->hands_attach_rot();
    else if (m_attached_items[1])
        return m_attached_items[1]->hands_attach_rot();
    else
        return Fvector().set(0, 0, 0);
}

const Fvector& player_hud::attach_pos() const {
    if (m_attached_items[0])
        return m_attached_items[0]->hands_attach_pos();
    else if (m_attached_items[1])
        return m_attached_items[1]->hands_attach_pos();
    else
        return Fvector().set(0, 0, 0);
}

void player_hud::update(const Fmatrix& cam_trans) {
    // Keep per-section projection overrides synchronized with the active HUD.
    // Absolute degree FOV must also be converted every frame because zoom and
    // camera effectors may change Device.fFOV.
    UpdateHudProjection();

    Fmatrix trans = cam_trans;
    update_inertion(trans);

    // A controller-owned hand must use the transform it would have had while
    // attached alone. Weapon-specific offsets are applied only afterwards.
    const Fmatrix controller_trans = trans;
    update_additional(trans);

    Fvector ypr = attach_rot();
    ypr.mul(PI / 180.f);
    m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
    m_attach_offset.translate_over(attach_pos());
    m_transform.mul(trans, m_attach_offset);
    // insert inertion here

    m_model->UpdateTracks();
    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model->dcast_PKinematics()->CalculateBones(TRUE);

    ApplyControllerHandTransform(controller_trans);

    if (m_attached_items[0])
        m_attached_items[0]->update(true);

    if (m_attached_items[1])
        m_attached_items[1]->update(true);
}

void player_hud::ApplyControllerHandTransform(const Fmatrix& controller_trans) {
    if (!m_controller_item || !m_controller_item->m_controller_owned ||
        !m_controller_item->m_preserve_other_hand ||
        m_controller_item->m_attach_place_idx != 1 ||
        m_attached_items[1] != m_controller_item || !m_attached_items[0]) {
        return;
    }

    const u16 part_id = m_model->partitions().part_id("left_hand");
    if (part_id == u16(-1))
        return;

    Fvector controller_ypr = m_controller_item->hands_attach_rot();
    controller_ypr.mul(PI / 180.f);

    Fmatrix controller_attach;
    controller_attach.setHPB(controller_ypr.x, controller_ypr.y, controller_ypr.z);
    controller_attach.translate_over(m_controller_item->hands_attach_pos());

    Fmatrix desired_transform;
    desired_transform.mul(controller_trans, controller_attach);

    Fmatrix current_inverse;
    current_inverse.invert(m_transform);

    // Bone transforms are model-local. This correction makes the left-hand
    // partition render as desired_transform while the shared HUD model and
    // right-hand partition continue using m_transform from slot 0.
    Fmatrix correction;
    correction.mul_43(current_inverse, desired_transform);

    IKinematics* kinematics = m_model->dcast_PKinematics();

    // With two attached items, preserve_other_hand deliberately keeps the
    // controller motion off the shared root partition so the weapon does not
    // inherit the left-hand animation. Reconstruct that omitted root pose and
    // apply it only to left-hand bones. Without this step, root translation in
    // the exported motion remains replaced by the weapon pose, which shows up
    // as a horizontal offset even after the HUD-section transform is isolated.
    if (m_controller_motion.valid()) {
        CBlend* controller_blend = NULL;
        const u32 blend_count = m_model->LL_PartBlendsCount(part_id);
        for (u32 blend_idx = 0; blend_idx < blend_count; ++blend_idx) {
            CBlend* blend = m_model->LL_PartBlend(part_id, blend_idx);
            if (blend && blend->blend_state() != CBlend::eFREE_SLOT &&
                blend->motionID == m_controller_motion) {
                controller_blend = blend;
            }
        }

        if (controller_blend) {
            CMotion* controller_root_motion =
                m_model->LL_GetRootMotion(m_controller_motion);
            CKey controller_root_key;
            if (!controller_root_motion ||
                !SampleControllerMotion(controller_root_key, *controller_blend,
                                        *controller_root_motion)) {
                controller_blend = NULL;
            }

            if (controller_blend) {
                Fmatrix controller_root;
                controller_root.mk_xform(controller_root_key.Q, controller_root_key.T);

                const u16 root_id = kinematics->LL_GetBoneRoot();
                Fmatrix current_root_inverse;
                current_root_inverse.invert(
                    kinematics->LL_GetBoneInstance(root_id).mTransform);

                Fmatrix root_correction;
                root_correction.mul_43(controller_root, current_root_inverse);

                Fmatrix combined_correction;
                combined_correction.mul_43(correction, root_correction);
                correction.set(combined_correction);
            }
        }
    }

    const CPartDef& left_hand = m_model->partitions().part(part_id);
    for (u32 bone_id : left_hand.bones) {
        if (bone_id >= kinematics->LL_BoneCount())
            continue;

        CBoneInstance& bone = kinematics->LL_GetBoneInstance((u16)bone_id);
        bone.mTransform.mulA_43(correction);
        bone.mRenderTransform.mul_43(
            bone.mTransform, kinematics->LL_GetData((u16)bone_id).m2b_transform);
    }
}

u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md,
                          float speed, bool preserve_other_hand) {

    u16 part_id = u16(-1);
    if (attached_item(0) && attached_item(1))
        part_id = m_model->partitions().part_id((part == 0) ? "right_hand" : "left_hand");

    if (preserve_other_hand && part == 1)
        m_controller_motion = M;

    u16 pc = m_model->partitions().count();
    for (u16 pid = 0; pid < pc; ++pid) {
        const bool play_partition =
            part_id == u16(-1) || pid == part_id || (!preserve_other_hand && pid == 0);

        if (play_partition) {
            CBlend* B = m_model->PlayCycle(pid, M, bMixIn);
            R_ASSERT(B);
            B->speed *= speed;
        }
    }
    m_model->dcast_PKinematics()->CalculateBones_Invalidate();

    return motion_length(M, md, speed);
}

u32 player_hud::play_controller_motion(const shared_str& motion_name, BOOL bMixIn,
                                       shared_str* played_motion_name) {
    if (played_motion_name)
        *played_motion_name = NULL;

    if (!m_controller_item) {
        Msg("! ItemUse: no controller HUD item attached");
        return 0;
    }

    if (!has_controller_motion(motion_name)) {
        Msg("! ItemUse: controller HUD [%s] has no motion alias [%s]",
            m_controller_item->m_sect_name.c_str(), motion_name.c_str());
        return 0;
    }

    const CMotionDef* md = NULL;
    u8 rnd = 0;

    const u32 duration = m_controller_item->anim_play(motion_name, bMixIn, md, rnd);

    if (played_motion_name) {
        string256 resolved_motion_name;
        const bool is_16x9 = UI().is_widescreen();

        xr_sprintf(resolved_motion_name, "%s%s", motion_name.c_str(),
                   ((m_controller_item->m_attach_place_idx == 1) && is_16x9) ? "_16x9" : "");

        player_hud_motion* motion =
            m_controller_item->m_hand_motions.find_motion(resolved_motion_name);

        if (motion && rnd < motion->m_animations.size())
            *played_motion_name = motion->m_animations[rnd].name;
    }

    return duration;
}

float player_hud::controller_motion_speed() const {
    return m_controller_item ? m_controller_item->last_anim_speed() : 1.f;
}

bool player_hud::has_hud_motion(const shared_str& hud_section,
                                const shared_str& motion_name) {
    if (!m_model || !hud_section.size() || !motion_name.size() ||
        !pSettings->section_exist(hud_section.c_str()) ||
        !pSettings->line_exist(hud_section.c_str(), motion_name.c_str())) {
        return false;
    }

    LPCSTR motion_config = pSettings->r_string(hud_section.c_str(), motion_name.c_str());
    const u32 item_count = _GetItemCount(motion_config);

    if (item_count < 1 || item_count > 3)
        return false;

    string512 base_motion;
    _GetItem(motion_config, 0, base_motion);

    string512 candidate;

    for (u32 index = 0; index <= 8; ++index) {
        if (index == 0)
            xr_strcpy(candidate, base_motion);
        else
            xr_sprintf(candidate, "%s%u", base_motion, index);

        if (m_model->ID_Cycle_Safe(candidate).valid())
            return true;
    }

    return false;
}

bool player_hud::can_attach_controller_item(const shared_str& hud_section) {
    if (!m_model || !hud_section.size() ||
        !pSettings->section_exist(hud_section.c_str()) ||
        !pSettings->line_exist(hud_section.c_str(), "item_visual") ||
        !pSettings->line_exist(hud_section.c_str(), "attach_place_idx") ||
        pSettings->r_u16(hud_section.c_str(), "attach_place_idx") > 1) {
        return false;
    }

    LPCSTR configured_visual = pSettings->r_string(hud_section.c_str(), "item_visual");

    if (!configured_visual || !configured_visual[0])
        return false;

    string_path visual_name;

    if (strext(configured_visual))
        xr_strcpy(visual_name, configured_visual);
    else
        strconcat(sizeof(visual_name), visual_name, configured_visual, ".ogf");

    string_path resolved_visual;
    const bool visual_exists =
        !!FS.exist(configured_visual) ||
        !!FS.exist(resolved_visual, "$level$", visual_name) ||
        !!FS.exist(resolved_visual, "$game_meshes$", visual_name);

    if (!visual_exists)
        return false;

    // create_hud_item() loads every anm_* alias in the section. Verify all of
    // them up front so optional controller HUDs never reach its legacy assert
    // path when an external OMF is missing or incomplete.
    CInifile::Sect& section = pSettings->r_section(hud_section.c_str());

    for (auto line = section.Data.cbegin(); line != section.Data.cend(); ++line) {
        if (strstr(line->first.c_str(), "anm_") == line->first.c_str() &&
            !has_hud_motion(hud_section, line->first)) {
            return false;
        }
    }

    return true;
}

bool player_hud::has_controller_motion(const shared_str& motion_name) {
    if (!m_controller_item || !motion_name.size())
        return false;

    string256 resolved_motion_name;
    const bool is_16x9 = UI().is_widescreen();

    xr_sprintf(resolved_motion_name, "%s%s", motion_name.c_str(),
               ((m_controller_item->m_attach_place_idx == 1) && is_16x9) ? "_16x9" : "");

    player_hud_motion* motion =
        m_controller_item->m_hand_motions.find_motion(resolved_motion_name);

    return motion && motion->m_animations.size();
}

bool player_hud::controller_item_transform(Fmatrix& result, LPCSTR bone_name,
                                           const Fvector& offset,
                                           const Fvector& orientation) {
    if (!m_controller_item)
        return false;

    // Keep the temporary item and its bones current even though ItemUseController
    // is updated before player_hud::update() in the actor frame.
    m_controller_item->update(true);

    Fmatrix base_transform;
    base_transform.set(m_controller_item->m_item_transform);

    if (bone_name && bone_name[0]) {
        const u16 bone_id = m_controller_item->m_model->LL_BoneID(bone_name);

        if (bone_id == BI_NONE)
            return false;

        base_transform.mul_43(m_controller_item->m_item_transform,
                              m_controller_item->m_model->LL_GetTransform(bone_id));
    }

    // Position is expressed in the selected bone's local coordinates.
    Fvector position;
    base_transform.transform_tiny(position, offset);

    Fvector rotation_angles = orientation;
    rotation_angles.mul(PI / 180.f);

    Fmatrix rotation;
    rotation.setHPB(rotation_angles.x, rotation_angles.y, rotation_angles.z);

    result.mul_43(base_transform, rotation);
    result.c.set(position);

    return true;
}

void player_hud::update_additional(Fmatrix& trans) {
    if (m_attached_items[0])
        m_attached_items[0]->update_hud_additional(trans);

    if (m_attached_items[1])
        m_attached_items[1]->update_hud_additional(trans);
}

static const float PITCH_OFFSET_R = 0.017f;
static const float PITCH_OFFSET_N = 0.012f;
static const float PITCH_OFFSET_D = 0.02f;
static const float ORIGIN_OFFSET = -0.05f;
static const float TENDTO_SPEED = 5.f;

void player_hud::update_inertion(Fmatrix& trans) {
    if (inertion_allowed()) {
        Fmatrix xform;
        Fvector& origin = trans.c;
        xform = trans;

        static Fvector st_last_dir = { 0, 0, 0 };

        // calc difference
        Fvector diff_dir;
        diff_dir.sub(xform.k, st_last_dir);

        // clamp by PI_DIV_2
        Fvector last;
        last.normalize_safe(st_last_dir);
        float dot = last.dotproduct(xform.k);
        if (dot < EPS) {
            Fvector v0;
            v0.crossproduct(st_last_dir, xform.k);
            st_last_dir.crossproduct(xform.k, v0);
            diff_dir.sub(xform.k, st_last_dir);
        }

        // tend to forward
        st_last_dir.mad(diff_dir, TENDTO_SPEED * Device.fTimeDelta);
        origin.mad(diff_dir, ORIGIN_OFFSET);

        // pitch compensation
        float pitch = angle_normalize_signed(xform.k.getP());
        origin.mad(xform.k, -pitch * PITCH_OFFSET_D);
        origin.mad(xform.i, -pitch * PITCH_OFFSET_R);
        origin.mad(xform.j, -pitch * PITCH_OFFSET_N);
    }
}

attachable_hud_item* player_hud::create_hud_item(const shared_str& sect) {
    xr_vector<attachable_hud_item*>::iterator it = m_pool.begin();
    xr_vector<attachable_hud_item*>::iterator it_e = m_pool.end();
    for (; it != it_e; ++it) {
        attachable_hud_item* itm = *it;
        if (itm->m_sect_name == sect)
            return itm;
    }
    attachable_hud_item* res = xr_new<attachable_hud_item>(this);
    res->load(sect);
    res->m_hand_motions.load(m_model, sect);
    m_pool.push_back(res);

    return res;
}

bool player_hud::allow_activation(CHudItem* item) {
    attachable_hud_item* attached = m_attached_items[1];
    if (!attached || !attached->m_parent_hud_item)
        return true;

    return attached->m_parent_hud_item->CheckCompatibility(item);
}

void player_hud::attach_item(CHudItem* item) {
    attachable_hud_item* pi = create_hud_item(item->HudSection());
    int item_idx = pi->m_attach_place_idx;

    if (m_attached_items[item_idx] != pi) {
        if (m_attached_items[item_idx]) {
            if (m_attached_items[item_idx]->m_parent_hud_item)
                m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();
        }

        m_attached_items[item_idx] = pi;
        pi->m_parent_hud_item = item;

        if (item_idx == 0 && m_attached_items[1] &&
            m_attached_items[1]->m_parent_hud_item) {
            m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
        }

        item->on_a_hud_attach();
    }
    pi->m_parent_hud_item = item;
    pi->m_controller_owned = false;

    UpdateHudProjection();
}

void player_hud::detach_item_idx(u16 idx) {
    if (NULL == attached_item(idx))
        return;

    attachable_hud_item* item = m_attached_items[idx];

    if (item->m_parent_hud_item)
        item->m_parent_hud_item->on_b_hud_detach();

    item->m_parent_hud_item = NULL;
    item->m_controller_owned = false;

    m_attached_items[idx] = NULL;

    if (idx == 1 && attached_item(0)) {
        u16 part_idR = m_model->partitions().part_id("right_hand");
        u32 bc = m_model->LL_PartBlendsCount(part_idR);
        for (u32 bidx = 0; bidx < bc; ++bidx) {
            CBlend* BR = m_model->LL_PartBlend(part_idR, bidx);
            if (!BR)
                continue;

            MotionID M = BR->motionID;

            u16 pc = m_model->partitions().count();
            for (u16 pid = 0; pid < pc; ++pid) {
                if (pid != part_idR) {
                    CBlend* B = m_model->PlayCycle(
                        pid, M, TRUE); // this can destroy BR calling UpdateTracks !
                    if (BR->blend_state() != CBlend::eFREE_SLOT) {
                        u16 bop = B->bone_or_part;
                        *B = *BR;
                        B->bone_or_part = bop;
                    }
                }
            }
        }
    } else if (idx == 0 && attached_item(1)) {
        OnMovementChanged(mcAnyMove);
    }

    UpdateHudProjection();
}

void player_hud::detach_item(CHudItem* item) {
    if (NULL == item->HudItemData())
        return;
    u16 item_idx = item->HudItemData()->m_attach_place_idx;

    if (m_attached_items[item_idx] == item->HudItemData()) {
        detach_item_idx(item_idx);
    }
}

void player_hud::detach_all_items() {
    m_attached_items[0] = NULL;
    m_attached_items[1] = NULL;
    UpdateHudProjection();
}

void player_hud::UpdateHudProjection() {
    attachable_hud_item* source = NULL;

    if (m_controller_item && m_controller_item->m_attach_place_idx < 2 &&
        m_attached_items[m_controller_item->m_attach_place_idx] == m_controller_item) {
        source = m_controller_item;
    } else if (m_attached_items[0]) {
        source = m_attached_items[0];
    } else if (m_attached_items[1]) {
        source = m_attached_items[1];
    }

    // Preserve a console change made while an override is active. Normally
    // psHUD_FOV equals m_applied_hud_fov until this method restores it.
    if (m_hud_fov_override_active && !fsimilar(psHUD_FOV, m_applied_hud_fov))
        m_default_hud_fov = psHUD_FOV;

    const bool uses_degrees = source && source->m_hud_fov_degrees > 0.f;
    float override_hud_fov =
        uses_degrees
            ? source->m_hud_fov_degrees / std::max(Device.fFOV, EPS_S)
            : (source ? source->m_hud_fov : 0.f);

    float item_fov_factor = 1.f;
    if (source && source->m_parent_hud_item) {
        CWeapon* weapon = smart_cast<CWeapon*>(source->m_parent_hud_item);
        if (weapon)
            item_fov_factor = weapon->AlternativeHudFovFactor();
    }

    if (!fsimilar(item_fov_factor, 1.f)) {
        const float base_hud_fov = override_hud_fov > 0.f
            ? override_hud_fov
            : (m_hud_fov_override_active ? m_default_hud_fov : psHUD_FOV);
        override_hud_fov = base_hud_fov * item_fov_factor;
        clamp(override_hud_fov, HUD_FOV_MIN, HUD_FOV_MAX);
    }

    if (override_hud_fov > 0.f) {
        if (!m_hud_fov_override_active)
            m_default_hud_fov = psHUD_FOV;

        if (!m_hud_fov_override_active || m_hud_fov_source != source) {
            if (uses_degrees) {
                Msg("* HUD FOV: section [%s] uses absolute [%.2f deg], user value [%.3f]",
                    source->m_sect_name.c_str(), source->m_hud_fov_degrees,
                    m_default_hud_fov);
            } else {
                Msg("* HUD FOV: section [%s] overrides [%.3f] -> [%.3f]",
                    source->m_sect_name.c_str(), m_default_hud_fov, override_hud_fov);
            }
        }

        psHUD_FOV = override_hud_fov;
        m_applied_hud_fov = override_hud_fov;
        m_hud_fov_override_active = true;
        m_hud_fov_source = source;
    } else if (m_hud_fov_override_active) {
        psHUD_FOV = m_default_hud_fov;
        m_applied_hud_fov = 0.f;
        m_hud_fov_override_active = false;
        m_hud_fov_source = NULL;

        Msg("* HUD FOV: restored user.ltx value [%.3f]", psHUD_FOV);
    }

    const float override_viewport_near = source ? source->m_viewport_near : 0.f;

    if (override_viewport_near > 0.f) {
        if (!m_viewport_near_override_active)
            m_default_viewport_near = IE_VIEWPORT_NEAR;

        if (!m_viewport_near_override_active || m_viewport_near_source != source) {
            Msg("* HUD viewport near: section [%s] overrides [%.4f] -> [%.4f]",
                source->m_sect_name.c_str(), m_default_viewport_near,
                override_viewport_near);
        }

        IE_VIEWPORT_NEAR = override_viewport_near;
        m_viewport_near_override_active = true;
        m_viewport_near_source = source;
    } else if (m_viewport_near_override_active) {
        IE_VIEWPORT_NEAR = m_default_viewport_near;
        m_viewport_near_override_active = false;
        m_viewport_near_source = NULL;

        Msg("* HUD viewport near: restored engine_external.ltx value [%.4f]",
            IE_VIEWPORT_NEAR);
    }
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result) {
    Fmatrix ancor_m = m_model->dcast_PKinematics()->LL_GetTransform(m_ancors[attach_slot_idx]);
    result.mul(m_transform, ancor_m);
    result.mulB_43(offset);
}

bool player_hud::inertion_allowed() {
    attachable_hud_item* hi = m_attached_items[0];

    if (!hi)
        return true;

    // Controller-owned HUD item не має CHudItem parent.
    if (hi->m_controller_owned || !hi->m_parent_hud_item)
        return true;

    return hi->m_parent_hud_item->HudInertionEnabled() &&
           hi->m_parent_hud_item->HudInertionAllowed();
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd) {
    if (cmd == 0) {
        if (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item) {
            if (m_attached_items[0]->m_parent_hud_item->GetState() == CHUDState::eIdle) {
                m_attached_items[0]->m_parent_hud_item->PlayAnimIdle();
            }
        }

        if (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item) {
            if (m_attached_items[1]->m_parent_hud_item->GetState() == CHUDState::eIdle) {
                m_attached_items[1]->m_parent_hud_item->PlayAnimIdle();
            }
        }
    } else {
        if (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item) {
            m_attached_items[0]->m_parent_hud_item->OnMovementChanged(cmd);
        }

        if (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item) {
            m_attached_items[1]->m_parent_hud_item->OnMovementChanged(cmd);
        }
    }
}
