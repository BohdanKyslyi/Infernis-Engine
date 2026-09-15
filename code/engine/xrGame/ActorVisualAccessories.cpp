#include "stdafx.h"
#include "Actor.h"
#include "Inventory.h"
#include "NoirInventorySlots.h"
#include "xrRender/Kinematics.h"
#include "../xrEngine/vis_common.h"
#include "../xrEngine/bone.h"

void CActor::ClearVisualAccessory(VisualAccessory& accessory) {
    if (accessory.visual)
        ::Render->model_Delete(accessory.visual);
    accessory.name = nullptr;
    accessory.bone_name = nullptr;
    accessory.owner_kinematics = nullptr;
    accessory.bone_map.clear();
    accessory.bone_id = u16(-1);
}

void CActor::ClearVisualAccessories() {
    ClearVisualAccessory(m_helmet_visual);
    ClearVisualAccessory(m_backpack_visual);
}

void CActor::UpdateVisualAccessory(VisualAccessory& accessory, CInventoryItem* item) {
    LPCSTR section = item ? item->object().cNameSect().c_str() : nullptr;
    if (!section || !pSettings || !pSettings->line_exist(section, "actor_visual")) {
        ClearVisualAccessory(accessory);
        return;
    }

    LPCSTR configured = pSettings->r_string(section, "actor_visual");
    if (!configured || !configured[0] || xr_strlen(configured) + 5 >= sizeof(string_path)) {
        ClearVisualAccessory(accessory);
        return;
    }

    string_path model;
    if (strext(configured))
        xr_strcpy(model, sizeof(model), configured);
    else
        strconcat(sizeof(model), model, configured, ".ogf");

    LPCSTR bone = pSettings->line_exist(section, "actor_visual_bone")
                      ? pSettings->r_string(section, "actor_visual_bone")
                      : "";
    if (accessory.name == model && accessory.bone_name == bone)
        return;

    ClearVisualAccessory(accessory);
    accessory.name = model;
    accessory.bone_name = bone;
    string_path found;
    if (!FS.exist(found, "$game_meshes$", model)) {
        Msg("! actor_visual '%s' for '%s' not found in game meshes", model, section);
        return;
    }

    accessory.visual = ::Render->model_Create(model);
}

void CActor::SyncVisualAccessories() {
    if (m_visual_accessories_frozen || !Visual())
        return;

    UpdateVisualAccessory(m_helmet_visual, inventory().ItemFromSlot(HELMET_SLOT));
    UpdateVisualAccessory(m_backpack_visual, NoirInventorySlots::BackpackEnabled()
                                                ? inventory().ItemFromSlot(BACKPACK_SLOT)
                                                : nullptr);
}

void CActor::RenderVisualAccessory(VisualAccessory& accessory, LPCSTR default_bone) {
    if (!accessory.visual)
        return;

    IKinematics* actor_bones = Visual()->dcast_PKinematics();
    if (!actor_bones)
        return;
    actor_bones->CalculateBones(TRUE);

    IKinematics* item_bones = accessory.visual->dcast_PKinematics();
    if (accessory.owner_kinematics != actor_bones) {
        accessory.owner_kinematics = actor_bones;
        accessory.bone_map.clear();
        accessory.bone_id = u16(-1);

        if (item_bones && !accessory.bone_name.size()) {
            // A skinned wearable uses the actor's current animation or ragdoll pose.
            bool compatible = item_bones->LL_BoneCount() != 0;
            for (u16 i = 0; compatible && i < item_bones->LL_BoneCount(); ++i) {
                LPCSTR name = item_bones->LL_BoneName_dbg(i);
                u16 bone = name ? actor_bones->LL_BoneID(name) : u16(-1);
                if (bone >= actor_bones->LL_BoneCount())
                    compatible = false;
                else
                    accessory.bone_map.push_back(bone);
            }
            if (!compatible) {
                accessory.bone_map.clear();
                Msg("! actor_visual '%s' has bones missing from actor visual '%s'",
                    accessory.name.c_str(), cNameVisual().c_str());
            }
        } else {
            LPCSTR name = accessory.bone_name.size() ? accessory.bone_name.c_str() : default_bone;
            accessory.bone_id = actor_bones->LL_BoneID(name);
            if (accessory.bone_id >= actor_bones->LL_BoneCount())
                Msg("! actor_visual '%s': actor bone '%s' is missing", accessory.name.c_str(), name);
        }
    }

    if (accessory.bone_id < actor_bones->LL_BoneCount()) {
        accessory.transform.mul_43(XFORM(),
                                   actor_bones->LL_GetBoneInstance(accessory.bone_id).mTransform);
        ::Render->set_Transform(&accessory.transform);
    } else if (item_bones && !accessory.bone_map.empty()) {
        item_bones->CalculateBones_Invalidate();
        item_bones->CalculateBones(TRUE);
        for (u16 i = 0; i < accessory.bone_map.size(); ++i) {
            const CBoneInstance& source = actor_bones->LL_GetBoneInstance(accessory.bone_map[i]);
            CBoneInstance& target = item_bones->LL_GetBoneInstance(i);
            target.mTransform = source.mTransform;
            target.mRenderTransform = source.mRenderTransform;
        }
        vis_data& bounds = accessory.visual->getVisData();
        bounds.box.merge(Visual()->getVisData().box);
        bounds.box.getsphere(bounds.sphere.P, bounds.sphere.R);
        ::Render->set_Transform(&XFORM());
    } else {
        return;
    }

    ::Render->add_Visual(accessory.visual);
}
