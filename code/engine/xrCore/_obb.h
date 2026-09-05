#pragma once
#ifndef FOBB_H
#define FOBB_H

#include <limits>

template <class T>
struct _obb {
public:
    using Self = _obb<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;
    using Tvector = _vector3<T>;
    using Tmatrix = _matrix<T>;

protected:
    [[nodiscard]] static bool clip(T fDenom, T fNumer, T& rfT0, T& rfT1) noexcept {
        // Return value is 'true' if line segment intersects the current test
        // plane.  Otherwise 'false' is returned in which case the line segment
        // is entirely clipped.

        if (fDenom > 0.0f) {
            if (fNumer > fDenom * rfT1)
                return false;
            if (fNumer > fDenom * rfT0)
                rfT0 = fNumer / fDenom;
            return true;
        } else if (fDenom < 0.0f) {
            if (fNumer > fDenom * rfT0)
                return false;
            if (fNumer > fDenom * rfT1)
                rfT1 = fNumer / fDenom;
            return true;
        } else {
            return fNumer <= 0.0f;
        }
    }
    
    [[nodiscard]] static bool intersect(const Tvector& start, const Tvector& dir, const Tvector& extent, T& rfT0, T& rfT1) noexcept {
        T fSaveT0 = rfT0, fSaveT1 = rfT1;

        bool bNotEntirelyClipped = clip(+dir.x, -start.x - extent[0], rfT0, rfT1) &&
                                   clip(-dir.x, +start.x - extent[0], rfT0, rfT1) &&
                                   clip(+dir.y, -start.y - extent[1], rfT0, rfT1) &&
                                   clip(-dir.y, +start.y - extent[1], rfT0, rfT1) &&
                                   clip(+dir.z, -start.z - extent[2], rfT0, rfT1) &&
                                   clip(-dir.z, +start.z - extent[2], rfT0, rfT1);

        return bNotEntirelyClipped && (rfT0 != fSaveT0 || rfT1 != fSaveT1);
    }

public:
    _matrix33<T> m_rotate{};
    Tvector m_translate{};
    Tvector m_halfsize{};

    inline SelfRef invalidate() noexcept {
        m_rotate.identity();
        m_translate.set(0, 0, 0);
        m_halfsize.set(0, 0, 0);
        return *this;
    }
    
    inline SelfRef identity() noexcept {
        invalidate();
        m_halfsize.set(T(0.5), T(0.5), T(0.5));
        return *this;
    }
    
    inline void xform_get(Tmatrix& D) const noexcept {
        D.i.set(m_rotate.i);
        D._14_ = 0;
        D.j.set(m_rotate.j);
        D._24_ = 0;
        D.k.set(m_rotate.k);
        D._34_ = 0;
        D.c.set(m_translate);
        D._44_ = 1;
    }
    
    inline SelfRef xform_set(const Tmatrix& S) noexcept {
        m_rotate.i.set(S.i);
        m_rotate.j.set(S.j);
        m_rotate.k.set(S.k);
        m_translate.set(S.c);
        return *this;
    }
    
    inline void xform_full(Tmatrix& D) const noexcept {
        Tmatrix R, S;
        xform_get(R);
        S.scale(m_halfsize);
        D.mul_43(R, S);
    }

    // NOTE: Unoptimized (as per original comment, left scalar logic)
    inline SelfRef transform(SelfCRef src, const Tmatrix& M) noexcept {
        Tmatrix srcR, destR;

        src.xform_get(srcR);
        destR.mul_43(M, srcR);
        xform_set(destR);
        m_halfsize.set(src.m_halfsize);
        return *this;
    }

    [[nodiscard]] inline bool intersect(const Tvector& start, const Tvector& dir, T& dist) const noexcept {
        // convert ray to box coordinates
        Tvector kDiff;
        kDiff.sub(start, m_translate);
        
        Tvector kOrigin;
        kOrigin.set(kDiff.dotproduct(m_rotate.i), kDiff.dotproduct(m_rotate.j), kDiff.dotproduct(m_rotate.k));
        
        Tvector kDirection;
        kDirection.set(dir.dotproduct(m_rotate.i), dir.dotproduct(m_rotate.j), dir.dotproduct(m_rotate.k));

        T fT0 = 0.0f, fT1 = std::numeric_limits<T>::max();
        
        if (intersect(kOrigin, kDirection, m_halfsize, fT0, fT1)) {
            bool bPick = false;
            if (fT0 > 0.0f) {
                if (fT0 < dist) {
                    dist = fT0;
                    bPick = true;
                }
                if (fT1 < dist) {
                    dist = fT1;
                    bPick = true;
                }
            } else {
                if (fT1 < dist) {
                    dist = fT1;
                    bPick = true;
                }
            }
            return bPick;
        }
        return false;
    }
};

using Fobb = _obb<float>;
using Dobb = _obb<double>;

template <class T>
[[nodiscard]] inline bool _valid(const _obb<T>& m) noexcept {
    return _valid(m.m_rotate) && _valid(m.m_translate) && _valid(m.m_halfsize);
}

#endif // FOBB_H