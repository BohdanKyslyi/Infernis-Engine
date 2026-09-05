#pragma once
#ifndef __MATRIX_H
#define __MATRIX_H

#include <cmath>
#include <immintrin.h> // SIMD

/*
 * DirectX-compliant, ie row-column order, ie m[Row][Col].
 * Same as:
 * m11  m12  m13  m14  first row.
 * m21  m22  m23  m24  second row.
 * m31  m32  m33  m34  third row.
 * m41  m42  m43  m44  fourth row.
 * Translation is (m41, m42, m43), (m14, m24, m34, m44) = (0, 0, 0, 1).
 * Stored in memory as m11 m12 m13 m14 m21...
 *
 * Multiplication rules:
 *
 * [x'y'z'1] = [xyz1][M]
 *
 * x' = x*m11 + y*m21 + z*m31 + m41
 * y' = x*m12 + y*m22 + z*m32 + m42
 * z' = x*m13 + y*m23 + z*m33 + m43
 * 1' =     0 +     0 +     0 + m44
 */

// NOTE_1: positive angle means clockwise rotation
// NOTE_2: mul(A,B) means transformation B, followed by A
// NOTE_3: I,J,K,C equals to R,N,D,T
// NOTE_4: The rotation sequence is ZXY

template <class T>
struct _matrix {
    using TYPE = T;
    using Self = _matrix<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;
    using Tvector = _vector3<T>;

    union {
        struct { // Direct definition
            T _11, _12, _13, _14;
            T _21, _22, _23, _24;
            T _31, _32, _33, _34;
            T _41, _42, _43, _44;
        };
        struct {
            Tvector i;
            T _14_;
            Tvector j;
            T _24_;
            Tvector k;
            T _34_;
            Tvector c;
            T _44_;
        };
        T m[4][4]; // Array
    };

    // Class members
    inline SelfRef set(SelfCRef a) noexcept {
        i.set(a.i);
        _14_ = a._14;
        j.set(a.j);
        _24_ = a._24;
        k.set(a.k);
        _34_ = a._34;
        c.set(a.c);
        _44_ = a._44;
        return *this;
    }
    
    inline SelfRef set(const Tvector& R, const Tvector& N, const Tvector& D, const Tvector& C) noexcept {
        i.set(R);
        _14_ = 0;
        j.set(N);
        _24_ = 0;
        k.set(D);
        _34_ = 0;
        c.set(C);
        _44_ = 1;
        return *this;
    }
    
    inline SelfRef identity() noexcept {
        _11 = 1; _12 = 0; _13 = 0; _14 = 0;
        _21 = 0; _22 = 1; _23 = 0; _24 = 0;
        _31 = 0; _32 = 0; _33 = 1; _34 = 0;
        _41 = 0; _42 = 0; _43 = 0; _44 = 1;
        return *this;
    }
    
    SelfRef rotation(const _quaternion<T>& Q);
    SelfRef mk_xform(const _quaternion<T>& Q, const Tvector& V);

    inline SelfRef mul(SelfCRef A, SelfCRef B) noexcept {
        VERIFY((this != &A) && (this != &B));
        if constexpr (std::is_same_v<T, float>) {
            const float* a = &A._11;
            const float* b = &B._11;
            float* out = &_11;
            
            __m128 a_row1 = _mm_loadu_ps(a);
            __m128 a_row2 = _mm_loadu_ps(a + 4);
            __m128 a_row3 = _mm_loadu_ps(a + 8);
            __m128 a_row4 = _mm_loadu_ps(a + 12);

            for (int r = 0; r < 4; ++r) {
                __m128 b_x = _mm_set1_ps(b[r * 4 + 0]);
                __m128 b_y = _mm_set1_ps(b[r * 4 + 1]);
                __m128 b_z = _mm_set1_ps(b[r * 4 + 2]);
                __m128 b_w = _mm_set1_ps(b[r * 4 + 3]);

                __m128 res = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(b_x, a_row1), _mm_mul_ps(b_y, a_row2)),
                    _mm_add_ps(_mm_mul_ps(b_z, a_row3), _mm_mul_ps(b_w, a_row4))
                );
                _mm_storeu_ps(out + r * 4, res);
            }
        } else {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    m[r][c] = A.m[0][c] * B.m[r][0] + A.m[1][c] * B.m[r][1] + A.m[2][c] * B.m[r][2] + A.m[3][c] * B.m[r][3];
                }
            }
        }
        return *this;
    }

    inline SelfRef mul_43(SelfCRef A, SelfCRef B) noexcept {
        VERIFY((this != &A) && (this != &B));
        if constexpr (std::is_same_v<T, float>) {
            const float* a = &A._11;
            const float* b = &B._11;
            float* out = &_11;
            
            __m128 a_row1 = _mm_loadu_ps(a);
            __m128 a_row2 = _mm_loadu_ps(a + 4);
            __m128 a_row3 = _mm_loadu_ps(a + 8);
            __m128 a_row4 = _mm_loadu_ps(a + 12);

            for (int r = 0; r < 3; ++r) { 
                __m128 b_x = _mm_set1_ps(b[r * 4 + 0]);
                __m128 b_y = _mm_set1_ps(b[r * 4 + 1]);
                __m128 b_z = _mm_set1_ps(b[r * 4 + 2]);
                
                __m128 res = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(b_x, a_row1), _mm_mul_ps(b_y, a_row2)),
                    _mm_mul_ps(b_z, a_row3)
                );
                _mm_storeu_ps(out + r * 4, res);
                out[r * 4 + 3] = 0.0f; 
            }

            __m128 b_x = _mm_set1_ps(b[12]);
            __m128 b_y = _mm_set1_ps(b[13]);
            __m128 b_z = _mm_set1_ps(b[14]);
            
            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(b_x, a_row1), _mm_mul_ps(b_y, a_row2)),
                _mm_add_ps(_mm_mul_ps(b_z, a_row3), a_row4) // + a_row4 (0,0,0,1 translation)
            );
            _mm_storeu_ps(out + 12, res);
            _44 = 1.0f;
        } else {
            for (int r = 0; r < 4; ++r) {
                m[r][0] = A.m[0][0] * B.m[r][0] + A.m[1][0] * B.m[r][1] + A.m[2][0] * B.m[r][2] + (r == 3 ? A.m[3][0] : 0);
                m[r][1] = A.m[0][1] * B.m[r][0] + A.m[1][1] * B.m[r][1] + A.m[2][1] * B.m[r][2] + (r == 3 ? A.m[3][1] : 0);
                m[r][2] = A.m[0][2] * B.m[r][0] + A.m[1][2] * B.m[r][1] + A.m[2][2] * B.m[r][2] + (r == 3 ? A.m[3][2] : 0);
                m[r][3] = (r == 3 ? 1 : 0);
            }
        }
        return *this;
    }
    
    inline SelfRef mulA_44(SelfCRef A) noexcept { // mul after
        Self B;
        B.set(*this);
        mul(A, B);
        return *this;
    }
    
    inline SelfRef mulB_44(SelfCRef B) noexcept { // mul before
        Self A;
        A.set(*this);
        mul(A, B);
        return *this;
    }
    
    inline SelfRef mulA_43(SelfCRef A) noexcept { // mul after (no projection)
        Self B;
        B.set(*this);
        mul_43(A, B);
        return *this;
    }
    
    inline SelfRef mulB_43(SelfCRef B) noexcept { // mul before (no projection)
        Self A;
        A.set(*this);
        mul_43(A, B);
        return *this;
    }
    
    inline SelfRef invert(SelfCRef a) noexcept { // important: this is 4x3 invert, not the 4x4 one
        T fDetInv =
            (a._11 * (a._22 * a._33 - a._23 * a._32) - a._12 * (a._21 * a._33 - a._23 * a._31) +
             a._13 * (a._21 * a._32 - a._22 * a._31));

        VERIFY(std::abs(fDetInv) > flt_zero);
        fDetInv = 1.0f / fDetInv;

        _11 = fDetInv * (a._22 * a._33 - a._23 * a._32);
        _12 = -fDetInv * (a._12 * a._33 - a._13 * a._32);
        _13 = fDetInv * (a._12 * a._23 - a._13 * a._22);
        _14 = 0.0f;

        _21 = -fDetInv * (a._21 * a._33 - a._23 * a._31);
        _22 = fDetInv * (a._11 * a._33 - a._13 * a._31);
        _23 = -fDetInv * (a._11 * a._23 - a._13 * a._21);
        _24 = 0.0f;

        _31 = fDetInv * (a._21 * a._32 - a._22 * a._31);
        _32 = -fDetInv * (a._11 * a._32 - a._12 * a._31);
        _33 = fDetInv * (a._11 * a._22 - a._12 * a._21);
        _34 = 0.0f;

        _41 = -(a._41 * _11 + a._42 * _21 + a._43 * _31);
        _42 = -(a._41 * _12 + a._42 * _22 + a._43 * _32);
        _43 = -(a._41 * _13 + a._42 * _23 + a._43 * _33);
        _44 = 1.0f;
        return *this;
    }

    inline bool invert_b(SelfCRef a) noexcept { // important: this is 4x3 invert, not the 4x4 one
        T fDetInv =
            (a._11 * (a._22 * a._33 - a._23 * a._32) - a._12 * (a._21 * a._33 - a._23 * a._31) +
             a._13 * (a._21 * a._32 - a._22 * a._31));

        if (std::abs(fDetInv) <= flt_zero)
            return false;
            
        fDetInv = 1.0f / fDetInv;

        _11 = fDetInv * (a._22 * a._33 - a._23 * a._32);
        _12 = -fDetInv * (a._12 * a._33 - a._13 * a._32);
        _13 = fDetInv * (a._12 * a._23 - a._13 * a._22);
        _14 = 0.0f;

        _21 = -fDetInv * (a._21 * a._33 - a._23 * a._31);
        _22 = fDetInv * (a._11 * a._33 - a._13 * a._31);
        _23 = -fDetInv * (a._11 * a._23 - a._13 * a._21);
        _24 = 0.0f;

        _31 = fDetInv * (a._21 * a._32 - a._22 * a._31);
        _32 = -fDetInv * (a._11 * a._32 - a._12 * a._31);
        _33 = fDetInv * (a._11 * a._22 - a._12 * a._21);
        _34 = 0.0f;

        _41 = -(a._41 * _11 + a._42 * _21 + a._43 * _31);
        _42 = -(a._41 * _12 + a._42 * _22 + a._43 * _32);
        _43 = -(a._41 * _13 + a._42 * _23 + a._43 * _33);
        _44 = 1.0f;
        return true;
    }

    inline SelfRef invert() noexcept { // slower than invert other matrix
        Self a;
        a.set(*this);
        invert(a);
        return *this;
    }
    
    inline SelfRef transpose(SelfCRef matSource) noexcept { // faster version of transpose
        _11 = matSource._11; _12 = matSource._21; _13 = matSource._31; _14 = matSource._41;
        _21 = matSource._12; _22 = matSource._22; _23 = matSource._32; _24 = matSource._42;
        _31 = matSource._13; _32 = matSource._23; _33 = matSource._33; _34 = matSource._43;
        _41 = matSource._14; _42 = matSource._24; _43 = matSource._34; _44 = matSource._44;
        return *this;
    }
    
    inline SelfRef transpose() noexcept { // self transpose - slower
        Self a;
        a.set(*this);
        transpose(a);
        return *this;
    }
    
    inline SelfRef translate(const Tvector& Loc) noexcept { // setup translation matrix
        identity();
        c.set(Loc.x, Loc.y, Loc.z);
        return *this;
    }
    
    inline SelfRef translate(T _x, T _y, T _z) noexcept { // setup translation matrix
        identity();
        c.set(_x, _y, _z);
        return *this;
    }
    
    inline SelfRef translate_over(const Tvector& Loc) noexcept { // modify only translation
        c.set(Loc.x, Loc.y, Loc.z);
        return *this;
    }
    
    inline SelfRef translate_over(T _x, T _y, T _z) noexcept { // modify only translation
        c.set(_x, _y, _z);
        return *this;
    }
    
    inline SelfRef translate_add(const Tvector& Loc) noexcept { // combine translation
        c.add(Loc);
        return *this;
    }
    
    inline SelfRef scale(T x, T y, T z) noexcept { // setup scale matrix
        identity();
        m[0][0] = x;
        m[1][1] = y;
        m[2][2] = z;
        return *this;
    }
    
    inline SelfRef scale(const Tvector& v) noexcept { // setup scale matrix
        return scale(v.x, v.y, v.z);
    }

    inline SelfRef rotateX(T Angle) noexcept { // rotation about X axis
        T cosa = std::cos(Angle);
        T sina = std::sin(Angle);
        i.set(1, 0, 0);       _14 = 0;
        j.set(0, cosa, sina); _24 = 0;
        k.set(0, -sina, cosa);_34 = 0;
        c.set(0, 0, 0);       _44 = 1;
        return *this;
    }
    
    inline SelfRef rotateY(T Angle) noexcept { // rotation about Y axis
        T cosa = std::cos(Angle);
        T sina = std::sin(Angle);
        i.set(cosa, 0, -sina);_14 = 0;
        j.set(0, 1, 0);       _24 = 0;
        k.set(sina, 0, cosa); _34 = 0;
        c.set(0, 0, 0);       _44 = 1;
        return *this;
    }
    
    inline SelfRef rotateZ(T Angle) noexcept { // rotation about Z axis
        T cosa = std::cos(Angle);
        T sina = std::sin(Angle);
        i.set(cosa, sina, 0); _14 = 0;
        j.set(-sina, cosa, 0);_24 = 0;
        k.set(0, 0, 1);       _34 = 0;
        c.set(0, 0, 0);       _44 = 1;
        return *this;
    }

    inline SelfRef rotation(const Tvector& vdir, const Tvector& vnorm) noexcept {
        Tvector vright;
        vright.crossproduct(vnorm, vdir).normalize();
        m[0][0] = vright.x; m[0][1] = vright.y; m[0][2] = vright.z; m[0][3] = 0;
        m[1][0] = vnorm.x;  m[1][1] = vnorm.y;  m[1][2] = vnorm.z;  m[1][3] = 0;
        m[2][0] = vdir.x;   m[2][1] = vdir.y;   m[2][2] = vdir.z;   m[2][3] = 0;
        m[3][0] = 0;        m[3][1] = 0;        m[3][2] = 0;        m[3][3] = 1;
        return *this;
    }

    inline SelfRef mapXYZ() noexcept {
        i.set(1, 0, 0); _14 = 0;
        j.set(0, 1, 0); _24 = 0;
        k.set(0, 0, 1); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }
    
    inline SelfRef mapXZY() noexcept {
        i.set(1, 0, 0); _14 = 0;
        j.set(0, 0, 1); _24 = 0;
        k.set(0, 1, 0); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }
    
    inline SelfRef mapYXZ() noexcept {
        i.set(0, 1, 0); _14 = 0;
        j.set(1, 0, 0); _24 = 0;
        k.set(0, 0, 1); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }
    
    inline SelfRef mapYZX() noexcept {
        i.set(0, 1, 0); _14 = 0;
        j.set(0, 0, 1); _24 = 0;
        k.set(1, 0, 0); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }
    
    inline SelfRef mapZXY() noexcept {
        i.set(0, 0, 1); _14 = 0;
        j.set(1, 0, 0); _24 = 0;
        k.set(0, 1, 0); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }
    
    inline SelfRef mapZYX() noexcept {
        i.set(0, 0, 1); _14 = 0;
        j.set(0, 1, 0); _24 = 0;
        k.set(1, 0, 0); _34 = 0;
        c.set(0, 0, 0); _44 = 1;
        return *this;
    }

    inline SelfRef rotation(const Tvector& axis, T Angle) noexcept {
        T Cosine = std::cos(Angle);
        T Sine = std::sin(Angle);
        m[0][0] = axis.x * axis.x + (1 - axis.x * axis.x) * Cosine;
        m[0][1] = axis.x * axis.y * (1 - Cosine) + axis.z * Sine;
        m[0][2] = axis.x * axis.z * (1 - Cosine) - axis.y * Sine;
        m[0][3] = 0;
        m[1][0] = axis.x * axis.y * (1 - Cosine) - axis.z * Sine;
        m[1][1] = axis.y * axis.y + (1 - axis.y * axis.y) * Cosine;
        m[1][2] = axis.y * axis.z * (1 - Cosine) + axis.x * Sine;
        m[1][3] = 0;
        m[2][0] = axis.x * axis.z * (1 - Cosine) + axis.y * Sine;
        m[2][1] = axis.y * axis.z * (1 - Cosine) - axis.x * Sine;
        m[2][2] = axis.z * axis.z + (1 - axis.z * axis.z) * Cosine;
        m[2][3] = 0;
        m[3][0] = 0;
        m[3][1] = 0;
        m[3][2] = 0;
        m[3][3] = 1;
        return *this;
    }

    // mirror X
    inline SelfRef mirrorX() noexcept {
        identity();
        m[0][0] = -1;
        return *this;
    }
    
    inline SelfRef mirrorX_over() noexcept {
        m[0][0] = -1;
        return *this;
    }
    
    inline SelfRef mirrorX_add() noexcept {
        m[0][0] *= -1;
        return *this;
    }

    // mirror Y
    inline SelfRef mirrorY() noexcept {
        identity();
        m[1][1] = -1;
        return *this;
    }
    
    inline SelfRef mirrorY_over() noexcept {
        m[1][1] = -1;
        return *this;
    }
    
    inline SelfRef mirrorY_add() noexcept {
        m[1][1] *= -1;
        return *this;
    }

    // mirror Z
    inline SelfRef mirrorZ() noexcept {
        identity();
        m[2][2] = -1;
        return *this;
    }
    
    inline SelfRef mirrorZ_over() noexcept {
        m[2][2] = -1;
        return *this;
    }
    
    inline SelfRef mirrorZ_add() noexcept {
        m[2][2] *= -1;
        return *this;
    }
    
    inline SelfRef mul(SelfCRef A, T v) noexcept {
        m[0][0] = A.m[0][0] * v; m[0][1] = A.m[0][1] * v; m[0][2] = A.m[0][2] * v; m[0][3] = A.m[0][3] * v;
        m[1][0] = A.m[1][0] * v; m[1][1] = A.m[1][1] * v; m[1][2] = A.m[1][2] * v; m[1][3] = A.m[1][3] * v;
        m[2][0] = A.m[2][0] * v; m[2][1] = A.m[2][1] * v; m[2][2] = A.m[2][2] * v; m[2][3] = A.m[2][3] * v;
        m[3][0] = A.m[3][0] * v; m[3][1] = A.m[3][1] * v; m[3][2] = A.m[3][2] * v; m[3][3] = A.m[3][3] * v;
        return *this;
    }
    
    inline SelfRef mul(T v) noexcept {
        m[0][0] *= v; m[0][1] *= v; m[0][2] *= v; m[0][3] *= v;
        m[1][0] *= v; m[1][1] *= v; m[1][2] *= v; m[1][3] *= v;
        m[2][0] *= v; m[2][1] *= v; m[2][2] *= v; m[2][3] *= v;
        m[3][0] *= v; m[3][1] *= v; m[3][2] *= v; m[3][3] *= v;
        return *this;
    }
    
    inline SelfRef div(SelfCRef A, T v) noexcept {
        VERIFY(std::abs(v) > 0.000001f);
        return mul(A, 1.0f / v);
    }
    
    inline SelfRef div(T v) noexcept {
        VERIFY(std::abs(v) > 0.000001f);
        return mul(1.0f / v);
    }
    
    // fov
    inline SelfRef build_projection(T fFOV, T fAspect, T fNearPlane, T fFarPlane) noexcept {
        return build_projection_HAT(std::tan(fFOV / 2.f), fAspect, fNearPlane, fFarPlane);
    }
    
    // half_fov-angle-tangent
    inline SelfRef build_projection_HAT(T HAT, T fAspect, T fNearPlane, T fFarPlane) noexcept {
        VERIFY(std::abs(fFarPlane - fNearPlane) > EPS_S);
        VERIFY(std::abs(HAT) > EPS_S);

        T cot = T(1) / HAT;
        T w = fAspect * cot;
        T h = T(1) * cot;
        T Q = fFarPlane / (fFarPlane - fNearPlane);

        _11 = w;  _12 = 0; _13 = 0; _14 = 0;
        _21 = 0;  _22 = h; _23 = 0; _24 = 0;
        _31 = 0;  _32 = 0; _33 = Q; _34 = 1.0f;
        _41 = 0;  _42 = 0; _43 = -Q * fNearPlane; _44 = 0;
        return *this;
    }
    
    inline SelfRef build_projection_ortho(T w, T h, T zn, T zf) noexcept {
        _11 = T(2) / w; _12 = 0;        _13 = 0;                _14 = 0;
        _21 = 0;        _22 = T(2) / h; _23 = 0;                _24 = 0;
        _31 = 0;        _32 = 0;        _33 = T(1) / (zf - zn); _34 = 0;
        _41 = 0;        _42 = 0;        _43 = zn / (zn - zf);   _44 = T(1);
        return *this;
    }
    
    inline SelfRef build_camera(const Tvector& vFrom, const Tvector& vAt, const Tvector& vWorldUp) noexcept {
        Tvector vView;
        vView.sub(vAt, vFrom).normalize();

        T fDotProduct = vWorldUp.dotproduct(vView);

        Tvector vUp;
        vUp.mul(vView, -fDotProduct).add(vWorldUp).normalize();

        Tvector vRight;
        vRight.crossproduct(vUp, vView);

        _11 = vRight.x; _12 = vUp.x; _13 = vView.x; _14 = 0.0f;
        _21 = vRight.y; _22 = vUp.y; _23 = vView.y; _24 = 0.0f;
        _31 = vRight.z; _32 = vUp.z; _33 = vView.z; _34 = 0.0f;

        _41 = -vFrom.dotproduct(vRight);
        _42 = -vFrom.dotproduct(vUp);
        _43 = -vFrom.dotproduct(vView);
        _44 = 1.0f;
        return *this;
    }
    
    inline SelfRef build_camera_dir(const Tvector& vFrom, const Tvector& vView, const Tvector& vWorldUp) noexcept {
        T fDotProduct = vWorldUp.dotproduct(vView);

        Tvector vUp;
        vUp.mul(vView, -fDotProduct).add(vWorldUp).normalize();

        Tvector vRight;
        vRight.crossproduct(vUp, vView);

        _11 = vRight.x; _12 = vUp.x; _13 = vView.x; _14 = 0.0f;
        _21 = vRight.y; _22 = vUp.y; _23 = vView.y; _24 = 0.0f;
        _31 = vRight.z; _32 = vUp.z; _33 = vView.z; _34 = 0.0f;

        _41 = -vFrom.dotproduct(vRight);
        _42 = -vFrom.dotproduct(vUp);
        _43 = -vFrom.dotproduct(vView);
        _44 = 1.0f;
        return *this;
    }

    inline SelfRef inertion(SelfCRef mat, T v) noexcept {
        T iv = 1.f - v;
        for (int i = 0; i < 4; i++) {
            m[i][0] = m[i][0] * v + mat.m[i][0] * iv;
            m[i][1] = m[i][1] * v + mat.m[i][1] * iv;
            m[i][2] = m[i][2] * v + mat.m[i][2] * iv;
            m[i][3] = m[i][3] * v + mat.m[i][3] * iv;
        }
        return *this;
    }
    
    inline void transform_tiny(Tvector& dest, const Tvector& v) const noexcept { // preferred to use
        if constexpr (std::is_same_v<T, float>) {
            __m128 vx = _mm_set1_ps(v.x);
            __m128 vy = _mm_set1_ps(v.y);
            __m128 vz = _mm_set1_ps(v.z);
            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(vx, _mm_loadu_ps(&_11)), _mm_mul_ps(vy, _mm_loadu_ps(&_21))),
                _mm_add_ps(_mm_mul_ps(vz, _mm_loadu_ps(&_31)), _mm_loadu_ps(&_41))
            );
            dest.x = _mm_cvtss_f32(res);
            dest.y = _mm_cvtss_f32(_mm_shuffle_ps(res, res, _MM_SHUFFLE(1, 1, 1, 1)));
            dest.z = _mm_cvtss_f32(_mm_shuffle_ps(res, res, _MM_SHUFFLE(2, 2, 2, 2)));
        } else {
            dest.x = v.x * _11 + v.y * _21 + v.z * _31 + _41;
            dest.y = v.x * _12 + v.y * _22 + v.z * _32 + _42;
            dest.z = v.x * _13 + v.y * _23 + v.z * _33 + _43;
        }
    }
    
    inline void transform_tiny32(Fvector2& dest, const Tvector& v) const noexcept { // preferred to use
        dest.x = v.x * _11 + v.y * _21 + v.z * _31 + _41;
        dest.y = v.x * _12 + v.y * _22 + v.z * _32 + _42;
    }
    
    inline void transform_tiny23(Tvector& dest, const Fvector2& v) const noexcept { // preferred to use
        dest.x = v.x * _11 + v.y * _21 + _41;
        dest.y = v.x * _12 + v.y * _22 + _42;
        dest.z = v.x * _13 + v.y * _23 + _43;
    }
    
    inline void transform_dir(Tvector& dest, const Tvector& v) const noexcept { // preferred to use
        if constexpr (std::is_same_v<T, float>) {
            __m128 vx = _mm_set1_ps(v.x);
            __m128 vy = _mm_set1_ps(v.y);
            __m128 vz = _mm_set1_ps(v.z);
            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(vx, _mm_loadu_ps(&_11)), _mm_mul_ps(vy, _mm_loadu_ps(&_21))),
                _mm_mul_ps(vz, _mm_loadu_ps(&_31))
            );
            dest.x = _mm_cvtss_f32(res);
            dest.y = _mm_cvtss_f32(_mm_shuffle_ps(res, res, _MM_SHUFFLE(1, 1, 1, 1)));
            dest.z = _mm_cvtss_f32(_mm_shuffle_ps(res, res, _MM_SHUFFLE(2, 2, 2, 2)));
        } else {
            dest.x = v.x * _11 + v.y * _21 + v.z * _31;
            dest.y = v.x * _12 + v.y * _22 + v.z * _32;
            dest.z = v.x * _13 + v.y * _23 + v.z * _33;
        }
    }
    
    inline void transform(Fvector4& dest, const Tvector& v) const noexcept { // preferred to use
        dest.w = v.x * _14 + v.y * _24 + v.z * _34 + _44;
        dest.x = (v.x * _11 + v.y * _21 + v.z * _31 + _41) / dest.w;
        dest.y = (v.x * _12 + v.y * _22 + v.z * _32 + _42) / dest.w;
        dest.z = (v.x * _13 + v.y * _23 + v.z * _33 + _43) / dest.w;
    }
    
    inline void transform(Tvector& dest, const Tvector& v) const noexcept { // preferred to use
        T iw = 1.f / (v.x * _14 + v.y * _24 + v.z * _34 + _44);
        dest.x = (v.x * _11 + v.y * _21 + v.z * _31 + _41) * iw;
        dest.y = (v.x * _12 + v.y * _22 + v.z * _32 + _42) * iw;
        dest.z = (v.x * _13 + v.y * _23 + v.z * _33 + _43) * iw;
    }

    inline void transform(Fvector4& dest, const Fvector4& v) const noexcept { // preferred to use
        dest.w = v.x * _14 + v.y * _24 + v.z * _34 + v.w * _44;
        dest.x = v.x * _11 + v.y * _21 + v.z * _31 + v.w * _41;
        dest.y = v.x * _12 + v.y * _22 + v.z * _32 + v.w * _42;
        dest.z = v.x * _13 + v.y * _23 + v.z * _33 + v.w * _43;
    }

    inline void transform_tiny(Tvector& v) const noexcept {
        Tvector res;
        transform_tiny(res, v);
        v.set(res);
    }
    
    inline void transform(Tvector& v) const noexcept {
        Tvector res;
        transform(res, v);
        v.set(res);
    }
    
    inline void transform_dir(Tvector& v) const noexcept {
        Tvector res;
        transform_dir(res, v);
        v.set(res);
    }
    
    inline SelfRef setHPB(T h, T p, T b) noexcept {
        T _ch, _cp, _cb, _sh, _sp, _sb, _cc, _cs, _sc, _ss;

        _sh = std::sin(h);
        _ch = std::cos(h);
        _sp = std::sin(p);
        _cp = std::cos(p);
        _sb = std::sin(b);
        _cb = std::cos(b);
        _cc = _ch * _cb;
        _cs = _ch * _sb;
        _sc = _sh * _cb;
        _ss = _sh * _sb;

        i.set(_cc - _sp * _ss, -_cp * _sb, _sp * _cs + _sc);
        _14_ = 0;
        j.set(_sp * _sc + _cs, _cp * _cb, _ss - _sp * _cc);
        _24_ = 0;
        k.set(-_cp * _sh, _sp, _cp * _ch);
        _34_ = 0;
        c.set(0, 0, 0);
        _44_ = 1;
        return *this;
    }
    
    inline SelfRef setXYZ(T x, T y, T z) noexcept { return setHPB(y, x, z); }
    inline SelfRef setXYZ(Tvector const& xyz) noexcept { return setHPB(xyz.y, xyz.x, xyz.z); }
    inline SelfRef setXYZi(T x, T y, T z) noexcept { return setHPB(-y, -x, -z); }
    inline SelfRef setXYZi(Tvector const& xyz) noexcept { return setHPB(-xyz.y, -xyz.x, -xyz.z); }
    
    inline void getHPB(T& h, T& p, T& b) const noexcept {
        T cy = std::sqrt(j.y * j.y + i.y * i.y);
        if (cy > 16.0f * type_epsilon<T>) {
            h = (T)-std::atan2(k.x, k.z);
            p = (T)-std::atan2(-k.y, cy);
            b = (T)-std::atan2(i.y, j.y);
        } else {
            h = (T)-std::atan2(-i.z, i.x);
            p = (T)-std::atan2(-k.y, cy);
            b = 0;
        }
    }
    
    inline void getHPB(Tvector& hpb) const noexcept { getHPB(hpb.x, hpb.y, hpb.z); }
    inline void getXYZ(T& x, T& y, T& z) const noexcept { getHPB(y, x, z); }
    inline void getXYZ(Tvector& xyz) const noexcept { getXYZ(xyz.x, xyz.y, xyz.z); }
    
    inline void getXYZi(T& x, T& y, T& z) const noexcept {
        getHPB(y, x, z);
        x *= -1.f;
        y *= -1.f;
        z *= -1.f;
    }
    
    inline void getXYZi(Tvector& xyz) const noexcept {
        getXYZ(xyz.x, xyz.y, xyz.z);
        xyz.mul(-1.f);
    }
};

using Fmatrix = _matrix<float>;
using Dmatrix = _matrix<double>;

namespace xr {
    template <class T>
    [[nodiscard]] inline bool valid(const _matrix<T>& m) noexcept {
        return valid(m.i) && valid(m._14_) && valid(m.j) && valid(m._24_) &&
               valid(m.k) && valid(m._34_) && valid(m.c) && valid(m._44_);
    }
} // xr namespace

extern XRCORE_API Fmatrix Fidentity;
extern XRCORE_API Dmatrix Didentity;

#endif // __MATRIX_H