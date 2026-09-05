#pragma once
#ifndef _matrix33H_
#define _matrix33H_

#include <cmath>
#include <cstring>
#include <algorithm>

template <class T>
struct _matrix; // Forward declaration for 4x4 matrix

template <class T>
struct _matrix33 {
public:
    using TYPE = T;
    using Self = _matrix33<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;
    using Tvector = _vector3<T>;

public:
    union {
        struct { // Direct definition
            T _11, _12, _13;
            T _21, _22, _23;
            T _31, _32, _33;
        };
        struct {
            Tvector i;
            Tvector j;
            Tvector k;
        };
        T m[3][3]; // Array
    };

    // Class members
    inline SelfRef set_rapid(const _matrix<T>& a) noexcept {
        m[0][0] = a.m[0][0];
        m[0][1] = a.m[0][1];
        m[0][2] = -a.m[0][2];
        m[1][0] = a.m[1][0];
        m[1][1] = a.m[1][1];
        m[1][2] = -a.m[1][2];
        m[2][0] = -a.m[2][0];
        m[2][1] = -a.m[2][1];
        m[2][2] = a.m[2][2];
        return *this;
    }

    inline SelfRef set(SelfCRef a) noexcept {
        *this = a; // Compiler optimizes this better than memcpy
        return *this;
    }

    inline SelfRef set(const _matrix<T>& a) noexcept {
        _11 = a._11; _12 = a._12; _13 = a._13;
        _21 = a._21; _22 = a._22; _23 = a._23;
        _31 = a._31; _32 = a._32; _33 = a._33;
        return *this;
    }

    inline SelfRef identity() noexcept {
        _11 = 1.f; _12 = 0.f; _13 = 0.f;
        _21 = 0.f; _22 = 1.f; _23 = 0.f;
        _31 = 0.f; _32 = 0.f; _33 = 1.f;
        return *this;
    }

    inline SelfRef transpose(SelfCRef matSource) noexcept {
        _11 = matSource._11; _12 = matSource._21; _13 = matSource._31;
        _21 = matSource._12; _22 = matSource._22; _23 = matSource._32;
        _31 = matSource._13; _32 = matSource._23; _33 = matSource._33;
        return *this;
    }

    inline SelfRef transpose(const _matrix<T>& matSource) noexcept {
        _11 = matSource._11; _12 = matSource._21; _13 = matSource._31;
        _21 = matSource._12; _22 = matSource._22; _23 = matSource._32;
        _31 = matSource._13; _32 = matSource._23; _33 = matSource._33;
        return *this;
    }

    inline SelfRef transpose() noexcept {
        std::swap(_12, _21);
        std::swap(_13, _31);
        std::swap(_23, _32);
        return *this;
    }

    inline SelfRef MxM(SelfCRef M1, SelfCRef M2) noexcept {
        m[0][0] = (M1.m[0][0] * M2.m[0][0] + M1.m[0][1] * M2.m[1][0] + M1.m[0][2] * M2.m[2][0]);
        m[1][0] = (M1.m[1][0] * M2.m[0][0] + M1.m[1][1] * M2.m[1][0] + M1.m[1][2] * M2.m[2][0]);
        m[2][0] = (M1.m[2][0] * M2.m[0][0] + M1.m[2][1] * M2.m[1][0] + M1.m[2][2] * M2.m[2][0]);
        m[0][1] = (M1.m[0][0] * M2.m[0][1] + M1.m[0][1] * M2.m[1][1] + M1.m[0][2] * M2.m[2][1]);
        m[1][1] = (M1.m[1][0] * M2.m[0][1] + M1.m[1][1] * M2.m[1][1] + M1.m[1][2] * M2.m[2][1]);
        m[2][1] = (M1.m[2][0] * M2.m[0][1] + M1.m[2][1] * M2.m[1][1] + M1.m[2][2] * M2.m[2][1]);
        m[0][2] = (M1.m[0][0] * M2.m[0][2] + M1.m[0][1] * M2.m[1][2] + M1.m[0][2] * M2.m[2][2]);
        m[1][2] = (M1.m[1][0] * M2.m[0][2] + M1.m[1][1] * M2.m[1][2] + M1.m[1][2] * M2.m[2][2]);
        m[2][2] = (M1.m[2][0] * M2.m[0][2] + M1.m[2][1] * M2.m[1][2] + M1.m[2][2] * M2.m[2][2]);
        return *this;
    }

    inline SelfRef MTxM(SelfCRef M1, SelfCRef M2) noexcept {
        m[0][0] = (M1.m[0][0] * M2.m[0][0] + M1.m[1][0] * M2.m[1][0] + M1.m[2][0] * M2.m[2][0]);
        m[1][0] = (M1.m[0][1] * M2.m[0][0] + M1.m[1][1] * M2.m[1][0] + M1.m[2][1] * M2.m[2][0]);
        m[2][0] = (M1.m[0][2] * M2.m[0][0] + M1.m[1][2] * M2.m[1][0] + M1.m[2][2] * M2.m[2][0]);
        m[0][1] = (M1.m[0][0] * M2.m[0][1] + M1.m[1][0] * M2.m[1][1] + M1.m[2][0] * M2.m[2][1]);
        m[1][1] = (M1.m[0][1] * M2.m[0][1] + M1.m[1][1] * M2.m[1][1] + M1.m[2][1] * M2.m[2][1]);
        m[2][1] = (M1.m[0][2] * M2.m[0][1] + M1.m[1][2] * M2.m[1][1] + M1.m[2][2] * M2.m[2][1]);
        m[0][2] = (M1.m[0][0] * M2.m[0][2] + M1.m[1][0] * M2.m[1][2] + M1.m[2][0] * M2.m[2][2]);
        m[1][2] = (M1.m[0][1] * M2.m[0][2] + M1.m[1][1] * M2.m[1][2] + M1.m[2][1] * M2.m[2][2]);
        m[2][2] = (M1.m[0][2] * M2.m[0][2] + M1.m[1][2] * M2.m[1][2] + M1.m[2][2] * M2.m[2][2]);
        return *this;
    }

#define ROT_MACRO(a, i, j, k, l)       \
    g = a.m[i][j];                     \
    h = a.m[k][l];                     \
    a.m[i][j] = g - s * (h + g * tau); \
    a.m[k][l] = h + s * (g - h * tau);

    inline int Meigen(Tvector& dout, SelfRef a) noexcept {
        int i;
        T tresh, theta, tau, t, sm, s, h, g, c;
        int nrot = 0;
        Tvector b, z, d;
        _matrix33 v;

        v.identity();
        b.set(a.m[0][0], a.m[1][1], a.m[2][2]);
        d.set(a.m[0][0], a.m[1][1], a.m[2][2]);
        z.set(0, 0, 0);

        for (i = 0; i < 50; i++) {
            sm = std::abs(a.m[0][1]) + std::abs(a.m[0][2]) + std::abs(a.m[1][2]);
            if (sm == 0.0f) {
                set(v);
                dout.set(d);
                return i;
            }
            if (i < 3)
                tresh = 0.2f * sm / 9.0f;
            else
                tresh = 0.0f;
            
            // 0, 1
            g = 100.0f * std::abs(a.m[0][1]);
            if (i > 3 && std::abs(d.x) + g == std::abs(d.x) && std::abs(d.y) + g == std::abs(d.y)) {
                a.m[0][1] = 0.0f;
            } else if (std::abs(a.m[0][1]) > tresh) {
                h = d.y - d.x;
                if (std::abs(h) + g == std::abs(h)) {
                    t = a.m[0][1] / h;
                } else {
                    theta = 0.5f * h / a.m[0][1];
                    t = 1.0f / (std::abs(theta) + std::sqrt(1.0f + theta * theta));
                    if (theta < 0.0f) t = -t;
                }
                c = 1.0f / std::sqrt(1.0f + t * t);
                s = t * c;
                tau = s / (1.0f + c);
                h = t * a.m[0][1];
                z.x -= h; z.y += h;
                d.x -= h; d.y += h;
                a.m[0][1] = 0.0f;
                ROT_MACRO(a, 0, 2, 1, 2);
                ROT_MACRO(v, 0, 0, 0, 1);
                ROT_MACRO(v, 1, 0, 1, 1);
                ROT_MACRO(v, 2, 0, 2, 1);
                nrot++;
            }
            
            // 0, 2
            g = 100.0f * std::abs(a.m[0][2]);
            if (i > 3 && std::abs(d.x) + g == std::abs(d.x) && std::abs(d.z) + g == std::abs(d.z)) {
                a.m[0][2] = 0.0f;
            } else if (std::abs(a.m[0][2]) > tresh) {
                h = d.z - d.x;
                if (std::abs(h) + g == std::abs(h)) {
                    t = a.m[0][2] / h;
                } else {
                    theta = 0.5f * h / a.m[0][2];
                    t = 1.0f / (std::abs(theta) + std::sqrt(1.0f + theta * theta));
                    if (theta < 0.0f) t = -t;
                }
                c = 1.0f / std::sqrt(1.0f + t * t);
                s = t * c;
                tau = s / (1.0f + c);
                h = t * a.m[0][2];
                z.x -= h; z.z += h;
                d.x -= h; d.z += h;
                a.m[0][2] = 0.0f;
                ROT_MACRO(a, 0, 1, 1, 2);
                ROT_MACRO(v, 0, 0, 0, 2);
                ROT_MACRO(v, 1, 0, 1, 2);
                ROT_MACRO(v, 2, 0, 2, 2);
                nrot++;
            }
            
            // 1, 2
            g = 100.0f * std::abs(a.m[1][2]);
            if (i > 3 && std::abs(d.y) + g == std::abs(d.y) && std::abs(d.z) + g == std::abs(d.z)) {
                a.m[1][2] = 0.0f;
            } else if (std::abs(a.m[1][2]) > tresh) {
                h = d.z - d.y;
                if (std::abs(h) + g == std::abs(h)) {
                    t = a.m[1][2] / h;
                } else {
                    theta = 0.5f * h / a.m[1][2];
                    t = 1.0f / (std::abs(theta) + std::sqrt(1.0f + theta * theta));
                    if (theta < 0.0f) t = -t;
                }
                c = 1.0f / std::sqrt(1.0f + t * t);
                s = t * c;
                tau = s / (1.0f + c);
                h = t * a.m[1][2];
                z.y -= h; z.z += h;
                d.y -= h; d.z += h;
                a.m[1][2] = 0.0f;
                ROT_MACRO(a, 0, 1, 0, 2);
                ROT_MACRO(v, 0, 1, 0, 2);
                ROT_MACRO(v, 1, 1, 1, 2);
                ROT_MACRO(v, 2, 1, 2, 2);
                nrot++;
            }
            
            b.add(z);
            d.set(b);
            z.set(0, 0, 0);
        }
        return i;
    }
#undef ROT_MACRO

    inline SelfRef McolcMcol(int cr, SelfCRef M, int c) noexcept {
        m[0][cr] = M.m[0][c];
        m[1][cr] = M.m[1][c];
        m[2][cr] = M.m[2][c];
        return *this;
    }

    inline SelfRef MxMpV(SelfCRef M1, SelfCRef M2, const Tvector& T_vec) noexcept {
        m[0][0] = (M1.m[0][0] * M2.m[0][0] + M1.m[0][1] * M2.m[1][0] + M1.m[0][2] * M2.m[2][0] + T_vec.x);
        m[1][0] = (M1.m[1][0] * M2.m[0][0] + M1.m[1][1] * M2.m[1][0] + M1.m[1][2] * M2.m[2][0] + T_vec.y);
        m[2][0] = (M1.m[2][0] * M2.m[0][0] + M1.m[2][1] * M2.m[1][0] + M1.m[2][2] * M2.m[2][0] + T_vec.z);
        m[0][1] = (M1.m[0][0] * M2.m[0][1] + M1.m[0][1] * M2.m[1][1] + M1.m[0][2] * M2.m[2][1] + T_vec.x);
        m[1][1] = (M1.m[1][0] * M2.m[0][1] + M1.m[1][1] * M2.m[1][1] + M1.m[1][2] * M2.m[2][1] + T_vec.y);
        m[2][1] = (M1.m[2][0] * M2.m[0][1] + M1.m[2][1] * M2.m[1][1] + M1.m[2][2] * M2.m[2][1] + T_vec.z);
        m[0][2] = (M1.m[0][0] * M2.m[0][2] + M1.m[0][1] * M2.m[1][2] + M1.m[0][2] * M2.m[2][2] + T_vec.x);
        m[1][2] = (M1.m[1][0] * M2.m[0][2] + M1.m[1][1] * M2.m[1][2] + M1.m[1][2] * M2.m[2][2] + T_vec.y);
        m[2][2] = (M1.m[2][0] * M2.m[0][2] + M1.m[2][1] * M2.m[1][2] + M1.m[2][2] * M2.m[2][2] + T_vec.z);
        return *this;
    }

    inline SelfRef Mqinverse(SelfCRef M) noexcept {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                int i1 = (i + 1) % 3;
                int i2 = (i + 2) % 3;
                int j1 = (j + 1) % 3;
                int j2 = (j + 2) % 3;
                m[i][j] = (M.m[j1][i1] * M.m[j2][i2] - M.m[j1][i2] * M.m[j2][i1]);
            }
        }
        return *this;
    }

    inline SelfRef MxMT(SelfCRef M1, SelfCRef M2) noexcept {
        m[0][0] = (M1.m[0][0] * M2.m[0][0] + M1.m[0][1] * M2.m[0][1] + M1.m[0][2] * M2.m[0][2]);
        m[1][0] = (M1.m[1][0] * M2.m[0][0] + M1.m[1][1] * M2.m[0][1] + M1.m[1][2] * M2.m[0][2]);
        m[2][0] = (M1.m[2][0] * M2.m[0][0] + M1.m[2][1] * M2.m[0][1] + M1.m[2][2] * M2.m[0][2]);
        m[0][1] = (M1.m[0][0] * M2.m[1][0] + M1.m[0][1] * M2.m[1][1] + M1.m[0][2] * M2.m[1][2]);
        m[1][1] = (M1.m[1][0] * M2.m[1][0] + M1.m[1][1] * M2.m[1][1] + M1.m[1][2] * M2.m[1][2]);
        m[2][1] = (M1.m[2][0] * M2.m[1][0] + M1.m[2][1] * M2.m[1][1] + M1.m[2][2] * M2.m[1][2]);
        m[0][2] = (M1.m[0][0] * M2.m[2][0] + M1.m[0][1] * M2.m[2][1] + M1.m[0][2] * M2.m[2][2]);
        m[1][2] = (M1.m[1][0] * M2.m[2][0] + M1.m[1][1] * M2.m[2][1] + M1.m[1][2] * M2.m[2][2]);
        m[2][2] = (M1.m[2][0] * M2.m[2][0] + M1.m[2][1] * M2.m[2][1] + M1.m[2][2] * M2.m[2][2]);
        return *this;
    }

    inline SelfRef MskewV(const Tvector& v) noexcept {
        m[0][0] = m[1][1] = m[2][2] = 0.0f;
        m[1][0] = v.z;
        m[0][1] = -v.z;
        m[0][2] = v.y;
        m[2][0] = -v.y;
        m[1][2] = -v.x;
        m[2][1] = v.x;
        return *this;
    }

    inline SelfRef sMxVpV(Tvector& R, T s1, const Tvector& V1, const Tvector& V2) const noexcept {
        R.x = s1 * (m[0][0] * V1.x + m[0][1] * V1.y + m[0][2] * V1.z) + V2.x;
        R.y = s1 * (m[1][0] * V1.x + m[1][1] * V1.y + m[1][2] * V1.z) + V2.y;
        R.z = s1 * (m[2][0] * V1.x + m[2][1] * V1.y + m[2][2] * V1.z) + V2.z;
        return const_cast<SelfRef>(*this);
    }

    inline void MTxV(Tvector& R, const Tvector& V1) const noexcept {
        R.x = (m[0][0] * V1.x + m[1][0] * V1.y + m[2][0] * V1.z);
        R.y = (m[0][1] * V1.x + m[1][1] * V1.y + m[2][1] * V1.z);
        R.z = (m[0][2] * V1.x + m[1][2] * V1.y + m[2][2] * V1.z);
    }

    inline void MTxVpV(Tvector& R, const Tvector& V1, const Tvector& V2) const noexcept {
        R.x = (m[0][0] * V1.x + m[1][0] * V1.y + m[2][0] * V1.z + V2.x);
        R.y = (m[0][1] * V1.x + m[1][1] * V1.y + m[2][1] * V1.z + V2.y);
        R.z = (m[0][2] * V1.x + m[1][2] * V1.y + m[2][2] * V1.z + V2.z);
    }

    inline SelfRef MTxVmV(Tvector& R, const Tvector& V1, const Tvector& V2) const noexcept {
        R.x = (m[0][0] * V1.x + m[1][0] * V1.y + m[2][0] * V1.z - V2.x);
        R.y = (m[0][1] * V1.x + m[1][1] * V1.y + m[2][1] * V1.z - V2.y);
        R.z = (m[0][2] * V1.x + m[1][2] * V1.y + m[2][2] * V1.z - V2.z);
        return const_cast<SelfRef>(*this);
    }

    inline SelfRef sMTxV(Tvector& R, T s1, const Tvector& V1) const noexcept {
        R.x = s1 * (m[0][0] * V1.x + m[1][0] * V1.y + m[2][0] * V1.z);
        R.y = s1 * (m[0][1] * V1.x + m[1][1] * V1.y + m[2][1] * V1.z);
        R.z = s1 * (m[0][2] * V1.x + m[1][2] * V1.y + m[2][2] * V1.z);
        return const_cast<SelfRef>(*this);
    }

    inline SelfRef MxV(Tvector& R, const Tvector& V1) const noexcept {
        R.x = (m[0][0] * V1.x + m[0][1] * V1.y + m[0][2] * V1.z);
        R.y = (m[1][0] * V1.x + m[1][1] * V1.y + m[1][2] * V1.z);
        R.z = (m[2][0] * V1.x + m[2][1] * V1.y + m[2][2] * V1.z);
        return const_cast<SelfRef>(*this);
    }

    inline void transform_dir(_vector2<T>& dest, const _vector2<T>& v) const noexcept { // preferred to use
        dest.x = v.x * _11 + v.y * _21;
        dest.y = v.x * _12 + v.y * _22;
    }

    inline void transform_dir(_vector2<T>& v) const noexcept {
        _vector2<T> res;
        transform_dir(res, v);
        v.set(res);
    }

    inline SelfRef MxVpV(Tvector& R, const Tvector& V1, const Tvector& V2) const noexcept {
        R.x = (m[0][0] * V1.x + m[0][1] * V1.y + m[0][2] * V1.z + V2.x);
        R.y = (m[1][0] * V1.x + m[1][1] * V1.y + m[1][2] * V1.z + V2.y);
        R.z = (m[2][0] * V1.x + m[2][1] * V1.y + m[2][2] * V1.z + V2.z);
        return const_cast<SelfRef>(*this);
    }
};

using Fmatrix33 = _matrix33<float>;
using Dmatrix33 = _matrix33<double>;

namespace xr {
    template <class T>
    [[nodiscard]] inline bool valid(const _matrix33<T>& m) noexcept {
        return valid(m.i) && valid(m.j) && valid(m.k);
    }
} // namespace xr

#endif // _matrix33H_