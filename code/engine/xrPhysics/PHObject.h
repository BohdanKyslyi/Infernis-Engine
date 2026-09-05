#pragma once
#ifndef CPHOBJECT
#define CPHOBJECT

#include "../xrcdb/ispatial.h"
#include "PHItemList.h"
#include "PHIsland.h"
#include <immintrin.h>

typedef u32 CLClassBits;
typedef u32 CLBits;
class ISpatial;
using qResultVec = xr_vector<ISpatial*>;
class CPHObject;
class CPHUpdateObject;
class CPHMoveStorage;
class CPHSynchronize;

typedef void CollideCallback(CPHObject* obj1, CPHObject* obj2, dGeomID o1, dGeomID o2);

#ifdef DEBUG
class IPhysicsShellHolder;
#endif

class CPHObject : public ISpatial {
#ifdef DEBUG
    friend struct SPHObjDBGDraw;
#endif
    DECLARE_PHLIST_ITEM(CPHObject)

    Flags8 m_flags;

    enum {
        st_activated = (1 << 0),
        st_freezed = (1 << 1),
        st_dirty = (1 << 2),
        st_net_interpolation = (1 << 3),
        fl_ray_motions = (1 << 4),
        st_recently_deactivated = (1 << 5)
    };

    CPHIsland m_island;
    CLBits m_collide_bits;
    u8 m_check_count;
    _flags<CLClassBits> m_collide_class_bits;

public:
    enum ECastType { tpNotDefinite, tpShell, tpCharacter, tpStaticShell };

protected:
    Fvector AABB;

protected:
    [[nodiscard]] virtual dGeomID dSpacedGeom() = 0;
    virtual void get_spatial_params() = 0;
    virtual void spatial_register() override;
    
    inline void SetRayMotions() { m_flags.set(fl_ray_motions, TRUE); }
    inline void UnsetRayMotions() { m_flags.set(fl_ray_motions, FALSE); }
    inline void SetPrefereExactIntegration() { m_island.SetPrefereExactIntegration(); }

    [[nodiscard]] CPHObject* SelfPointer() { return this; }

public:
    [[nodiscard]] inline BOOL IsRayMotion() const { return m_flags.test(fl_ray_motions); }
    
    inline void IslandReinit() { m_island.Unmerge(); }
    inline void IslandStep(dReal step) { m_island.Step(step); }
    inline void MergeIsland(CPHObject* obj) { m_island.Merge(&obj->m_island); }
    
    [[nodiscard]] inline CPHIsland& Island() { return m_island; }
    [[nodiscard]] inline dWorldID DActiveWorld() { return m_island.DActiveWorld(); }
    [[nodiscard]] inline CPHIsland* DActiveIsland() { return m_island.DActiveIsland(); }
    [[nodiscard]] inline dWorldID DWorld() { return m_island.DWorld(); }

    virtual void FreezeContent();
    virtual void UnFreezeContent();
    virtual void EnableObject(CPHObject* obj);
    virtual bool DoCollideObj();
    virtual bool step_single(dReal step);
    
    void reinit_single();
    void step_prediction(float time);
    void step(float time);
    
    virtual void PhDataUpdate(dReal step) = 0;
    virtual void PhTune(dReal step) = 0;
    virtual void spatial_move() override;
    virtual void InitContact(dContact* c, bool& do_collide, u16 /*material_idx_1*/, u16 /*material_idx_2*/) = 0;
    virtual void CutVelocity(float l_limit, float a_limit) {};

    void Freeze();
    void UnFreeze();
    
    [[nodiscard]] inline bool IsFreezed() const { return !!(m_flags.test(st_freezed)); }
    
    inline void NetInterpolationON() { m_flags.set(st_net_interpolation, TRUE); }
    inline void NetInterpolationOFF() { m_flags.set(st_net_interpolation, FALSE); }
    [[nodiscard]] inline bool NetInterpolation() const { return !!(m_flags.test(st_net_interpolation)); }
    
    [[nodiscard]] virtual u16 get_elements_number() = 0;
    [[nodiscard]] virtual CPHSynchronize* get_element_sync(u16 element) = 0;

    CPHObject();
    virtual ~CPHObject() = default;

    void activate();
    [[nodiscard]] inline bool is_active() const { return !!m_flags.test(st_activated); }
    void deactivate();
    void put_in_recently_deactivated();
    void remove_from_recently_deactivated();
    void check_recently_deactivated();
    void collision_disable();
    void collision_enable();
    
    virtual void ClearRecentlyDeactivated() {}
    virtual void Collide();
    virtual void near_callback(CPHObject* obj) {}
    virtual void RMotionsQuery(qResultVec& res) {}
    
    [[nodiscard]] virtual CPHMoveStorage* MoveStorage() { return nullptr; }
    [[nodiscard]] virtual ECastType CastType() { return tpNotDefinite; }
    
    virtual void vis_update_activate() {}
    virtual void vis_update_deactivate() {}

#ifdef DEBUG
    [[nodiscard]] virtual IPhysicsShellHolder* ref_object() = 0;
#endif

    [[nodiscard]] inline CLBits& collide_bits() { return m_collide_bits; }
    [[nodiscard]] inline _flags<CLClassBits>& collide_class_bits() { return m_collide_class_bits; }
    [[nodiscard]] inline const CLBits& collide_bits() const { return m_collide_bits; }
    [[nodiscard]] inline const _flags<CLClassBits>& collide_class_bits() const { return m_collide_class_bits; }
    
    void CollideDynamics();
};

DEFINE_PHITEM_LIST(CPHObject, PH_OBJECT_STORAGE, PH_OBJECT_I)

#endif // CPHOBJECT