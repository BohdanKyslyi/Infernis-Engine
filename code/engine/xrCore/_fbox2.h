#pragma once
#ifndef __FBOX2
#define __FBOX2

#include <cmath>

template <class T>
class _box2 {
public:
    using TYPE = T;
    using Self = _box2<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;
    using Tvector = _vector2<T>;

    union {
        struct {
            Tvector min;
            Tvector max;
        };
        struct {
            T x1, y1;
            T x2, y2;
        };
    };

    inline SelfRef set(const Tvector& _min, const Tvector& _max) noexcept {
        min.set(_min);
        max.set(_max);
        return *this;
    }
    
    inline SelfRef set(T _x1, T _y1, T _x2, T _y2) noexcept {
        min.set(_x1, _y1);
        max.set(_x2, _y2);
        return *this;
    }
    
    inline SelfRef set(SelfCRef b) noexcept {
        min.set(b.min);
        max.set(b.max);
        return *this;
    }

    inline SelfRef null() noexcept {
        min.set(0.f, 0.f);
        max.set(0.f, 0.f);
        return *this;
    }
    
    inline SelfRef identity() noexcept {
        min.set(-0.5f, -0.5f);
        max.set(0.5f, 0.5f);
        return *this;
    }
    
    inline SelfRef invalidate() noexcept {
        min.set(type_max<T>, type_max<T>);
        max.set(type_min<T>, type_min<T>);
        return *this;
    }

    inline SelfRef shrink(T s) noexcept {
        min.add(s);
        max.sub(s);
        return *this;
    }
    
    inline SelfRef shrink(const Tvector& s) noexcept {
        min.add(s);
        max.sub(s);
        return *this;
    }
    
    inline SelfRef grow(T s) noexcept {
        min.sub(s);
        max.add(s);
        return *this;
    }
    
    inline SelfRef grow(const Tvector& s) noexcept {
        min.sub(s);
        max.add(s);
        return *this;
    }

    inline SelfRef add(const Tvector& p) noexcept {
        min.add(p);
        max.add(p);
        return *this;
    }
    
    inline SelfRef offset(const Tvector& p) noexcept {
        min.add(p);
        max.add(p);
        return *this;
    }
    
    inline SelfRef add(SelfCRef b, const Tvector& p) noexcept {
        min.add(b.min, p);
        max.add(b.max, p);
        return *this;
    }

    [[nodiscard]] constexpr BOOL contains(T x, T y) const noexcept { 
        return (x >= x1) && (x <= x2) && (y >= y1) && (y <= y2); 
    }
    
    [[nodiscard]] constexpr BOOL contains(const Tvector& p) const noexcept { 
        return contains(p.x, p.y); 
    }
    
    [[nodiscard]] constexpr BOOL contains(SelfCRef b) const noexcept { 
        return contains(b.min) && contains(b.max); 
    }

    [[nodiscard]] constexpr BOOL similar(SelfCRef b) const noexcept { 
        return min.similar(b.min) && max.similar(b.max); 
    }

    inline SelfRef modify(const Tvector& p) noexcept {
        min.min(p);
        max.max(p);
        return *this;
    }
    
    inline SelfRef merge(SelfCRef b) noexcept {
        modify(b.min);
        modify(b.max);
        return *this;
    }
    
    inline SelfRef merge(SelfCRef b1, SelfCRef b2) noexcept {
        invalidate();
        merge(b1);
        merge(b2);
        return *this;
    }

    inline void getsize(Tvector& R) const noexcept { R.sub(max, min); }
    
    inline void getradius(Tvector& R) const noexcept {
        getsize(R);
        R.mul(0.5f);
    }
    
    [[nodiscard]] inline T getradius() const noexcept {
        Tvector R;
        getsize(R);
        R.mul(0.5f);
        return R.magnitude();
    }

    inline void getcenter(Tvector& C) const noexcept {
        C.x = (min.x + max.x) * 0.5f;
        C.y = (min.y + max.y) * 0.5f;
    }
    
    inline void getsphere(Tvector& C, T& R) const noexcept {
        getcenter(C);
        R = C.distance_to(max);
    }

    [[nodiscard]] constexpr BOOL intersect(SelfCRef box) const noexcept {
        if (max.x < box.min.x) return FALSE;
        if (max.y < box.min.y) return FALSE;
        if (min.x > box.max.x) return FALSE;
        if (min.y > box.max.y) return FALSE;
        return TRUE;
    }

    inline SelfRef sort() noexcept {
        if (min.x > max.x) std::swap(min.x, max.x);
        if (min.y > max.y) std::swap(min.y, max.y);
        return *this;
    }

    [[nodiscard]] BOOL Pick(const Tvector& start, const Tvector& dir) const noexcept {
        T alpha, xt, yt;
        Tvector rvmin, rvmax;

        rvmin.sub(min, start);
        rvmax.sub(max, start);

        if (!fis_zero(dir.x)) {
            alpha = rvmin.x / dir.x;
            yt = alpha * dir.y;
            if (yt >= rvmin.y && yt <= rvmax.y) return TRUE;
            
            alpha = rvmax.x / dir.x;
            yt = alpha * dir.y;
            if (yt >= rvmin.y && yt <= rvmax.y) return TRUE;
        }

        if (!fis_zero(dir.y)) {
            alpha = rvmin.y / dir.y;
            xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) return TRUE;
            
            alpha = rvmax.y / dir.y;
            xt = alpha * dir.x;
            if (xt >= rvmin.x && xt <= rvmax.x) return TRUE;
        }
        return FALSE;
    }

    [[nodiscard]] BOOL pick_exact(const Tvector& start, const Tvector& dir) const noexcept {
        T alpha, xt, yt;
        Tvector rvmin, rvmax;

        rvmin.sub(min, start);
        rvmax.sub(max, start);

        if (std::abs(dir.x) != 0) {
            alpha = rvmin.x / dir.x;
            yt = alpha * dir.y;
            if (yt >= rvmin.y - EPS && yt <= rvmax.y + EPS) return TRUE;
            
            alpha = rvmax.x / dir.x;
            yt = alpha * dir.y;
            if (yt >= rvmin.y - EPS && yt <= rvmax.y + EPS) return TRUE;
        }
        
        if (std::abs(dir.y) != 0) {
            alpha = rvmin.y / dir.y;
            xt = alpha * dir.x;
            if (xt >= rvmin.x - EPS && xt <= rvmax.x + EPS) return TRUE;
            
            alpha = rvmax.y / dir.y;
            xt = alpha * dir.x;
            if (xt >= rvmin.x - EPS && xt <= rvmax.x + EPS) return TRUE;
        }
        return FALSE;
    }

    [[nodiscard]] BOOL Pick2(const Tvector& origin, const Tvector& dir, Tvector& coord) const noexcept {
        BOOL Inside = TRUE;
        Tvector MaxT;
        MaxT.x = MaxT.y = -1.0f;

        auto IR = [](T& x) -> u32& { return reinterpret_cast<u32&>(x); };

        if (origin[0] < min[0]) {
            coord[0] = min[0]; Inside = FALSE;
            if (IR(dir[0])) MaxT[0] = (min[0] - origin[0]) / dir[0];
        } else if (origin[0] > max[0]) {
            coord[0] = max[0]; Inside = FALSE;
            if (IR(dir[0])) MaxT[0] = (max[0] - origin[0]) / dir[0];
        }

        if (origin[1] < min[1]) {
            coord[1] = min[1]; Inside = FALSE;
            if (IR(dir[1])) MaxT[1] = (min[1] - origin[1]) / dir[1];
        } else if (origin[1] > max[1]) {
            coord[1] = max[1]; Inside = FALSE;
            if (IR(dir[1])) MaxT[1] = (max[1] - origin[1]) / dir[1];
        }

        if (Inside) {
            coord = origin;
            return TRUE;
        }

        u32 WhichPlane = (MaxT[1] > MaxT[0]) ? 1 : 0;

        if (IR(MaxT[WhichPlane]) & 0x80000000) return FALSE;

        if (WhichPlane == 0) {
            coord[1] = origin[1] + MaxT[0] * dir[1];
            if ((coord[1] < min[1]) || (coord[1] > max[1])) return FALSE;
            return TRUE;
        } else {
            coord[0] = origin[0] + MaxT[1] * dir[0];
            if ((coord[0] < min[0]) || (coord[0] > max[0])) return FALSE;
            return TRUE;
        }
    }

    inline void getpoint(int index, Tvector& result) const noexcept {
        switch (index) {
        case 0: result.set(min.x, min.y); break;
        case 1: result.set(max.x, min.y); break;
        case 2: result.set(max.x, max.y); break;
        case 3: result.set(min.x, max.y); break;
        default: result.set(0.f, 0.f); break;
        }
    }
    
    inline void getpoints(Tvector* result) const noexcept {
        result[0].set(min.x, min.y);
        result[1].set(max.x, min.y);
        result[2].set(max.x, max.y);
        result[3].set(min.x, max.y);
    }
};

using Fbox2 = _box2<float>;
using Dbox2 = _box2<double>;

namespace xr {
    template <class T>
    [[nodiscard]] constexpr bool valid(const _box2<T>& c) noexcept {
        return valid(c.min) && valid(c.max);
    }
} 

#endif // __FBOX2