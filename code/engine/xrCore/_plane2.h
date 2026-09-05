#pragma once
#ifndef _PLANE2_H
#define _PLANE2_H

#include <cmath>

template <class T>
class _plane2 {
public:
    using TYPE = T;
    using Self = _plane2<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;

    _vector2<T> n{};
    T d{0};

    inline SelfRef set(SelfCRef P) noexcept {
        n.set(P.n);
        d = P.d;
        return *this;
    }

    [[nodiscard]] inline BOOL similar(SelfCRef P, T eps_n = EPS, T eps_d = EPS) const noexcept {
        return (n.similar(P.n, eps_n) && (std::abs(d - P.d) < eps_d));
    }

    inline SelfRef build(const _vector2<T>& _p, const _vector2<T>& _n) noexcept {
        d = -n.normalize(_n).dotproduct(_p);
        return *this;
    }

    inline SelfRef project(_vector2<T>& pdest, const _vector2<T>& psrc) const noexcept {
        pdest.mad(psrc, n, -classify(psrc));
        return const_cast<SelfRef>(*this);
    }

    [[nodiscard]] inline T classify(const _vector2<T>& v) const noexcept { 
        return n.dotproduct(v) + d; 
    }

    inline SelfRef normalize() noexcept {
        T denom = 1.0f / n.magnitude();
        n.mul(denom);
        d *= denom;
        return *this;
    }

    [[nodiscard]] inline T distance(const _vector2<T>& v) const noexcept { 
        return std::abs(classify(v)); 
    }

    [[nodiscard]] inline BOOL intersectRayDist(const _vector2<T>& P, const _vector2<T>& D, T& dist) const noexcept {
        T numer = classify(P);
        T denom = n.dotproduct(D);

        if (std::abs(denom) < EPS_S) {
            return FALSE; // Normal is orthogonal to vector, can't intersect
        } else {
            dist = -(numer / denom);
            return ((dist > 0.0f) || fis_zero(dist));
        }
    }

    [[nodiscard]] inline BOOL intersectRayPoint(const _vector2<T>& P, const _vector2<T>& D, _vector2<T>& dest) const noexcept {
        T numer = classify(P);
        T denom = n.dotproduct(D);

        if (std::abs(denom) < EPS_S) {
            return FALSE; // Normal is orthogonal to vector, can't intersect
        } else {
            T dist = -(numer / denom);
            dest.mad(P, D, dist);
            return ((dist > 0.0f) || fis_zero(dist));
        }
    }

    [[nodiscard]] inline BOOL intersect(const _vector2<T>& u, const _vector2<T>& v, _vector2<T>& isect) const noexcept {
        T denom, dist;
        _vector2<T> t;

        t.sub(v, u);
        denom = n.dotproduct(t);
        
        if (std::abs(denom) < EPS) {
            return FALSE; // They are parallel
        }

        dist = -(n.dotproduct(u) + d) / denom;
        if (dist < -EPS || dist > 1.0f + EPS) {
            return FALSE;
        }
        
        isect.mad(u, t, dist);
        return TRUE;
    }

    [[nodiscard]] inline BOOL intersect_2(const _vector2<T>& u, const _vector2<T>& v, _vector2<T>& isect) const noexcept {
        T dist1, dist2;
        _vector2<T> t;

        dist1 = n.dotproduct(u) + d;
        dist2 = n.dotproduct(v) + d;

        if (dist1 * dist2 < 0.0f) {
            return FALSE;
        }

        t.sub(v, u);
        isect.mad(u, t, dist1 / std::abs(dist1 - dist2));
        return TRUE;
    }
};

using Fplane2 = _plane2<float>;
using Dplane2 = _plane2<double>;

namespace xr {
    template <class T>
    [[nodiscard]] inline bool valid(const _plane2<T>& p) noexcept {
        return valid(p.n) && valid(p.d);
    }
} // namespace xr

#endif // _PLANE2_H