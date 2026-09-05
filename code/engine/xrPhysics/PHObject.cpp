#include "stdafx.h"
#include "Physics.h"
#include "PHObject.h"
#include "PHWorld.h"
#include "PHMoveStorage.h"
#include "dRayMotions.h"
#include "PHCollideValidator.h"
#include "console_vars.h"

#ifdef DEBUG
#include "debug_output.h"
#endif

extern CPHWorld* ph_world;

CPHObject::CPHObject() : ISpatial(g_SpatialSpacePhysic) {
    m_flags.flags = 0;
    spatial.type |= STYPE_PHYSIC;
    m_island.Init();
    m_check_count = 0;
    CPHCollideValidator::InitObject(*this);
}

void CPHObject::activate() {
    R_ASSERT2(dSpacedGeom(), "trying to activate destroyed or not created object!");
    if (m_flags.test(st_activated)) return;
    if (m_flags.test(st_freezed)) {
        UnFreeze();
        return;
    }
    if (m_flags.test(st_recently_deactivated))
        remove_from_recently_deactivated();
        
    ph_world->AddObject(this);
    vis_update_activate();
    m_flags.set(st_activated, TRUE);
}

void CPHObject::EnableObject(CPHObject* obj) { activate(); }

void CPHObject::deactivate() {
    if (!m_flags.test(st_activated)) return;
    VERIFY2(m_island.IsActive(), "can not do it during processing");
    ph_world->RemoveObject(PH_OBJECT_I(this));
    vis_update_deactivate();
    m_flags.set(st_activated, FALSE);
}

void CPHObject::put_in_recently_deactivated() {
    VERIFY(!m_flags.test(st_activated) && !m_flags.test(st_freezed));
    if (m_flags.test(st_recently_deactivated)) return;
    m_check_count = u8(ph_console::ph_tri_clear_disable_count);
    m_flags.set(st_recently_deactivated, TRUE);
    ph_world->AddRecentlyDisabled(this);
}

void CPHObject::remove_from_recently_deactivated() {
    if (!m_flags.test(st_recently_deactivated)) return;
    m_check_count = 0;
    m_flags.set(st_recently_deactivated, FALSE);
    ph_world->RemoveFromRecentlyDisabled(PH_OBJECT_I(this));
}

void CPHObject::check_recently_deactivated() {
    if (m_check_count == 0) {
        ClearRecentlyDeactivated();
        remove_from_recently_deactivated();
    } else {
        m_check_count--;
    }
}

void CPHObject::spatial_move() {
    get_spatial_params();
    ISpatial::spatial_move();
    m_flags.set(st_dirty, TRUE);
}

void CPHObject::Collide() {
    if (m_flags.test(fl_ray_motions)) {
        CPHMoveStorage* tracers = MoveStorage();
        if (tracers) {
            for (auto I = tracers->begin(); I != tracers->end(); ++I) {
                const Fvector *from = nullptr, *to = nullptr;
                Fvector dir;
                I.Positions(from, to);
                
                if (from->x == -dInfinity) continue;
                
                dir.sub(*to, *from);
                float magnitude = dir.magnitude();
                if (magnitude < EPS) continue;
                
                dir.mul(1.f / magnitude);
                g_SpatialSpacePhysic->q_ray(ph_world->r_spatial, 0, STYPE_PHYSIC, *from, dir, magnitude);

#ifdef DEBUG
                if (debug_output().ph_dbg_draw_mask().test(phDbgDrawRayMotions)) {
                    debug_output().DBG_OpenCashedDraw();
                    debug_output().DBG_DrawLine(*from, Fvector().add(*from, Fvector().mul(dir, magnitude)), D3DCOLOR_XRGB(0, 255, 0));
                    debug_output().DBG_ClosedCashedDraw(30000);
                }
#endif
                qResultVec& result = ph_world->r_spatial;
                for (ISpatial* spatial_obj : result) {
                    CPHObject* obj2 = static_cast<CPHObject*>(spatial_obj);
                    if (obj2 == this || !obj2->m_flags.test(st_dirty)) continue;
                    
                    dGeomID motion_ray = ph_world->GetMotionRayGeom();
                    dGeomRayMotionSetGeom(motion_ray, I.dGeom());
                    dGeomRayMotionsSet(motion_ray, (const dReal*)from, (const dReal*)&dir, magnitude);
                    NearCallback(this, obj2, motion_ray, obj2->dSpacedGeom());
                }
            }
        }
    }
    CollideDynamics();
    if (CPHCollideValidator::DoCollideStatic(*this))
        CollideStatic(dSpacedGeom(), this);
        
    m_flags.set(st_dirty, FALSE);
}

// AVX2 + SoA Broadphase culling
void CPHObject::CollideDynamics() {
    g_SpatialSpacePhysic->q_box(ph_world->r_spatial, 0, STYPE_PHYSIC, spatial.sphere.P, AABB);
    qResultVec& result = ph_world->r_spatial;
    
    if (result.empty()) return;

    const size_t count = result.size();
    
    // Якщо об'єктів достатньо для векторизації (>= 8 для AVX2)
    if (count >= 8) {
        size_t mem_size = count * sizeof(float);
        float* posX = static_cast<float*>(xr_malloc(mem_size));
        float* posY = static_cast<float*>(xr_malloc(mem_size));
        float* posZ = static_cast<float*>(xr_malloc(mem_size));
        float* rads = static_cast<float*>(xr_malloc(mem_size));
        CPHObject** objs = static_cast<CPHObject**>(xr_malloc(count * sizeof(CPHObject*)));

        size_t valid_count = 0;

        // Конвертація з AoS у SoA
        for (ISpatial* spatial_obj : result) {
            CPHObject* obj2 = static_cast<CPHObject*>(spatial_obj);
            if (obj2 != this && obj2->m_flags.test(st_dirty)) {
                posX[valid_count] = obj2->spatial.sphere.P.x;
                posY[valid_count] = obj2->spatial.sphere.P.y;
                posZ[valid_count] = obj2->spatial.sphere.P.z;
                rads[valid_count] = obj2->spatial.sphere.R;
                objs[valid_count] = obj2;
                valid_count++;
            }
        }

        __m256 my_x = _mm256_set1_ps(spatial.sphere.P.x);
        __m256 my_y = _mm256_set1_ps(spatial.sphere.P.y);
        __m256 my_z = _mm256_set1_ps(spatial.sphere.P.z);
        __m256 my_r = _mm256_set1_ps(spatial.sphere.R);

        size_t i = 0;
        for (; i + 8 <= valid_count; i += 8) {
            // Unaligned load для безпеки (якщо xr_malloc не гарантує 32-байтне вирівнювання)
            __m256 ox = _mm256_loadu_ps(&posX[i]);
            __m256 oy = _mm256_loadu_ps(&posY[i]);
            __m256 oz = _mm256_loadu_ps(&posZ[i]);
            __m256 orad = _mm256_loadu_ps(&rads[i]);

            __m256 dx = _mm256_sub_ps(my_x, ox);
            __m256 dy = _mm256_sub_ps(my_y, oy);
            __m256 dz = _mm256_sub_ps(my_z, oz);
            
            // Дистанція у квадраті: dx*dx + dy*dy + dz*dz
            __m256 dist_sq = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy)), _mm256_mul_ps(dz, dz));
            
            // Сума радіусів у квадраті
            __m256 sum_rad = _mm256_add_ps(my_r, orad);
            __m256 rad_sq = _mm256_mul_ps(sum_rad, sum_rad);

            // Порівняння: dist_sq <= rad_sq
            __m256 cmp_mask = _mm256_cmp_ps(dist_sq, rad_sq, _CMP_LE_OQ);
            int mask = _mm256_movemask_ps(cmp_mask);

            // Якщо є хоч одне пересічення сфер, викликаємо важку перевірку ODE
            if (mask != 0) {
                for (int j = 0; j < 8; ++j) {
                    if ((mask & (1 << j)) && CPHCollideValidator::DoCollide(*this, *objs[i + j])) {
                        NearCallback(this, objs[i + j], dSpacedGeom(), objs[i + j]->dSpacedGeom());
                    }
                }
            }
        }

        // Обробка залишку (Tail processing)
        for (; i < valid_count; ++i) {
            if (CPHCollideValidator::DoCollide(*this, *objs[i])) {
                NearCallback(this, objs[i], dSpacedGeom(), objs[i]->dSpacedGeom());
            }
        }

        xr_free(posX);
        xr_free(posY);
        xr_free(posZ);
        xr_free(rads);
        xr_free(objs);
    } 
    else {
        for (ISpatial* spatial_obj : result) {
            CPHObject* obj2 = static_cast<CPHObject*>(spatial_obj);
            if (obj2 == this || !obj2->m_flags.test(st_dirty)) continue;
            
            if (CPHCollideValidator::DoCollide(*this, *obj2)) {
                NearCallback(this, obj2, dSpacedGeom(), obj2->dSpacedGeom());
            }
        }
    }
}

void CPHObject::reinit_single() {
    IslandReinit();
    for (ISpatial* spatial_obj : ph_world->r_spatial) {
        static_cast<CPHObject*>(spatial_obj)->IslandReinit();
    }
    ph_world->r_spatial.clear();
    dJointGroupEmpty(ContactGroup);
    ContactFeedBacks.empty();
    ContactEffectors.empty();
}

void CPHObject::step_prediction(float time) {
    // general idea:
    // perform normal step by time as local as possible for this object then return world to the pervious state
}

bool CPHObject::step_single(dReal step) {
    CollideDynamics();
    bool ret = !m_island.IsObjGroun();
    if (ret) {
        IslandStep(step);
        reinit_single();
        spatial_move();
        CollideDynamics();
        ret = !m_island.IsObjGroun();
    }
    reinit_single();
    return ret;
}

void CPHObject::step(float time) {
    ph_world->r_spatial.clear();
    reinit_single();
    Collide();
    IslandStep(time);
    reinit_single();
}

bool CPHObject::DoCollideObj() {
    CollideDynamics();
    bool ret = m_island.IsObjGroun();
    reinit_single();
    return ret;
}

void CPHObject::FreezeContent() {
    R_ASSERT(!m_flags.test(st_freezed));
    m_flags.set(st_freezed, TRUE);
    m_flags.set(st_activated, FALSE);
    vis_update_deactivate();
}

void CPHObject::UnFreezeContent() {
    R_ASSERT(m_flags.test(st_freezed));
    m_flags.set(st_freezed, FALSE);
    m_flags.set(st_activated, TRUE);
    vis_update_activate();
}

void CPHObject::spatial_register() {
    get_spatial_params();
    ISpatial::spatial_register();
    m_flags.set(st_dirty, TRUE);
}

void CPHObject::collision_disable() { ISpatial::spatial_unregister(); }
void CPHObject::collision_enable() { ISpatial::spatial_register(); }

void CPHObject::Freeze() {
    if (!m_flags.test(st_activated)) return;
    ph_world->RemoveObject(this);
    ph_world->AddFreezedObject(this);
    FreezeContent();
}

void CPHObject::UnFreeze() {
    if (!m_flags.test(st_freezed)) return;
    UnFreezeContent();
    ph_world->RemoveFreezedObject(this);
    ph_world->AddObject(this);
}

CPHUpdateObject::CPHUpdateObject() { b_activated = false; }

void CPHUpdateObject::Activate() {
    if (b_activated) return;
    ph_world->AddUpdateObject(this);
    b_activated = true;
}

void CPHUpdateObject::Deactivate() {
    if (!b_activated) return;
    ph_world->RemoveUpdateObject(this);
    b_activated = false;
}