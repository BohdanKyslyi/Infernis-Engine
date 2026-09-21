#include "stdafx.h"
#include "RepairKit.h"

#include "GameObject.h"
#include "Level.h"
#include "entity_alive.h"

CRepairKit::CRepairKit()
    : m_target_id(u16(-1)),
      m_restore_condition(0.5f),
      m_min_condition(0.4f),
      m_max_condition(0.9f),
      m_unlimited_usage(false) {}

void CRepairKit::Load(LPCSTR section) {
    inherited::Load(section);

    m_restore_condition =
        READ_IF_EXISTS(pSettings, r_float, section, "restore_condition", 0.5f);
    m_min_condition =
        READ_IF_EXISTS(pSettings, r_float, section, "repair_min_condition", 0.4f);
    m_max_condition =
        READ_IF_EXISTS(pSettings, r_float, section, "repair_max_condition", 0.9f);
    m_unlimited_usage =
        !!READ_IF_EXISTS(pSettings, r_bool, section, "unlimited_usage", false);

    clamp(m_restore_condition, 0.f, 1.f);
    clamp(m_min_condition, 0.f, 1.f);
    clamp(m_max_condition, m_min_condition, 1.f);
}

CInventoryItem* CRepairKit::RepairTarget() const {
    if (m_target_id == u16(-1))
        return NULL;

    return smart_cast<CInventoryItem*>(Level().Objects.net_Find(m_target_id));
}

void CRepairKit::SetRepairTarget(CInventoryItem* target) {
    m_target_id = target ? target->object_id() : u16(-1);
}

bool CRepairKit::CanRepair(const CInventoryItem* target) const {
    if (!target || target->object_id() == object_id() || !m_pInventory ||
        target->m_pInventory != m_pInventory) {
        return false;
    }

    const float condition = target->GetCondition();
    if (condition < m_min_condition || condition >= m_max_condition)
        return false;

    return target->IsRepairableBy(CInventoryItem::object().cNameSect().c_str()) &&
           target->HasRepairMaterials();
}

bool CRepairKit::UseAllowed() const {
    return Useful() && CanRepair(RepairTarget());
}

bool CRepairKit::UseBy(CEntityAlive* entity_alive) {
    CInventoryItem* target = RepairTarget();
    if (!entity_alive || !CanRepair(target)) {
        m_target_id = u16(-1);
        return false;
    }

    target->ConsumeRepairMaterials();
    target->ChangeCondition(m_restore_condition);

    const int previous_portions = m_iPortionsNum;
    const bool used = inherited::UseBy(entity_alive);
    if (used && m_unlimited_usage) {
        m_iPortionsNum = previous_portions;
        UpdatePortionState();
    }

    m_target_id = u16(-1);
    return used;
}
