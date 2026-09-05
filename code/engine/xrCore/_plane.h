#pragma once
#ifndef _PLANE_H
#define _PLANE_H

#include <cmath>

template <class T>
class _plane {
public:
    using TYPE = T;
    using Self = _plane<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;

    _vector3<T> n{};
    T d{0};

    inline SelfRef set(SelfCRef P) noexcept {
        n.set(P.n);
        d = P.d;
        return *this;
    }

    [[nodiscard]] inline BOOL similar(SelfCRef P, T eps_n = EPS, T eps_d = EPS) const noexcept {
        return (n.similar(P.n, eps_n) && (std::abs(d - P.d) < eps_d));
    }

    inline SelfRef build(const _vector3<T>& v1, const _vector3<T>& v2, const _vector3<T>& v3) noexcept {
        _vector3<T> t1, t2;
        n.crossproduct(t1.sub(v1, v2), t2.sub(v1, v3)).normalize();
        d = -n.dotproduct(v1);
        return *this;
    }

    inline SelfRef build_precise(const _vector3<T>& v1, const _vector3<T>& v2, const _vector3<T>& v3) noexcept {
        _vector3<T> t1, t2;
        n.crossproduct(t1.sub(v1, v2), t2.sub(v1, v3));
        exact_normalize(n);
        d = -n.dotproduct(v1);
        return *this;
    }

    inline SelfRef build(const _vector3<T>& _p, const _vector3<T>& _n) noexcept {
        d = -n.normalize(_n).dotproduct(_p);
        return *this;
    }

    inline SelfRef build_unit_normal(const _vector3<T>& _p, const _vector3<T>& _n) noexcept {
        VERIFY(fsimilar(_n.magnitude(), 1.0f, EPS));
        d = -n.set(_n).dotproduct(_p);
        return *this;
    }

    inline SelfCRef project(_vector3<T>& pdest, const _vector3<T>& psrc) const noexcept {
        pdest.mad(psrc, n, -classify(psrc));
        return *this;
    }

    inline SelfRef project(_vector3<T>& pdest, const _vector3<T>& psrc) noexcept {
        pdest.mad(psrc, n, -classify(psrc));
        return *this;
    }

    [[nodiscard]] inline T classify(const _vector3<T>& v) const noexcept { 
        return n.dotproduct(v) + d; 
    }

    inline SelfRef normalize() noexcept {
        T denom = 1.f / n.magnitude();
        n.mul(denom);
        d *= denom;
        return *this;
    }

    [[nodiscard]] inline T distance(const _vector3<T>& v) const noexcept { 
        return std::abs(classify(v)); 
    }

    [[nodiscard]] inline BOOL intersectRayDist(const _vector3<T>& P, const _vector3<T>& D, T& dist) const noexcept {
        T numer = classify(P);
        T denom = n.dotproduct(D);

        if (std::abs(denom) < EPS_S) {
            return FALSE; // normal is orthogonal to vector3, cant intersect
        }

        dist = -(numer / denom);
        return ((dist > 0.f) || fis_zero(dist));
    }

    [[nodiscard]] inline BOOL intersectRayPoint(const _vector3<T>& P, const _vector3<T>& D, _vector3<T>& dest) const noexcept {
        T numer = classify(P);
        T denom = n.dotproduct(D);

        if (std::abs(denom) < EPS_S) {
            return FALSE; // normal is orthogonal to vector3, cant intersect
        } else {
            T dist = -(numer / denom);
            dest.mad(P, D, dist);
            return ((dist > 0.f) || fis_zero(dist));
        }
    }

    [[nodiscard]] inline BOOL intersect(const _vector3<T>& u, const _vector3<T>& v, _vector3<T>& isect) const noexcept {
        T denom, dist;
        _vector3<T> t;

        t.sub(v, u);
        denom = n.dotproduct(t);
        if (std::abs(denom) < EPS) {
            return FALSE; // they are parallel
        }

        dist = -(n.dotproduct(u) + d) / denom;
        if (dist < -EPS || dist > 1.0f + EPS) {
            return FALSE;
        }
        isect.mad(u, t, dist);
        return TRUE;
    }

    [[nodiscard]] inline BOOL intersect_2(const _vector3<T>& u, const _vector3<T>& v, _vector3<T>& isect) const noexcept {
        T dist1, dist2;
        _vector3<T> t;

        dist1 = n.dotproduct(u) + d;
        dist2 = n.dotproduct(v) + d;

        if (dist1 * dist2 < 0.0f) {
            return FALSE;
        }

        t.sub(v, u);
        isect.mad(u, t, dist1 / std::abs(dist1 - dist2));
        return TRUE;
    }

    inline SelfRef transform(SelfCRef P, const _matrix<T>& M) noexcept {
        _vector3<T> point, transformedPoint;
        point.mul(P.n, -P.d);
        M.transform_dir(n, P.n);
        n.normalize();
        M.transform_tiny(transformedPoint, point);
        d = -n.dotproduct(transformedPoint);
        return *this;
    }
};

using Fplane = _plane<float>;
using Dplane = _plane<double>;

namespace xr {
    template <class T>
    [[nodiscard]] inline bool valid(const _plane<T>& s) noexcept {
        return valid(s.n) && valid(s.d);
    }
} // namespace xr

#endif // _PLANE_H
