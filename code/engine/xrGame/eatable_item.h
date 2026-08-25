#pragma once

#include "inventory_item.h"

class CPhysicItem;
class CEntityAlive;

//
// Spawn physical waste produced by a consumable.
//
// Unlike next_section this object has NO parent
// and therefore appears directly in the world.
//
void SpawnConsumableTrash(CEntityAlive* entity_alive, const shared_str& section, u32 count);

struct SNextSectionItem {
    shared_str section;
    u32 count;

    SNextSectionItem() : count(1) {}
};

class CEatableItem : public CInventoryItem {

private:
    typedef CInventoryItem inherited;

protected:
    CPhysicItem* m_physic_item;

public:
    CEatableItem();
    virtual ~CEatableItem();
    virtual DLL_Pure* _construct();
    virtual CEatableItem* cast_eatable_item() { return this; }

    virtual void Load(LPCSTR section);
    virtual bool Useful() const;

    virtual BOOL net_Spawn(CSE_Abstract* DC);

    virtual void save(NET_Packet& output_packet) override;
    virtual void load(IReader& input_packet) override;

    virtual void OnH_B_Independent(bool just_before_destroy);
    virtual void OnH_A_Independent();
    virtual bool UseBy(CEntityAlive* npc);

    virtual u32 Cost() const override;

    virtual LPCSTR NameItem() override;
    virtual LPCSTR NameShort() override;
    virtual shared_str ItemDescription() override;

    virtual Irect GetInvGridRect() const override;
    virtual float Weight() const override;

    virtual bool Empty() { return PortionsNum() == 0; };

    const shared_str& TrashObject() const { return m_trash_object; }

    u32 TrashCount() const { return m_trash_count; }

    bool HasTrash() const { return m_trash_object.size() && m_trash_count > 0; }

    //
    // Current number of remaining portions.
    //
    int PortionsNum() const { return m_iPortionsNum; }

    //
    // Initial number of portions loaded from config.
    //
    int TotalPortions() const { return m_iTotalPortionsNum; }

    //
    // 1-based index of the current use.
    //
    // Example for eat_portions_num = 3:
    //
    // remaining 3 -> use #1
    // remaining 2 -> use #2
    // remaining 1 -> use #3
    //
    int PortionIndex() const {
        if (m_iTotalPortionsNum <= 0)
            return 1;

        int index = m_iTotalPortionsNum - m_iPortionsNum + 1;

        if (index < 1)
            index = 1;

        if (index > m_iTotalPortionsNum)
            index = m_iTotalPortionsNum;

        return index;
    }

    const shared_str& PortionStateSection() const { return m_portion_state; }

    void UpdatePortionState();

    void ApplyPortionVisual();

protected:
    int m_iPortionsNum;
    int m_iTotalPortionsNum;

    shared_str m_portion_state;

    shared_str m_portion_name;
    shared_str m_portion_name_short;
    shared_str m_portion_description;

    Irect m_portion_grid_rect;

    float m_portion_weight;
    float m_portion_cost_factor;

    shared_str m_base_visual;
    shared_str m_portion_visual;

    //
    // Physical waste produced by the current use/state.
    //
    shared_str m_trash_object;
    u32 m_trash_count;

    void UpdateTrashState();
    void ApplyTrashConfig(LPCSTR section);

    struct SNextRandomBundle {
        xr_vector<SNextSectionItem> items;
    };

    struct SNextRandomPool {
        shared_str section;
        xr_vector<SNextRandomBundle> bundles;
    };

    protected:
    //
    // Native item outcome recipe.
    //
    xr_vector<SNextSectionItem> m_next_sections;

    //
    // Random outcome pools.
    //
    xr_vector<SNextRandomPool> m_next_random_pools;

    void UpdateNextSections();
    void LoadNextSectionsFrom(LPCSTR section);
    void SpawnNextSections(CEntityAlive* owner);
    bool HasNextSections(LPCSTR section) const;

    void UpdateNextRandom();
    void LoadNextRandomFrom(LPCSTR section);
    bool LoadRandomPool(LPCSTR pool_section, SNextRandomPool& pool);
    void SpawnNextRandom(CEntityAlive* owner);

    bool HasNextRandom(LPCSTR section) const;

    void UpdateOutcomeRecipes();
};


