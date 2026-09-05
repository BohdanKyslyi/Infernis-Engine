#pragma once
#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "PhysicsCommon.h"
#include "ExtendedGeom.h"
#include "mathutilsode.h"
#include "../xrEngine/iphysicsgeometry.h"
#include <immintrin.h> 

// SIMD-оптимізований макрос множення 3x3 матриць
inline void dMULTIPLY3_333(dReal* A, const dReal* B, const dReal* C) {
    __m128 c_row0 = _mm_loadu_ps(C);
    __m128 c_row1 = _mm_loadu_ps(C + 4);
    __m128 c_row2 = _mm_loadu_ps(C + 8);

    for (int i = 0; i < 3; ++i) {
        __m128 b_x = _mm_set1_ps(B[i * 4 + 0]);
        __m128 b_y = _mm_set1_ps(B[i * 4 + 1]);
        __m128 b_z = _mm_set1_ps(B[i * 4 + 2]);

        __m128 res = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b_x, c_row0), _mm_mul_ps(b_y, c_row1)), _mm_mul_ps(b_z, c_row2));
        _mm_storeu_ps(A + i * 4, res);
    }
}

class CGameObject;
class CPHObject;

class XRPHYSICS_API CODEGeom : public IPhysicsGeometry {
protected:
    dGeomID m_geom_transform{nullptr};
    u16 m_bone_id;
    Flags16 m_flags;

public:
    CODEGeom();
    virtual ~CODEGeom();

    [[nodiscard]] virtual float volume() = 0;
    virtual void get_mass(dMass& m) = 0; // unit density mass
    void get_mass(dMass& m, const Fvector& ref_point, float density);
    void get_mass(dMass& m, const Fvector& ref_point);
    void add_self_mass(dMass& m, const Fvector& ref_point);
    void add_self_mass(dMass& m, const Fvector& ref_point, float density);
    
    void get_local_center_bt(Fvector& center);  
    void get_global_center_bt(Fvector& center); 
    void get_local_form_bt(Fmatrix& form);      
    virtual void get_xform(Fmatrix& form) const;
    
#ifdef DEBUG
    virtual void dbg_draw(float scale, u32 color, Flags32 flags) const;
#endif

    virtual void get_Box(Fmatrix& form, Fvector& sz) const;
    [[nodiscard]] virtual bool collide_fluids() const;
    void set_static_ref_form(const Fmatrix& form);
    
    virtual void get_max_area_dir_bt(Fvector& dir) = 0;
    [[nodiscard]] virtual float radius() = 0;
    virtual void get_Extensions(const Fvector& axis, float center_prg, float& lo_ext, float& hi_ext) const = 0;

    void clear_cashed_tries();

    [[nodiscard]] IC dGeomID geom() { return dGeomTransformGetGeom(m_geom_transform); }
    [[nodiscard]] IC dGeomID geometry_transform() { return m_geom_transform; }
    [[nodiscard]] IC dGeomID geometry() { return m_geom_transform ? (geom() ? geom() : m_geom_transform) : nullptr; }
    [[nodiscard]] IC dGeomID geometry_bt() { return is_transformed_bt() ? geom() : geometry_transform(); }
    
    [[nodiscard]] IC const dGeomID geom() const { return dGeomTransformGetGeom(m_geom_transform); }
    [[nodiscard]] IC const dGeomID geometry_transform() const { return m_geom_transform; }
    [[nodiscard]] IC const dGeomID geometry() const { return m_geom_transform ? (geom() ? geom() : m_geom_transform) : nullptr; }
    [[nodiscard]] IC const dGeomID geometry_bt() const { return is_transformed_bt() ? geom() : geometry_transform(); }

    [[nodiscard]] ICF static bool is_transform(dGeomID g) { return dGeomGetClass(g) == dGeomTransformClass; }
    [[nodiscard]] IC bool is_transformed_bt() const { return is_transform(m_geom_transform); }
    [[nodiscard]] IC u16& element_position() { return dGeomGetUserData(geometry())->element_position; }
    
    [[nodiscard]] virtual const Fvector& local_center() = 0;
    virtual void get_local_form(Fmatrix& form) = 0;
    virtual void set_local_form(const Fmatrix& form) = 0;
    void set_local_form_bt(const Fmatrix& xform);

    void set_body(dBodyID body);
    inline void set_bone_id(u16 id) { m_bone_id = id; }
    [[nodiscard]] inline u16 bone_id() const { return m_bone_id; }
    inline void set_shape_flags(const Flags16& _flags) { m_flags = _flags; }
    
    void add_to_space(dSpaceID space);
    void remove_from_space(dSpaceID space);
    void set_material(u16 ul_material);
    void set_contact_cb(ContactCallbackFun* ccb);
    void set_obj_contact_cb(ObjectContactCallbackFun* occb);
    void add_obj_contact_cb(ObjectContactCallbackFun* occb);
    void remove_obj_contact_cb(ObjectContactCallbackFun* occb);
    void set_callback_data(void* cd);
    [[nodiscard]] void* get_callback_data();
    void set_ref_object(IPhysicsShellHolder* ro);
    void set_ph_object(CPHObject* o);

protected:
    void init();
    void get_final_tx_bt(const dReal*& p, const dReal*& R, dReal* bufV, dReal* bufM) const;
    virtual dGeomID create() = 0;

public:
    static void get_final_tx(dGeomID g, const dReal*& p, const dReal*& R, dReal* bufV, dReal* bufM);
    void build(const Fvector& ref_point);
    virtual void set_build_position(const Fvector& ref_point); 
    void clear_motion_history(bool set_unspecified);
    void move_local_basis(const Fmatrix& inv_new_mul_old);
    void destroy();
};

class CBoxGeom : public CODEGeom {
    using inherited = CODEGeom;
    Fobb m_box;

public:
    explicit CBoxGeom(const Fobb& box);
    [[nodiscard]] virtual float volume() override;
    [[nodiscard]] virtual float radius() override;
    virtual void get_Extensions(const Fvector& axis, float center_prg, float& lo_ext, float& hi_ext) const override;
    virtual void get_max_area_dir_bt(Fvector& dir) override;
    virtual void get_mass(dMass& m) override;
    [[nodiscard]] virtual const Fvector& local_center() override;
    virtual void get_local_form(Fmatrix& form) override;
    virtual void set_local_form(const Fmatrix& form) override;
    virtual dGeomID create() override;
    virtual void set_build_position(const Fvector& ref_point) override;
    
    void set_size(const Fvector& half_size);
    void get_size(Fvector& half_size) const;

private:
#ifdef DEBUG
    virtual void dbg_draw(float scale, u32 color, Flags32 flags) const override;
#endif
};

class CSphereGeom : public CODEGeom {
    using inherited = CODEGeom;
    Fsphere m_sphere;

public:
    explicit CSphereGeom(const Fsphere& sphere);
    [[nodiscard]] virtual float volume() override;
    [[nodiscard]] virtual float radius() override;
    virtual void get_Extensions(const Fvector& axis, float center_prg, float& lo_ext, float& hi_ext) const override;
    virtual void get_max_area_dir_bt(Fvector& dir) override {};
    virtual void get_mass(dMass& m) override;
    [[nodiscard]] virtual const Fvector& local_center() override;
    virtual void get_local_form(Fmatrix& form) override;
    virtual void set_local_form(const Fmatrix& form) override;
    virtual dGeomID create() override;
    virtual void set_build_position(const Fvector& ref_point) override;

private:
#ifdef DEBUG
    virtual void dbg_draw(float scale, u32 color, Flags32 flags) const override;
#endif
};

class CCylinderGeom : public CODEGeom {
    using inherited = CODEGeom;
    Fcylinder m_cylinder;

public:
    explicit CCylinderGeom(const Fcylinder& cyl);
    [[nodiscard]] virtual float volume() override;
    [[nodiscard]] virtual float radius() override;
    virtual void get_Extensions(const Fvector& axis, float center_prg, float& lo_ext, float& hi_ext) const override;
    virtual void get_max_area_dir_bt(Fvector& dir) override {};
    [[nodiscard]] virtual const Fvector& local_center() override;
    virtual void get_mass(dMass& m) override;
    virtual void get_local_form(Fmatrix& form) override;
    virtual void set_local_form(const Fmatrix& form) override;
    virtual dGeomID create() override;
    virtual void set_build_position(const Fvector& ref_point) override;
    void set_radius(float r);

private:
#ifdef DEBUG
    virtual void dbg_draw(float scale, u32 color, Flags32 flags) const override;
#endif
};

#endif // GEOMETRY_H