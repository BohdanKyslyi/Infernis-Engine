////////////////////////////////////////////////////////////////////////////
//	Module 		: eatable_item.cpp
//	Created 	: 24.03.2003
//	Author		: Yuri Dobronravin
//	Description : Eatable item
//  Modified 	: 25.08.2026
//	by  		: Bohdan «Infernis» Kyslyi
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "eatable_item.h"
#include "xrmessages.h"
#include "physic_item.h"
#include "Level.h"
#include "entity_alive.h"
#include "EntityCondition.h"
#include "InventoryOwner.h"
#include <string_table.h>
#include "inventory.h"
#include "gameobject.h"
#include "ai_object_location.h"

CEatableItem::CEatableItem() {
    m_iPortionsNum = -1;
    m_iTotalPortionsNum = -1;

    m_portion_state = NULL;

    m_portion_name = NULL;
    m_portion_name_short = NULL;
    m_portion_description = NULL;

    m_portion_weight = 0.0f;
    m_portion_cost_factor = -1.0f;

    m_base_visual = NULL;
    m_portion_visual = NULL;

    m_trash_object = NULL;
    m_trash_count = 0;

    m_physic_item = 0;
}

CEatableItem::~CEatableItem() {}

DLL_Pure* CEatableItem::_construct() {
    m_physic_item = smart_cast<CPhysicItem*>(this);
    return (inherited::_construct());
}

void CEatableItem::Load(LPCSTR section) {
    inherited::Load(section);

    m_base_visual = object().cNameVisual();
    m_portion_visual = m_base_visual;

    m_iTotalPortionsNum = pSettings->r_s32(section, "eat_portions_num");

    m_iPortionsNum = m_iTotalPortionsNum;

    VERIFY(m_iTotalPortionsNum < 10000);

    UpdatePortionState();
}

void SpawnConsumableTrash(CEntityAlive* entity_alive, const shared_str& section, u32 count) {
    if (!entity_alive)
        return;

    if (!section.size())
        return;

    if (count == 0)
        return;

    //
    // Same authority rule as next_section / next_random.
    //
    if (!OnServer())
        return;

    if (!pSettings->section_exist(section.c_str())) {
        Msg("! Eatable: trash section [%s] does not exist", section.c_str());

        return;
    }

    CGameObject* owner = smart_cast<CGameObject*>(entity_alive);

    if (!owner) {
        Msg("! Eatable: trash owner is not CGameObject");
        return;
    }

    //
    // Spawn slightly in front of the actor and above the ground,
    // rather than directly inside the actor's collision capsule.
    //
    Fvector position = owner->Position();

    Fvector direction = owner->Direction();

    direction.y = 0.0f;
    direction.normalize_safe();

    position.mad(direction, 0.35f);

    position.y += 0.6f;

    const u32 level_vertex_id = owner->ai_location().level_vertex_id();

    for (u32 i = 0; i < count; ++i) {
        Level().spawn_item(section.c_str(), position, level_vertex_id,

                           //
                           // 0xffff = no parent.
                           // Trash is spawned into the world,
                           // NOT into actor inventory.
                           //
                           u16(-1),

                           false);
    }

#ifdef DEBUG

    Msg("* Eatable trash spawned: [%s] x[%u]", section.c_str(), count);

#endif
}

void CEatableItem::ApplyTrashConfig(LPCSTR section) {
    if (!section || !section[0]) {
        return;
    }

    if (!pSettings->section_exist(section))
        return;

    //
    // trash_object
    //
    if (pSettings->line_exist(section, "trash_object")) {
        LPCSTR value = pSettings->r_string(section, "trash_object");

        //
        // Explicit state override:
        //
        // trash_object = none
        //
        if (!value || !value[0] || !xr_strcmp(value, "none")) {
            m_trash_object = NULL;
        } else {
            if (!pSettings->section_exist(value)) {
                Msg("! Eatable: invalid trash_object [%s] in [%s]", value, section);

                m_trash_object = NULL;
            } else {
                m_trash_object = value;
            }
        }
    }

    //
    // trash_count
    //
    if (pSettings->line_exist(section, "trash_count")) {
        m_trash_count = pSettings->r_u32(section, "trash_count");
    }
}

void CEatableItem::UpdateTrashState() {
    //
    // Default:
    //
    // no trash object;
    // if object appears, default count = 1.
    //
    m_trash_object = NULL;
    m_trash_count = 1;

    if (!m_section_id.size())
        return;

    //
    // Priority #1:
    // physical item section.
    //
    ApplyTrashConfig(m_section_id.c_str());

    //
    // Priority #2:
    // animated use section.
    //
    // [conserva]
    // hud = conserva_hud_model
    //
    if (pSettings->line_exist(m_section_id.c_str(), "hud")) {
        LPCSTR use_section = pSettings->r_string(m_section_id.c_str(), "hud");

        if (use_section && use_section[0] && pSettings->section_exist(use_section)) {
            ApplyTrashConfig(use_section);
        }
    }

    //
    // Priority #3:
    // current portion_state.
    //
    // Highest priority.
    //
    if (m_portion_state.size()) {
        ApplyTrashConfig(m_portion_state.c_str());
    }

    if (!m_trash_object.size())
        m_trash_count = 0;

#ifdef DEBUG

    if (HasTrash()) {
        Msg("* Eatable trash recipe [%s]: [%s] x[%u]", m_section_id.c_str(), m_trash_object.c_str(),
            m_trash_count);
    }

#endif
}

BOOL CEatableItem::net_Spawn(CSE_Abstract* DC) {
    if (!inherited::net_Spawn(DC))
        return FALSE;

    return TRUE;
};

void CEatableItem::save(NET_Packet& packet) {
    inherited::save(packet);

    u16 portions = 0;

    if (m_iPortionsNum > 0)
        portions = static_cast<u16>(m_iPortionsNum);

    packet.w_u16(portions);
}

void CEatableItem::load(IReader& packet) {
    inherited::load(packet);

    m_iPortionsNum = static_cast<int>(packet.r_u16());

    //
    // Protect against invalid/outdated data.
    //
    if (m_iPortionsNum < 0)
        m_iPortionsNum = 0;

    if (m_iTotalPortionsNum > 0 && m_iPortionsNum > m_iTotalPortionsNum) {
        m_iPortionsNum = m_iTotalPortionsNum;
    }
    UpdatePortionState();
}

bool CEatableItem::Useful() const {
    if (!inherited::Useful())
        return false;

    //ïðîâåðèòü íå âñå ëè åùå ñúåäåíî
    if (m_iPortionsNum == 0)
        return false;

    return true;
}

void CEatableItem::OnH_A_Independent() {
    inherited::OnH_A_Independent();
    if (!Useful()) {
        if (object().Local() && OnServer())
            object().DestroyObject();
    }
}

void CEatableItem::OnH_B_Independent(bool just_before_destroy) {
    if (!Useful()) {
        object().setVisible(FALSE);
        object().setEnabled(FALSE);
        if (m_physic_item)
            m_physic_item->m_ready_to_destroy = true;
    }
    inherited::OnH_B_Independent(just_before_destroy);
}

bool CEatableItem::UseBy(CEntityAlive* entity_alive) {
    SMedicineInfluenceValues V;
    V.Load(m_physic_item->cNameSect());

    CInventoryOwner* IO = smart_cast<CInventoryOwner*>(entity_alive);
    R_ASSERT(IO);
    R_ASSERT(m_pInventory == IO->m_inventory);
    R_ASSERT(object().H_Parent()->ID() == entity_alive->ID());

    entity_alive->conditions().ApplyInfluence(V, m_physic_item->cNameSect());

    for (u8 i = 0; i < (u8)eBoostMaxCount; i++) {
        if (pSettings->line_exist(m_physic_item->cNameSect().c_str(),
                                  ef_boosters_section_names[i])) {
            SBooster B;
            B.Load(m_physic_item->cNameSect(), (EBoostParams)i);
            entity_alive->conditions().ApplyBooster(B, m_physic_item->cNameSect());
        }
    }

    if (m_iPortionsNum > 0)
        --m_iPortionsNum;
    else
        m_iPortionsNum = 0;

    //
    // IMPORTANT:
    //
    // m_next_sections still belongs to the state
    // which has just been used.
    //
    // Do this BEFORE UpdatePortionState(), otherwise
    // the last use could resolve another state's recipe.
    //
    if (Empty()) {
        //
        // Guaranteed outcome.
        //
        SpawnNextSections(entity_alive);

        //
        // Random outcome pools.
        //
        SpawnNextRandom(entity_alive);
    }

    //
    // Now resolve the post-use state.
    //
    UpdatePortionState();

    if (m_pInventory)
        m_pInventory->InvalidateState();

    return true;
}

u32 CEatableItem::Cost() const {
    u32 res = inherited::Cost();

    //
    // Explicit portion-state price modifier.
    //
    if (m_portion_cost_factor >= 0.0f) {
        return iFloor(res * m_portion_cost_factor + 0.5f);
    }

    //
    // Default behaviour:
    // price follows remaining portions.
    //
    if (m_iTotalPortionsNum <= 1)
        return res;

    if (m_iPortionsNum <= 0)
        return 0;

    return iFloor(res * (float)m_iPortionsNum / (float)m_iTotalPortionsNum + 0.5f);
}

void CEatableItem::UpdatePortionState() {
    //
    // First restore original/base inventory values.
    //
    m_portion_state = NULL;

    m_portion_name = m_name;

    m_portion_name_short = m_nameShort;

    m_portion_description = m_Description;

    m_portion_weight = m_weight;

    m_portion_cost_factor = -1.0f;

    m_portion_grid_rect = inherited::GetInvGridRect();

    //
    // Default world model.
    //
    m_portion_visual = m_base_visual;

    if (m_section_id.size() == 0) {
        ApplyPortionVisual();
        UpdateOutcomeRecipes();
        UpdateTrashState();
        return;
    }

    const int use_index = PortionIndex();

    //
    // Resolve portion state with backwards fallback.
    //
    for (int index = use_index; index >= 1; --index) {
        string64 line;

        if (index == 1) {
            xr_strcpy(line, "portion_state");
        } else {
            xr_sprintf(line, "portion_state_%d", index);
        }

        if (!pSettings->line_exist(m_section_id.c_str(), line)) {
            continue;
        }

        LPCSTR state = pSettings->r_string(m_section_id.c_str(), line);

        if (!state || !state[0])
            continue;

        if (!pSettings->section_exist(state)) {
            Msg("! Eatable: invalid portion state [%s] for [%s]", state, m_section_id.c_str());

            continue;
        }

        m_portion_state = state;

        break;
    }

    //
    // No state -> original inventory parameters remain active.
    //
    if (!m_portion_state.size()) {
        ApplyPortionVisual();
        UpdateOutcomeRecipes();
        UpdateTrashState();
        return;
    }

    LPCSTR state = m_portion_state.c_str();

    //
    // Name.
    //
    if (pSettings->line_exist(state, "inv_name")) {
        m_portion_name = CStringTable().translate(pSettings->r_string(state, "inv_name"));
    }

    //
    // Short name.
    //
    if (pSettings->line_exist(state, "inv_name_short")) {
        m_portion_name_short =
            CStringTable().translate(pSettings->r_string(state, "inv_name_short"));
    }

    //
    // Description.
    //
    if (pSettings->line_exist(state, "description")) {
        m_portion_description = CStringTable().translate(pSettings->r_string(state, "description"));
    }

    //
    // Weight.
    //
    if (pSettings->line_exist(state, "inv_weight")) {
        m_portion_weight = pSettings->r_float(state, "inv_weight");

        if (m_portion_weight < 0.0f)
            m_portion_weight = 0.0f;
    }

    //
    // Inventory icon.
    //
    // Important:
    // x2/y2 in this Irect are used by X-Ray
    // as grid WIDTH/HEIGHT, not final coordinates.
    //
    if (pSettings->line_exist(state, "inv_grid_x")) {
        m_portion_grid_rect.x1 = pSettings->r_s32(state, "inv_grid_x");
    }

    if (pSettings->line_exist(state, "inv_grid_y")) {
        m_portion_grid_rect.y1 = pSettings->r_s32(state, "inv_grid_y");
    }

    if (pSettings->line_exist(state, "inv_grid_width")) {
        m_portion_grid_rect.x2 = pSettings->r_s32(state, "inv_grid_width");
    }

    if (pSettings->line_exist(state, "inv_grid_height")) {
        m_portion_grid_rect.y2 = pSettings->r_s32(state, "inv_grid_height");
    }

    //
    // Optional price override.
    //
    if (pSettings->line_exist(state, "cost_factor")) {
        m_portion_cost_factor = pSettings->r_float(state, "cost_factor");

        if (m_portion_cost_factor < 0.0f)
            m_portion_cost_factor = 0.0f;
    }

    //
    // World visual.
    //
    if (pSettings->line_exist(state, "visual")) {
        string_path visual_name;

        xr_strcpy(visual_name, pSettings->r_string(state, "visual"));

        //
        // Match CObject::Load() behaviour.
        //
        if (strext(visual_name))
            *strext(visual_name) = 0;

        xr_strlwr(visual_name);

        m_portion_visual = visual_name;
    }

#ifdef DEBUG
    Msg("* Eatable portion state [%s]: use [%d], weight [%.3f], cost factor [%.3f]",
        m_portion_state.c_str(), PortionIndex(), m_portion_weight, m_portion_cost_factor);
#endif
    ApplyPortionVisual();
    UpdateOutcomeRecipes();
    UpdateTrashState();
}

void CEatableItem::ApplyPortionVisual() {
    if (!m_portion_visual.size())
        return;

    //
    // Don't recreate the render model
    // if the current visual is already correct.
    //
    if (object().cNameVisual() == m_portion_visual) {
        return;
    }

    Msg("* Eatable visual changed: [%s] -> [%s]", object().cNameSect().c_str(),
        m_portion_visual.c_str());

    object().cNameVisual_set(m_portion_visual);
}

LPCSTR CEatableItem::NameItem() {
    if (m_portion_name.size())
        return m_portion_name.c_str();

    return inherited::NameItem();
}

LPCSTR CEatableItem::NameShort() {
    if (m_portion_name_short.size())
        return m_portion_name_short.c_str();

    return inherited::NameShort();
}

shared_str CEatableItem::ItemDescription() {
    if (m_portion_description.size())
        return m_portion_description;

    return inherited::ItemDescription();
}

Irect CEatableItem::GetInvGridRect() const {
    if (m_iTotalPortionsNum > 0)
        return m_portion_grid_rect;

    return inherited::GetInvGridRect();
}

float CEatableItem::Weight() const {
    if (m_iTotalPortionsNum > 0)
        return m_portion_weight;

    return inherited::Weight();
}

bool CEatableItem::HasNextSections(LPCSTR section) const {
    if (!section || !section[0])
        return false;

    if (!pSettings->section_exist(section))
        return false;

    return pSettings->line_exist(section, "next_section");
}

void CEatableItem::LoadNextSectionsFrom(LPCSTR section) {
    m_next_sections.clear();

    if (!section || !section[0])
        return;

    if (!pSettings->section_exist(section))
        return;

    for (u32 index = 1;; ++index) {
        string64 line;

        if (index == 1) {
            xr_strcpy(line, "next_section");
        } else {
            xr_sprintf(line, "next_section_%u", index);
        }

        //
        // Numbering must be contiguous.
        //
        if (!pSettings->line_exist(section, line)) {
            break;
        }

        LPCSTR value = pSettings->r_string(section, line);

        if (!value || !value[0])
            continue;

        const int param_count = _GetItemCount(value);

        if (param_count < 1)
            continue;

        string256 item_section;
        string64 count_string;

        _GetItem(value, 0, item_section);

        if (!item_section[0])
            continue;

        //
        // Validate destination section immediately,
        // not when player actually opens the pack.
        //
        if (!pSettings->section_exist(item_section)) {
            Msg("! Eatable: invalid next_section [%s] in [%s]", item_section, section);

            continue;
        }

        u32 item_count = 1;

        if (param_count >= 2) {
            _GetItem(value, 1, count_string);

            const int parsed_count = atoi(count_string);

            if (parsed_count <= 0) {
                Msg("! Eatable: invalid next_section count [%s] in [%s]", count_string, section);

                continue;
            }

            item_count = static_cast<u32>(parsed_count);
        }

        SNextSectionItem entry;

        entry.section = item_section;

        entry.count = item_count;

        m_next_sections.push_back(entry);
    }
}

void CEatableItem::UpdateNextSections() {
    m_next_sections.clear();

    //
    // Default recipe from the real item section.
    //
    if (HasNextSections(m_section_id.c_str())) {
        LoadNextSectionsFrom(m_section_id.c_str());
    }

    //
    // Portion state has priority.
    //
    if (m_portion_state.size() && HasNextSections(m_portion_state.c_str())) {
        LoadNextSectionsFrom(m_portion_state.c_str());
    }

#ifdef DEBUG
    if (!m_next_sections.empty()) {
        Msg("* Eatable next-section recipe [%s]: [%u] entries", m_section_id.c_str(),
            (u32)m_next_sections.size());
    }
#endif
}

void CEatableItem::SpawnNextSections(CEntityAlive* entity_alive) {
#ifdef DEBUG
    Msg("* Eatable SpawnNextSections called for [%s], entries [%u]", m_section_id.c_str(),
        (u32)m_next_sections.size());
#endif
    if (m_next_sections.empty())
        return;

    //
    // Spawn must happen on server.
    //
    if (!OnServer())
        return;

    CGameObject* owner = smart_cast<CGameObject*>(entity_alive);

    if (!owner) {
        Msg("! Eatable: next_section owner is not CGameObject");

        return;
    }

    const Fvector& position = owner->Position();

    const u32 level_vertex_id = owner->ai_location().level_vertex_id();

    const u16 parent_id = owner->ID();

    for (u32 entry_id = 0; entry_id < m_next_sections.size(); ++entry_id) {
        const SNextSectionItem& entry = m_next_sections[entry_id];

        for (u32 i = 0; i < entry.count; ++i) {
            Level().spawn_item(entry.section.c_str(), position, level_vertex_id, parent_id, false);
        }

        Msg("* Eatable spawned: [%s] x[%u]", entry.section.c_str(), entry.count);
    }
}

void CEatableItem::UpdateOutcomeRecipes() {
    UpdateNextSections();
    UpdateNextRandom();
    UpdateTrashState();
}

bool CEatableItem::HasNextRandom(LPCSTR section) const {
    if (!section || !section[0])
        return false;

    if (!pSettings->section_exist(section))
        return false;

    return pSettings->line_exist(section, "next_random");
}

bool CEatableItem::LoadRandomPool(LPCSTR pool_section, SNextRandomPool& pool) {
    pool.section = pool_section;

    pool.bundles.clear();

    if (!pool_section || !pool_section[0]) {
        return false;
    }

    if (!pSettings->section_exist(pool_section)) {
        Msg("! Eatable: random pool section [%s] does not exist", pool_section);

        return false;
    }

    for (u32 index = 1;; ++index) {
        string64 line;

        if (index == 1) {
            xr_strcpy(line, "option");
        } else {
            xr_sprintf(line, "option_%u", index);
        }

        if (!pSettings->line_exist(pool_section, line)) {
            break;
        }

        LPCSTR value = pSettings->r_string(pool_section, line);

        if (!value || !value[0]) {
            continue;
        }

        const int param_count = _GetItemCount(value);

        if (param_count <= 0)
            continue;

        SNextRandomBundle bundle;

        //
        // Special shorthand:
        //
        // option = medkit
        //
        // means medkit x1.
        //
        if (param_count == 1) {
            string256 item_section;

            _GetItem(value, 0, item_section);

            if (!pSettings->section_exist(item_section)) {
                Msg("! Eatable: invalid random item [%s] in pool [%s]", item_section, pool_section);

                continue;
            }

            SNextSectionItem item;

            item.section = item_section;

            item.count = 1;

            bundle.items.push_back(item);
        } else {
            //
            // Bundle syntax is:
            //
            // section,count,section,count,...
            //
            if ((param_count % 2) != 0) {
                Msg("! Eatable: invalid random bundle [%s:%s] - expected section,count pairs",
                    pool_section, line);

                continue;
            }

            bool valid_bundle = true;

            for (int param = 0; param < param_count; param += 2) {
                string256 item_section;
                string64 count_string;

                _GetItem(value, param, item_section);

                _GetItem(value, param + 1, count_string);

                if (!pSettings->section_exist(item_section)) {
                    Msg("! Eatable: invalid random item [%s] in pool [%s]", item_section,
                        pool_section);

                    valid_bundle = false;
                    break;
                }

                const int parsed_count = atoi(count_string);

                if (parsed_count <= 0) {
                    Msg("! Eatable: invalid random item count [%s] in pool [%s]", count_string,
                        pool_section);

                    valid_bundle = false;
                    break;
                }

                SNextSectionItem item;

                item.section = item_section;

                item.count = static_cast<u32>(parsed_count);

                bundle.items.push_back(item);
            }

            if (!valid_bundle)
                continue;
        }

        if (!bundle.items.empty()) {
            pool.bundles.push_back(bundle);
        }
    }

    return !pool.bundles.empty();
}

void CEatableItem::LoadNextRandomFrom(LPCSTR section) {
    m_next_random_pools.clear();

    if (!section || !section[0]) {
        return;
    }

    if (!pSettings->section_exist(section))
        return;

    for (u32 index = 1;; ++index) {
        string64 line;

        if (index == 1) {
            xr_strcpy(line, "next_random");
        } else {
            xr_sprintf(line, "next_random_%u", index);
        }

        if (!pSettings->line_exist(section, line)) {
            break;
        }

        LPCSTR pool_section = pSettings->r_string(section, line);

        if (!pool_section || !pool_section[0]) {
            continue;
        }

        SNextRandomPool pool;

        if (LoadRandomPool(pool_section, pool)) {
            m_next_random_pools.push_back(pool);
        }
    }
}

void CEatableItem::UpdateNextRandom() {
    m_next_random_pools.clear();

    //
    // Base item recipe.
    //
    if (HasNextRandom(m_section_id.c_str())) {
        LoadNextRandomFrom(m_section_id.c_str());
    }

    //
    // Portion state overrides base recipe.
    //
    if (m_portion_state.size() && HasNextRandom(m_portion_state.c_str())) {
        LoadNextRandomFrom(m_portion_state.c_str());
    }

#ifdef DEBUG

    if (!m_next_random_pools.empty()) {
        Msg("* Eatable random recipe [%s]: [%u] pools", m_section_id.c_str(),
            (u32)m_next_random_pools.size());
    }

#endif
}

void CEatableItem::SpawnNextRandom(CEntityAlive* entity_alive) {
    if (m_next_random_pools.empty())
        return;

    if (!OnServer())
        return;

    CGameObject* owner = smart_cast<CGameObject*>(entity_alive);

    if (!owner)
        return;

    const Fvector& position = owner->Position();

    const u32 level_vertex_id = owner->ai_location().level_vertex_id();

    const u16 parent_id = owner->ID();

    for (u32 pool_id = 0; pool_id < m_next_random_pools.size(); ++pool_id) {
        const SNextRandomPool& pool = m_next_random_pools[pool_id];

        if (pool.bundles.empty())
            continue;

        //
        // Uniform random selection.
        //
        // Every option has exactly the same chance.
        //
        const u32 selected = Random.randI((u32)pool.bundles.size());

        const SNextRandomBundle& bundle = pool.bundles[selected];

#ifdef DEBUG

        Msg("* Eatable random pool [%s]: selected option [%u/%u]", pool.section.c_str(),
            selected + 1, (u32)pool.bundles.size());

#endif

        for (u32 item_id = 0; item_id < bundle.items.size(); ++item_id) {
            const SNextSectionItem& item = bundle.items[item_id];

            for (u32 count = 0; count < item.count; ++count) {
                Level().spawn_item(item.section.c_str(), position, level_vertex_id, parent_id,
                                   false);
            }

#ifdef DEBUG

            Msg("* Eatable random spawned: [%s] x[%u]", item.section.c_str(), item.count);

#endif
        }
    }
}