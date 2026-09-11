#include "stdafx.h"
#include "player_hud_legs.h"

#include "Actor.h"
#include "Level.h"
#include "actor_defs.h"
#include "../xrEngine/bone.h"
#include "xrRender/Kinematics.h"

namespace {
constexpr LPCSTR HUD_EXTENSIONS_SECTION = "hud_extensions";
constexpr LPCSTR LEGS_ENABLED_LINE = "enable_actor_legs";
constexpr LPCSTR LEGS_VISUAL_LINE = "legs_visual";
constexpr LPCSTR LEGS_FORWARD_OFFSET_LINE = "legs_fwd_offset";
constexpr LPCSTR LEGS_VERTICAL_OFFSET_LINE = "legs_y_offset";

bool ReadActorLegsEnabled() {
    return pSettings && pSettings->section_exist(HUD_EXTENSIONS_SECTION) &&
           pSettings->line_exist(HUD_EXTENSIONS_SECTION, LEGS_ENABLED_LINE) &&
           pSettings->r_bool(HUD_EXTENSIONS_SECTION, LEGS_ENABLED_LINE);
}

float ReadLegsOffset(const shared_str& player_hud_section, LPCSTR line) {
    if (pSettings->line_exist(player_hud_section.c_str(), line))
        return pSettings->r_float(player_hud_section.c_str(), line);

    if (pSettings->section_exist(HUD_EXTENSIONS_SECTION) &&
        pSettings->line_exist(HUD_EXTENSIONS_SECTION, line)) {
        return pSettings->r_float(HUD_EXTENSIONS_SECTION, line);
    }

    return 0.f;
}
} // namespace

CActorLegsController::CActorLegsController()
    : m_model(NULL), m_forward_offset(0.f), m_vertical_offset(0.f),
      m_reported_skeleton_mismatch(false) {
    m_transform.identity();
}

CActorLegsController::~CActorLegsController() { DestroyModel(); }

void CActorLegsController::DestroyModel() {
    if (m_model) {
        IRenderVisual* visual = m_model->dcast_RenderVisual();
        ::Render->model_Delete(visual);
        m_model = NULL;
    }

    m_visual_name = NULL;
    m_reported_skeleton_mismatch = false;
}

bool CActorLegsController::VisualExists(LPCSTR visual_name) const {
    if (!visual_name || !visual_name[0])
        return false;

    string_path file_name;
    if (strext(visual_name))
        xr_strcpy(file_name, visual_name);
    else
        strconcat(sizeof(file_name), file_name, visual_name, ".ogf");

    string_path resolved_name;
    return !!FS.exist(visual_name) || !!FS.exist(resolved_name, "$level$", file_name) ||
           !!FS.exist(resolved_name, "$game_meshes$", file_name);
}

void CActorLegsController::Load(const shared_str& player_hud_section) {
    if (!ReadActorLegsEnabled()) {
        DestroyModel();
        return;
    }

    LPCSTR visual_name = NULL;
    if (pSettings->line_exist(player_hud_section.c_str(), LEGS_VISUAL_LINE)) {
        visual_name = pSettings->r_string(player_hud_section.c_str(), LEGS_VISUAL_LINE);
    } else if (pSettings->line_exist(HUD_EXTENSIONS_SECTION, LEGS_VISUAL_LINE)) {
        // Optional common model for projects that do not need outfit-specific legs.
        visual_name = pSettings->r_string(HUD_EXTENSIONS_SECTION, LEGS_VISUAL_LINE);
    }

    if (!visual_name || !visual_name[0] || !stricmp(visual_name, "none")) {
        DestroyModel();
        return;
    }

    m_forward_offset = ReadLegsOffset(player_hud_section, LEGS_FORWARD_OFFSET_LINE);
    m_vertical_offset = ReadLegsOffset(player_hud_section, LEGS_VERTICAL_OFFSET_LINE);

    if (m_model && m_visual_name.equal(visual_name))
        return;

    DestroyModel();

    if (!VisualExists(visual_name)) {
        Msg("! Actor legs: visual [%s] from HUD section [%s] was not found", visual_name,
            player_hud_section.c_str());
        return;
    }

    IRenderVisual* visual = ::Render->model_Create(visual_name);
    IKinematics* kinematics = smart_cast<IKinematics*>(visual);
    if (!kinematics) {
        if (visual)
            ::Render->model_Delete(visual);

        Msg("! Actor legs: visual [%s] from HUD section [%s] is not skeletal", visual_name,
            player_hud_section.c_str());
        return;
    }

    m_model = kinematics;
    m_visual_name = visual_name;
    m_model->CalculateBones_Invalidate();
    m_model->CalculateBones(TRUE);

    Msg("* Actor legs: loaded visual [%s] for HUD section [%s]", visual_name,
        player_hud_section.c_str());
}

bool CActorLegsController::SyncBones(CActor* actor) {
    if (!actor || !actor->Visual() || !m_model)
        return false;

    IKinematics* actor_model = actor->Visual()->dcast_PKinematics();
    if (!actor_model)
        return false;

    actor_model->CalculateBones(TRUE);
    m_model->CalculateBones_Invalidate();
    m_model->CalculateBones(TRUE);

    u16 synchronized_bones = 0;
    IKinematics::accel* legs_bones = m_model->LL_Bones();
    for (IKinematics::accel::const_iterator it = legs_bones->begin(); it != legs_bones->end();
         ++it) {
        const u16 actor_bone = actor_model->LL_BoneID(it->first);
        if (actor_bone == BI_NONE)
            continue;

        const u16 legs_bone = it->second;
        CBoneInstance& legs_instance = m_model->LL_GetBoneInstance(legs_bone);
        legs_instance.mTransform.set(actor_model->LL_GetBoneInstance(actor_bone).mTransform);
        legs_instance.mRenderTransform.mul_43(legs_instance.mTransform,
                                              m_model->LL_GetData(legs_bone).m2b_transform);
        ++synchronized_bones;
    }

    if (!synchronized_bones) {
        if (!m_reported_skeleton_mismatch) {
            Msg("! Actor legs: visual [%s] has no bones matching the actor skeleton",
                m_visual_name.c_str());
            m_reported_skeleton_mismatch = true;
        }
        return false;
    }

    // Rebuild the upper branch after copying the lower-body transforms. This
    // keeps child transforms coherent for legs models based on a full actor
    // skeleton, while still allowing the mesh itself to contain only the legs.
    const u16 spine_bone = m_model->LL_BoneID("bip01_spine");
    if (spine_bone != BI_NONE) {
        CBoneData& spine_data = m_model->LL_GetData(spine_bone);
        const u16 parent_bone = spine_data.GetParentID();
        if (parent_bone != BI_NONE)
            m_model->Bone_Calculate(&spine_data, &m_model->LL_GetTransform(parent_bone));
    }

    return true;
}

void CActorLegsController::Render() {
    if (!m_model || !g_pGameLevel)
        return;

    CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
    if (!actor || !actor->g_Alive() || !actor->HUDview() || actor->Holder() ||
        (actor->MovingState() & mcClimb)) {
        return;
    }

    if (!SyncBones(actor))
        return;

    m_transform.set(actor->XFORM());
    if (!fis_zero(m_forward_offset)) {
        Fvector forward = m_transform.k;
        forward.y = 0.f;
        forward.normalize_safe();
        m_transform.c.mad(forward, m_forward_offset);
    }
    m_transform.c.y += m_vertical_offset;

    const BOOL was_hud = ::Render->get_HUD();
    ::Render->set_HUD(FALSE);
    ::Render->set_Transform(&m_transform);
    ::Render->add_Visual(m_model->dcast_RenderVisual());
    ::Render->set_HUD(was_hud);
}
