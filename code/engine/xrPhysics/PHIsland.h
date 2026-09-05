#pragma once
#ifndef PH_ISLAND_H
#define PH_ISLAND_H

#pragma warning(disable : 4995)
#pragma warning(disable : 4267)
#include "ode/ode/src/objects.h"
#include "ode/ode/src/joint.h"
#pragma warning(default : 4995)
#pragma warning(default : 4267)

#include "ode/include/ode/objects.h"
#include "PhysicsCommon.h"

namespace {
    constexpr int base_shift = 8;
    constexpr int shift_to_variable = base_shift / 2;
    constexpr u8 mask_static = 0xf;
    constexpr u8 stActive = 1 << 0;
    constexpr u8 flPrefereExactIntegration = 1 << 1;
    
    constexpr int JOINTS_LIMIT = 1500;
    constexpr int BODIES_LIMIT = 500;
}

class CPHIslandFlags {
    Flags8 flags;

public:
    CPHIslandFlags() { init(); }

    inline void init() noexcept {
        flags.zero();
        flags.set(stActive, TRUE);
        unmerge();
    }
    
    [[nodiscard]] inline BOOL is_active() const noexcept { 
        return flags.test(stActive << shift_to_variable); 
    }

    inline void set_prefere_exact_integration() noexcept { flags.set(flPrefereExactIntegration, TRUE); }
    inline void uset_prefere_exact_integration() noexcept { flags.set(flPrefereExactIntegration, FALSE); }

    [[nodiscard]] inline BOOL is_exact_integration_prefeared() const noexcept {
        return flags.test(flPrefereExactIntegration << shift_to_variable);
    }

    inline void merge(CPHIslandFlags& aflags) noexcept {
        flags.flags |= aflags.flags.flags & mask_static;
        aflags.flags.set(stActive << shift_to_variable, FALSE);
    }
    
    inline void unmerge() noexcept {
        flags.flags = ((flags.flags & mask_static) << shift_to_variable) | (flags.flags & mask_static);
    }
};

class CPHIsland : public dxWorld {
    CPHIslandFlags m_flags;
    
    dxBody* m_first_body{nullptr};
    dxJoint* m_first_joint{nullptr};
    dxJoint** m_joints_tail{nullptr};
    dxBody** m_bodies_tail{nullptr};
    CPHIsland* m_self_active{this};
    
    int m_nj{0};
    int m_nb{0};

public:
    CPHIsland() = default;
    ~CPHIsland() = default;

    [[nodiscard]] inline bool IsObjGroun() const noexcept { return nb > m_nb; }
    [[nodiscard]] inline bool IsJointGroun() const noexcept { return nj > m_nj; }
    [[nodiscard]] inline bool CheckSize() const noexcept { return nj < JOINTS_LIMIT && nb < BODIES_LIMIT; }
    
    [[nodiscard]] inline int MaxJoints() const noexcept { return JOINTS_LIMIT - nj; }
    [[nodiscard]] inline int MaxJoints(const CPHIsland* island) const noexcept { return MaxJoints() - island->nj; }
    [[nodiscard]] inline int MaxBodies(const CPHIsland* island) const noexcept { return BODIES_LIMIT - nb - island->nb; }

    inline bool CanMerge(const CPHIsland* island, int& MAX_JOINTS) const noexcept {
        MAX_JOINTS = MaxJoints(island);
        return MAX_JOINTS > 0 && ((nb + island->nb) < BODIES_LIMIT);
    }

    [[nodiscard]] inline bool IsActive() const noexcept { return !!m_flags.is_active(); }

    [[nodiscard]] inline dWorldID DWorld() noexcept { return reinterpret_cast<dWorldID>(this); }
    [[nodiscard]] inline dWorldID DActiveWorld() noexcept { return reinterpret_cast<dWorldID>(DActiveIsland()); }
    
    [[nodiscard]] inline CPHIsland* DActiveIsland() noexcept {
        GoActive();
        return m_self_active;
    }
    
    inline void GoActive() noexcept {
        while (!m_self_active->m_flags.is_active()) {
            m_self_active = m_self_active->m_self_active;
        }
    }
    
    inline void Merge(CPHIsland* island) noexcept {
        CPHIsland* first_island = DActiveIsland();
        CPHIsland* second_island = island->DActiveIsland();
        
        if (first_island == second_island) return;

        *(second_island->m_joints_tail) = first_island->firstjoint;
        first_island->firstjoint = second_island->firstjoint;
        if (0 == first_island->nj && 0 != second_island->nj) {
            first_island->m_joints_tail = second_island->m_joints_tail;
        }

        *(second_island->m_bodies_tail) = first_island->firstbody;
        first_island->firstbody = second_island->firstbody;

        first_island->nj += second_island->nj;
        first_island->nb += second_island->nb;
        
        VERIFY(!(*(first_island->m_bodies_tail)));
        VERIFY(!(*(first_island->m_joints_tail)));
        VERIFY(!((!(first_island->nj)) && (first_island->firstjoint)));
        
        second_island->m_self_active = first_island;
        m_flags.merge(second_island->m_flags);
    }
    
    inline void Unmerge() noexcept {
        firstjoint = m_first_joint;
        firstbody = m_first_body;
        if (!m_nj) {
            m_joints_tail = &firstjoint;
            *m_joints_tail = nullptr;
        } else {
            firstjoint->tome = reinterpret_cast<dObject**>(&firstjoint);
        }
        
        *m_joints_tail = nullptr;
        *m_bodies_tail = nullptr;
        
        m_flags.unmerge();
        m_self_active = this;
        nj = m_nj;
        nb = m_nb;
    }
    
    inline void Init() noexcept {
        m_flags.init();
        m_nj = nj = 0;
        m_nb = nb = 0;
        m_first_joint = firstjoint = nullptr;
        m_first_body = firstbody = nullptr;
        m_joints_tail = &firstjoint;
        m_bodies_tail = &firstbody;
        m_self_active = this;
    }
    
    inline void AddBody(dxBody* body) {
        VERIFY2(m_nj == nj && m_nb == nb && m_flags.is_active(), "can not remove/add during processing phase");
        dWorldAddBody(DWorld(), body);
        m_first_body = body;
        if (m_nb == 0) {
            m_bodies_tail = reinterpret_cast<dxBody**>(&body->next);
        }
        m_nb++;
    }
    
    inline void RemoveBody(dxBody* body) {
        VERIFY2(m_nj == nj && m_nb == nb && m_flags.is_active(), "can not remove/add during processing phase");
        if (m_first_body == body) m_first_body = static_cast<dxBody*>(body->next);
        
        if (m_bodies_tail == reinterpret_cast<dxBody**>(&(body->next))) {
            m_bodies_tail = reinterpret_cast<dxBody**>(body->tome);
        }
        
        dWorldRemoveBody(this, body);
        m_nb--;
    }
    
    inline void AddJoint(dxJoint* joint) {
        VERIFY2(m_nj == nj && m_nb == nb && m_flags.is_active(), "can not remove/add during processing phase");
        dWorldAddJoint(DWorld(), joint);
        m_first_joint = joint;
        if (!m_nj) {
            VERIFY(joint->next == nullptr);
            m_joints_tail = reinterpret_cast<dxJoint**>(&(joint->next));
        }
        m_nj++;
    }

    inline void ConnectJoint(dxJoint* joint) {
        if (!nj) {
            m_joints_tail = reinterpret_cast<dxJoint**>(&(joint->next));
            VERIFY(!firstjoint);
        }
        dWorldAddJoint(DWorld(), joint);
        VERIFY(!(*(m_joints_tail)));
    }

    inline void DisconnectJoint(dxJoint* joint) { dWorldRemoveJoint(DWorld(), joint); }
    inline void ConnectBody(dxBody* body) { dWorldAddBody(DWorld(), body); }
    inline void DisconnectBody(dxBody* body) { dWorldRemoveBody(DWorld(), body); }
    
    inline void RemoveJoint(dxJoint* joint) {
        VERIFY2(m_nj == nj && m_nb == nb && m_flags.is_active(), "can not remove/add during processing phase");
        if (m_first_joint == joint) m_first_joint = static_cast<dxJoint*>(joint->next);
        
        if (m_joints_tail == reinterpret_cast<dxJoint**>(&(joint->next))) {
            m_joints_tail = reinterpret_cast<dxJoint**>(joint->tome);
        }
        dWorldRemoveJoint(DWorld(), joint);
        VERIFY(!*(m_joints_tail));
        m_nj--;
    }
    
    inline void SetPrefereExactIntegration() noexcept { m_flags.set_prefere_exact_integration(); }
    
    void Step(dReal step);
    void Enable();
    void Repair();
};

#endif // PH_ISLAND_H