#pragma once
#ifndef PH_JOINT
#define PH_JOINT

#include "PhysicsShell.h"
#include "ode/include/ode/common.h"
#include "physics_scripted.h"
#include <immintrin.h> // SIMD

class CPHJointDestroyInfo;

class CPHJoint : public CPhysicsJoint, public cphysics_scripted {
    u16 m_bone_id;
    CPHElement* pFirst_element;
    CPHElement* pSecond_element;
    CODEGeom* pFirstGeom;

    CPHShell* pShell;
    dJointID m_joint;
    dJointID m_joint1;
    CPhysicsJoint** m_back_ref;
    CPHJointDestroyInfo* m_destroy_info;
    float m_erp; // joint erp
    float m_cfm; // joint cfm

    struct alignas(16) SPHAxis {
        float high;        // high limit
        float low;         // law limit
        float zero;        // zero angle position
        float erp;         // limit erp
        float cfm;         // limit cfm
        eVs vs;            // coordinate system
        float force;       // max force
        float velocity;    // velocity to achieve
        Fvector direction; // axis direction

        inline void set_limits(float h, float l) {
            high = h;
            low = l;
        }
        inline void set_direction(const Fvector& v) { direction.set(v); }
        inline void set_direction(const float x, const float y, const float z) { direction.set(x, y, z); }
        inline void set_param(const float e, const float c) {
            erp = e;
            cfm = c;
        }
        void set_sd_factors(float sf, float df, enumType jt);
        SPHAxis();
    };

    xr_vector<SPHAxis> axes;
    Fvector anchor;
    eVs vs_anchor;

    void CreateBall();
    void CreateHinge();
    void CreateHinge2();
    void CreateFullControl();
    void CreateSlider();
    void LimitAxisNum(int& axis_num);
    void SetForceActive(const int axis_num);
    void SetVelocityActive(const int axis_num);
    void SetLimitsActive(int axis_num);
    
    void CalcAxis(int ax_num, Fvector& axis, float& lo, float& hi, const Fmatrix& first_matrix, const Fmatrix& second_matrix);
    void CalcAxis(int ax_num, Fvector& axis, float& lo, float& hi, const Fmatrix& first_matrix, const Fmatrix& second_matrix, const Fmatrix& rotate);
    
    [[nodiscard]] virtual u16 GetAxesNumber() override;
    virtual void SetAxisSDfactors(float spring_factor, float damping_factor, int axis_num) override;
    virtual void SetJointSDfactors(float spring_factor, float damping_factor) override;
    virtual void SetJointSDfactorsActive();
    virtual void SetLimitsSDfactorsActive();
    virtual void SetAxisSDfactorsActive(int axis_num);
    virtual void SetJointFudgefactorActive(float factor) override;

    virtual void SetAxis(const SPHAxis& axis, const int axis_num);
    virtual void SetAnchor(const Fvector& position) override { SetAnchor(position.x, position.y, position.z); }
    virtual void SetAnchorVsFirstElement(const Fvector& position) override { SetAnchorVsFirstElement(position.x, position.y, position.z); }
    virtual void SetAnchorVsSecondElement(const Fvector& position) override { SetAnchorVsSecondElement(position.x, position.y, position.z); }

    virtual void SetAxisDir(const Fvector& orientation, const int axis_num) override { SetAxisDir(orientation.x, orientation.y, orientation.z, axis_num); }
    virtual void SetAxisDirVsFirstElement(const Fvector& orientation, const int axis_num) override { SetAxisDirVsFirstElement(orientation.x, orientation.y, orientation.z, axis_num); }
    virtual void SetAxisDirVsSecondElement(const Fvector& orientation, const int axis_num) override { SetAxisDirVsSecondElement(orientation.x, orientation.y, orientation.z, axis_num); }
    virtual void SetAxisDirDynamic(const Fvector& orientation, const int axis_num);

    virtual void SetLimits(const float low, const float high, const int axis_num) override;
    virtual void SetLimitsVsFirstElement(const float low, const float high, const int axis_num) override;
    virtual void SetLimitsVsSecondElement(const float low, const float high, const int axis_num) override;
    virtual void SetHiLimitDynamic(int axis_num, float limit) override;
    virtual void SetLoLimitDynamic(int axis_num, float limit) override;

    virtual void SetAnchor(const float x, const float y, const float z) override;
    virtual void SetAnchorVsFirstElement(const float x, const float y, const float z) override;
    virtual void SetAnchorVsSecondElement(const float x, const float y, const float z) override;

    virtual void SetAxisDir(const float x, const float y, const float z, const int axis_num) override;
    virtual void SetAxisDirVsFirstElement(const float x, const float y, const float z, const int axis_num) override;
    virtual void SetAxisDirVsSecondElement(const float x, const float y, const float z, const int axis_num) override;

public:
    [[nodiscard]] virtual CPhysicsElement* PFirst_element() override;
    [[nodiscard]] virtual CPhysicsElement* PSecond_element() override;
    [[nodiscard]] virtual u16 BoneID() override { return m_bone_id; }
    virtual void SetBoneID(u16 bone_id) override { m_bone_id = bone_id; }
    
    [[nodiscard]] inline CPHElement* PFirstElement() const { return pFirst_element; }
    [[nodiscard]] inline CPHElement* PSecondElement() const { return pSecond_element; }
    
    virtual void Activate() override;
    virtual void Create() override;
    virtual void RunSimulation() override;
    virtual void SetBackRef(CPhysicsJoint** j) override;
    virtual void SetForceAndVelocity(const float force, const float velocity = 0.f, const int axis_num = -1) override;
    virtual void SetForce(const float force, const int axis_num = -1) override;
    virtual void SetVelocity(const float velocity = 0.f, const int axis_num = -1) override;
    virtual void SetBreakable(float force, float torque) override;
    [[nodiscard]] virtual bool isBreakable() override { return !!m_destroy_info; }
    
    [[nodiscard]] virtual dJointID GetDJoint() { return m_joint; }
    [[nodiscard]] virtual dJointID GetDJoint1() { return m_joint1; }
    
    virtual void GetLimits(float& lo_limit, float& hi_limit, int axis_num) override;
    virtual void GetAxisDir(int num, Fvector& axis, eVs& vs) override;
    virtual void GetAxisDirDynamic(int num, Fvector& axis) override;
    virtual void GetAnchorDynamic(Fvector& anchor) override;
    [[nodiscard]] virtual bool IsWheelJoint() override;
    [[nodiscard]] virtual bool IsHingeJoint() override;
    virtual void GetAxisSDfactors(float& spring_factor, float& damping_factor, int axis_num) override;
    virtual void GetJointSDfactors(float& spring_factor, float& damping_factor) override;
    virtual void GetMaxForceAndVelocity(float& force, float& velocity, int axis_num) override;
    [[nodiscard]] virtual float GetAxisAngle(int axis_num) override;
    [[nodiscard]] virtual float GetAxisAngleRate(int axis_num) override;
    virtual void Deactivate() override;
    
    void ReattachFirstElement(CPHElement* new_element);
    [[nodiscard]] inline CODEGeom*& RootGeom() { return pFirstGeom; }
    [[nodiscard]] virtual CPHJointDestroyInfo* JointDestroyInfo() override { return m_destroy_info; }
    
    CPHJoint(CPhysicsJoint::enumType type, CPhysicsElement* first, CPhysicsElement* second);
    virtual ~CPHJoint() override;
    void SetShell(CPHShell* p);
    void ClearDestroyInfo();

private:
    [[nodiscard]] virtual iphysics_scripted& get_scripted() override { return *this; }
};

IC void own_axis(const Fmatrix& m, Fvector& axis) {
    if (m._11 == 1.f) {
        axis.set(1.f, 0.f, 0.f);
        return;
    }
    float k = m._13 * m._21 - m._11 * m._23 + m._23;

    if (k == 0.f) {
        if (m._13 == 0.f) {
            axis.set(0.f, 0.f, 1.f);
            return;
        }
        float k1 = m._13 / (1.f - m._11);
        axis.z = 1.f / std::sqrt(1.f + k1 * k1);
        axis.x = axis.z * k1;
        axis.y = 0.f;
        return;
    }

    float k_zy = -(m._12 * m._21 - m._11 * m._22 + m._11 + m._22 - 1.f) / k;
    float k_xy = (m._12 + m._13 * k_zy) / (1.f - m._11);
    axis.y = 1.f / std::sqrt(k_zy * k_zy + k_xy * k_xy + 1.f);
    axis.x = axis.y * k_xy;
    axis.z = axis.y * k_zy;
}

IC void own_axis_angle(const Fmatrix& m, Fvector& axis, float& angle) {
    own_axis(m, axis);
    Fvector ort1, ort2;
    if (!(axis.z == 0.f && axis.y == 0.f)) {
        ort1.set(0.f, -axis.z, axis.y);
        ort2.crossproduct(axis, ort1);
    } else {
        ort1.set(0.f, 1.f, 0.f);
        ort2.crossproduct(axis, ort1);
    }
    ort1.normalize();
    ort2.normalize();

    Fvector ort1_t;
    m.transform_dir(ort1_t, ort1);

    // Векторизований Dot Product за допомогою SSE4.1
    __m128 v_ort1 = _mm_set_ps(0.0f, ort1.z, ort1.y, ort1.x);
    __m128 v_ort2 = _mm_set_ps(0.0f, ort2.z, ort2.y, ort2.x);
    __m128 v_ort1_t = _mm_set_ps(0.0f, ort1_t.z, ort1_t.y, ort1_t.x);

    float cosinus = _mm_cvtss_f32(_mm_dp_ps(v_ort1, v_ort1_t, 0x71));
    float sinus = _mm_cvtss_f32(_mm_dp_ps(v_ort2, v_ort1_t, 0x71));
    
    angle = std::acos(cosinus);
    if (sinus < 0.f) angle = -angle;
}

IC void axis_angleB(const Fmatrix& m, const Fvector& axis, float& angle) {
    Fvector ort1, ort2;
    if (!(fis_zero(axis.z) && fis_zero(axis.y))) {
        ort1.set(0.f, -axis.z, axis.y);
        ort2.crossproduct(axis, ort1);
    } else {
        ort1.set(0.f, 1.f, 0.f);
        ort2.crossproduct(axis, ort1);
    }
    ort1.normalize();
    ort2.normalize();
    
    Fvector ort1_t;
    m.transform_dir(ort1_t, ort1);
    
    // SIMD Dot Products
    __m128 v_ort1 = _mm_set_ps(0.0f, ort1.z, ort1.y, ort1.x);
    __m128 v_ort2 = _mm_set_ps(0.0f, ort2.z, ort2.y, ort2.x);
    __m128 v_ort1_t = _mm_set_ps(0.0f, ort1_t.z, ort1_t.y, ort1_t.x);

    float pr1 = _mm_cvtss_f32(_mm_dp_ps(v_ort1, v_ort1_t, 0x71));
    float pr2 = _mm_cvtss_f32(_mm_dp_ps(v_ort2, v_ort1_t, 0x71));

    if (pr1 == 0.f && pr2 == 0.f) {
        angle = 0.f;
        return;
    }

    Fvector ort_r;
    ort_r.set(pr1 * ort1.x + pr2 * ort2.x, pr1 * ort1.y + pr2 * ort2.y, pr1 * ort1.z + pr2 * ort2.z);
    ort_r.normalize();

    __m128 v_ort_r = _mm_set_ps(0.0f, ort_r.z, ort_r.y, ort_r.x);
    float cosinus = _mm_cvtss_f32(_mm_dp_ps(v_ort1, v_ort_r, 0x71));
    float sinus = _mm_cvtss_f32(_mm_dp_ps(v_ort2, v_ort_r, 0x71));
    
    angle = std::acos(cosinus);
    if (sinus < 0.f) angle = -angle;
}

IC void axis_angleA(const Fmatrix& m, const Fvector& axis, float& angle) {
    Fvector ort1, ort2, axis_t;
    m.transform_dir(axis_t, axis);
    
    if (!(fis_zero(axis_t.z) && fis_zero(axis_t.y))) {
        ort1.set(0.f, -axis_t.z, axis_t.y);
        ort2.crossproduct(axis_t, ort1);
    } else {
        ort1.set(0.f, 1.f, 0.f);
        ort2.crossproduct(axis_t, ort1);
    }
    ort1.normalize();
    ort2.normalize();
    
    Fvector ort1_t;
    m.transform_dir(ort1_t, ort1);
    
    __m128 v_ort1 = _mm_set_ps(0.0f, ort1.z, ort1.y, ort1.x);
    __m128 v_ort2 = _mm_set_ps(0.0f, ort2.z, ort2.y, ort2.x);
    __m128 v_ort1_t = _mm_set_ps(0.0f, ort1_t.z, ort1_t.y, ort1_t.x);

    float pr1 = _mm_cvtss_f32(_mm_dp_ps(v_ort1, v_ort1_t, 0x71));
    float pr2 = _mm_cvtss_f32(_mm_dp_ps(v_ort2, v_ort1_t, 0x71));

    if (pr1 == 0.f && pr2 == 0.f) {
        angle = 0.f;
        return;
    }

    Fvector ort_r;
    ort_r.set(pr1 * ort1.x + pr2 * ort2.x, pr1 * ort1.y + pr2 * ort2.y, pr1 * ort1.z + pr2 * ort2.z);
    ort_r.normalize();

    __m128 v_ort_r = _mm_set_ps(0.0f, ort_r.z, ort_r.y, ort_r.x);
    float cosinus = _mm_cvtss_f32(_mm_dp_ps(v_ort1, v_ort_r, 0x71));
    float sinus = _mm_cvtss_f32(_mm_dp_ps(v_ort2, v_ort_r, 0x71));
    
    angle = std::acos(cosinus);
    if (sinus < 0.f) angle = -angle;
}

#endif // PH_JOINT