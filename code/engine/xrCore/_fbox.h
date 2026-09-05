#pragma once
#ifndef __FBOX
#define __FBOX

#include <immintrin.h>
#include <cmath>

template <class T>
class _box3 {
public:
    using TYPE = T;
    using Self = _box3<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;
    using Tvector = _vector3<T>;
    using Tmatrix = _matrix<T>;

    union {
        struct {
            Tvector min;
            Tvector max;
        };
        struct {
            T x1, y1, z1;
            T x2, y2, z2;
        };
    };

    [[nodiscard]] constexpr BOOL is_valid() const noexcept { 
        return (x2 >= x1) && (y2 >= y1) && (z2 >= z1); 
    }

    [[nodiscard]] constexpr const T* data() const noexcept { return &min.x; }

    inline SelfRef set(const Tvector& _min, const Tvector& _max) noexcept {
        min.set(_min); max.set(_max); return *this;
    }
    
    inline SelfRef set(T _x1, T _y1, T _z1, T _x2, T _y2, T _z2) noexcept {
        min.set(_x1, _y1, _z1); max.set(_x2, _y2, _z2); return *this;
    }
    
    inline SelfRef set(SelfCRef b) noexcept {
        min.set(b.min); max.set(b.max); return *this;
    }
    
    inline SelfRef setb(const Tvector& center, const Tvector& dim) noexcept {
        min.sub(center, dim); max.add(center, dim); return *this;
    }

    inline SelfRef null() noexcept {
        min.set(0, 0, 0); max.set(0, 0, 0); return *this;
    }
    
    inline SelfRef identity() noexcept {
        min.set(-0.5, -0.5, -0.5); max.set(0.5, 0.5, 0.5); return *this;
    }
    
    inline SelfRef invalidate() noexcept {
        min.set(type_max<T>, type_max<T>, type_max<T>);
        max.set(type_min<T>, type_min<T>, type_min<T>);
        return *this;
    }

    inline SelfRef shrink(T s) noexcept { min.add(s); max.sub(s); return *this; }
    inline SelfRef shrink(const Tvector& s) noexcept { min.add(s); max.sub(s); return *this; }
    inline SelfRef grow(T s) noexcept { min.sub(s); max.add(s); return *this; }
    inline SelfRef grow(const Tvector& s) noexcept { min.sub(s); max.add(s); return *this; }

    inline SelfRef add(const Tvector& p) noexcept { min.add(p); max.add(p); return *this; }
    inline SelfRef sub(const Tvector& p) noexcept { min.sub(p); max.sub(p); return *this; }
    inline SelfRef offset(const Tvector& p) noexcept { min.add(p); max.add(p); return *this; }
    inline SelfRef add(SelfCRef b, const Tvector& p) noexcept {
        min.add(b.min, p); max.add(b.max, p); return *this;
    }

    [[nodiscard]] constexpr BOOL contains(T _x, T _y, T _z) const noexcept {
        return (_x >= x1) && (_x <= x2) && (_y >= y1) && (_y <= y2) && (_z >= z1) && (_z <= z2);
    }
    
    [[nodiscard]] constexpr BOOL contains(const Tvector& p) const noexcept { return contains(p.x, p.y, p.z); }
    [[nodiscard]] constexpr BOOL contains(SelfCRef b) const noexcept { return contains(b.min) && contains(b.max); }
    [[nodiscard]] constexpr BOOL similar(SelfCRef b) const noexcept { return min.similar(b.min) && max.similar(b.max); }

    inline SelfRef modify(const Tvector& p) noexcept { min.min(p); max.max(p); return *this; }
    inline SelfRef modify(T _x, T _y, T _z) noexcept { return modify({_x, _y, _z}); }
    
    inline SelfRef merge(SelfCRef b) noexcept {
        modify(b.min); modify(b.max); return *this;
    }
    
    inline SelfRef merge(SelfCRef b1, SelfCRef b2) noexcept {
        invalidate(); merge(b1); merge(b2); return *this;
    }

    inline SelfRef xform(SelfCRef B, const Tmatrix& m) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            Tvector center, extents;
            B.get_CD(center, extents);
            
            Tvector new_center;
            m.transform_tiny(new_center, center);
            
            __m128 v_ext = _mm_set_ps(0.f, extents.z, extents.y, extents.x);
            
            __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
            
            __m128 m_i = _mm_and_ps(_mm_set_ps(0.f, m.i.z, m.i.y, m.i.x), abs_mask);
            __m128 m_j = _mm_and_ps(_mm_set_ps(0.f, m.j.z, m.j.y, m.j.x), abs_mask);
            __m128 m_k = _mm_and_ps(_mm_set_ps(0.f, m.k.z, m.k.y, m.k.x), abs_mask);
            
            __m128 ex = _mm_mul_ps(_mm_shuffle_ps(v_ext, v_ext, _MM_SHUFFLE(0,0,0,0)), m_i);
            __m128 ey = _mm_mul_ps(_mm_shuffle_ps(v_ext, v_ext, _MM_SHUFFLE(1,1,1,1)), m_j);
            __m128 ez = _mm_mul_ps(_mm_shuffle_ps(v_ext, v_ext, _MM_SHUFFLE(2,2,2,2)), m_k);
            
            __m128 new_ext = _mm_add_ps(_mm_add_ps(ex, ey), ez);
            
            Tvector final_ext;
            _mm_store_ss(&final_ext.x, new_ext);
            _mm_store_ss(&final_ext.y, _mm_shuffle_ps(new_ext, new_ext, _MM_SHUFFLE(1,1,1,1)));
            _mm_store_ss(&final_ext.z, _mm_shuffle_ps(new_ext, new_ext, _MM_SHUFFLE(2,2,2,2)));
            
            min.sub(new_center, final_ext);
            max.add(new_center, final_ext);
            return *this;
        } else {
            Tvector center, extents;
            B.get_CD(center, extents);
            
            Tvector new_center;
            m.transform_tiny(new_center, center);
            
            Tvector new_extents;
            new_extents.x = std::abs(m.i.x)*extents.x + std::abs(m.j.x)*extents.y + std::abs(m.k.x)*extents.z;
            new_extents.y = std::abs(m.i.y)*extents.x + std::abs(m.j.y)*extents.y + std::abs(m.k.y)*extents.z;
            new_extents.z = std::abs(m.i.z)*extents.x + std::abs(m.j.z)*extents.y + std::abs(m.k.z)*extents.z;
            
            min.sub(new_center, new_extents);
            max.add(new_center, new_extents);
            return *this;
        }
    }
    
    inline SelfRef xform(const Tmatrix& m) noexcept {
        Self b; b.set(*this); return xform(b, m);
    }

    inline void getsize(Tvector& R) const noexcept { R.sub(max, min); }
    
    inline void getradius(Tvector& R) const noexcept {
        getsize(R); R.mul(0.5f);
    }
    
    [[nodiscard]] inline T getradius() const noexcept {
        Tvector R; getradius(R); return R.magnitude();
    }
    
    [[nodiscard]] inline T getvolume() const noexcept {
        Tvector sz; getsize(sz); return sz.x * sz.y * sz.z;
    }
    
    inline SelfCRef getcenter(Tvector& C) const noexcept {
        C.x = (min.x + max.x) * 0.5f;
        C.y = (min.y + max.y) * 0.5f;
        C.z = (min.z + max.z) * 0.5f;
        return *this;
    }
    
    inline SelfCRef get_CD(Tvector& bc, Tvector& bd) const noexcept {
        bd.sub(max, min).mul(0.5f);
        bc.add(min, bd);
        return *this;
    }
    
    inline SelfRef scale(float s) noexcept {
        Tvector bd;
        bd.sub(max, min).mul(s);
        grow(bd);
        return *this;
    }
    
    inline SelfCRef getsphere(Tvector& C, T& R) const noexcept {
        getcenter(C);
        R = C.distance_to(max);
        return *this;
    }

    [[nodiscard]] constexpr BOOL intersect(SelfCRef box) const noexcept {
        if (max.x < box.min.x) return FALSE;
        if (max.y < box.min.y) return FALSE;
        if (max.z < box.min.z) return FALSE;
        if (min.x > box.max.x) return FALSE;
        if (min.y > box.max.y) return FALSE;
        if (min.z > box.max.z) return FALSE;
        return TRUE;
    }

    [[nodiscard]] BOOL Pick(const Tvector& start, const Tvector& dir) const noexcept {
        T alpha, xt, yt, zt;
        Tvector rvmin, rvmax;

        rvmin.sub(min, start);
        rvmax.sub(max, start);

        if (!fis_zero(dir.x)) {
            alpha = rvmin.x / dir.x; yt = alpha * dir.y;
            if (yt >= rvmin.y && yt <= rvmax.y) {
                zt = alpha * dir.z; if (zt >= rvmin.z && zt <= rvmax.z) return TRUE;
            }
            alpha = rvmax.x / dir.x; yt = alpha * dir.y;
            if (yt >= rvmin.y && yt <= rvmax.y) {
                zt = alpha * dir.z; if (zt >= rvmin.z && zt <= rvmax.z) return TRUE;
            }
        }

        if (!fis_zero(dir.y)) {
            alpha = rvmin.y / dir.y; xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) {
                zt = alpha * dir.z; if (zt >= rvmin.z && zt <= rvmax.z) return TRUE;
            }
            alpha = rvmax.y / dir.y; xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) {
                zt = alpha * dir.z; if (zt >= rvmin.z && zt <= rvmax.z) return TRUE;
            }
        }

        if (!fis_zero(dir.z)) {
            alpha = rvmin.z / dir.z; xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) {
                yt = alpha * dir.y; if (yt >= rvmin.y && yt <= rvmax.y) return TRUE;
            }
            alpha = rvmax.z / dir.z; xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) {
                yt = alpha * dir.y; if (yt >= rvmin.y && yt <= rvmax.y) return TRUE;
            }
        }
        return FALSE;
    }

    enum ERP_Result {
        rpNone = 0,
        rpOriginInside = 1,
        rpOriginOutside = 2,
        fcv_forcedword = u32(-1)
    };

    [[nodiscard]] ERP_Result Pick2(const Tvector& origin, const Tvector& dir, Tvector& coord) const noexcept {
        BOOL Inside = TRUE;
        Tvector MaxT;
        MaxT.x = MaxT.y = MaxT.z = -1.0f;

        auto IR = [](T& x) -> u32& { return reinterpret_cast<u32&>(x); };

        for (int i = 0; i < 3; ++i) {
            if (origin[i] < min[i]) {
                coord[i] = min[i]; Inside = FALSE;
                if (IR(dir[i])) MaxT[i] = (min[i] - origin[i]) / dir[i];
            } else if (origin[i] > max[i]) {
                coord[i] = max[i]; Inside = FALSE;
                if (IR(dir[i])) MaxT[i] = (max[i] - origin[i]) / dir[i];
            }
        }

        if (Inside) {
            coord = origin;
            return rpOriginInside;
        }

        u32 WhichPlane = 0;
        if (MaxT[1] > MaxT[0]) WhichPlane = 1;
        if (MaxT[2] > MaxT[WhichPlane]) WhichPlane = 2;

        if (IR(MaxT[WhichPlane]) & 0x80000000) return rpNone;

        for (int i = 0; i < 3; ++i) {
            if (i != WhichPlane) {
                coord[i] = origin[i] + MaxT[WhichPlane] * dir[i];
                if (coord[i] < min[i] || coord[i] > max[i]) return rpNone;
            }
        }
        
        return rpOriginOutside;
    }

    inline void getpoint(int index, Tvector& result) const noexcept {
        switch (index) {
        case 0: result.set(min.x, min.y, min.z); break;
        case 1: result.set(min.x, min.y, max.z); break;
        case 2: result.set(max.x, min.y, max.z); break;
        case 3: result.set(max.x, min.y, min.z); break;
        case 4: result.set(min.x, max.y, min.z); break;
        case 5: result.set(min.x, max.y, max.z); break;
        case 6: result.set(max.x, max.y, max.z); break;
        case 7: result.set(max.x, max.y, min.z); break;
        default: result.set(0, 0, 0); break;
        }
    }
    
    inline void getpoints(Tvector* result) const noexcept {
        result[0].set(min.x, min.y, min.z);
        result[1].set(min.x, min.y, max.z);
        result[2].set(max.x, min.y, max.z);
        result[3].set(max.x, min.y, min.z);
        result[4].set(min.x, max.y, min.z);
        result[5].set(min.x, max.y, max.z);
        result[6].set(max.x, max.y, max.z);
        result[7].set(max.x, max.y, min.z);
    }

    inline SelfRef modify(SelfCRef src, const Tmatrix& M) noexcept {
        Tvector pt;
        for (int i = 0; i < 8; i++) {
            src.getpoint(i, pt);
            M.transform_tiny(pt);
            modify(pt);
        }
        return *this;
    }
};

using Fbox = _box3<float>;
using Fbox3 = _box3<float>;
using Dbox = _box3<double>;
using Dbox3 = _box3<double>;

template <class T>
[[nodiscard]] constexpr bool _valid(const _box3<T>& c) noexcept {
    return _valid(c.min) && _valid(c.max);
}

#endif // __FBOX