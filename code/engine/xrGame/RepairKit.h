#pragma once

#include "eatable_item_object.h"

class CRepairKit : public CEatableItemObject {
    using inherited = CEatableItemObject;

public:
    CRepairKit();

    virtual CRepairKit* cast_repair_kit() override { return this; }
    virtual void Load(LPCSTR section) override;
    virtual bool UseBy(CEntityAlive* entity_alive) override;

    bool CanRepair(const CInventoryItem* target) const;
    bool UseAllowed() const;
    void SetRepairTarget(CInventoryItem* target);
    CInventoryItem* RepairTarget() const;

private:
    u16 m_target_id;
    float m_restore_condition;
    float m_min_condition;
    float m_max_condition;
    bool m_unlimited_usage;
};
