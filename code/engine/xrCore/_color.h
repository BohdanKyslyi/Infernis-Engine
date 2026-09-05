#pragma once
#ifndef __COLOR_H
#define __COLOR_H

#include <cmath>
#include <immintrin.h>

// maps unsigned 8 bits/channel to D3DCOLOR
[[nodiscard]] inline u32 color_argb(u32 a, u32 r, u32 g, u32 b) noexcept {
    return ((a & 0xff) << 24) | ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}
[[nodiscard]] inline u32 color_rgba(u32 r, u32 g, u32 b, u32 a) noexcept { return color_argb(a, r, g, b); }

[[nodiscard]] inline u32 color_argb_f(f32 a, f32 r, f32 g, f32 b) noexcept {
    s32 _r = clampr(iFloor(r * 255.f), 0, 255);
    s32 _g = clampr(iFloor(g * 255.f), 0, 255);
    s32 _b = clampr(iFloor(b * 255.f), 0, 255);
    s32 _a = clampr(iFloor(a * 255.f), 0, 255);
    return color_argb(_a, _r, _g, _b);
}
[[nodiscard]] inline u32 color_rgba_f(f32 r, f32 g, f32 b, f32 a) noexcept { return color_argb_f(a, r, g, b); }
[[nodiscard]] inline u32 color_xrgb(u32 r, u32 g, u32 b) noexcept { return color_argb(0xff, r, g, b); }

[[nodiscard]] constexpr u32 color_get_R(u32 rgba) noexcept { return (((rgba) >> 16) & 0xff); }
[[nodiscard]] constexpr u32 color_get_G(u32 rgba) noexcept { return (((rgba) >> 8) & 0xff); }
[[nodiscard]] constexpr u32 color_get_B(u32 rgba) noexcept { return ((rgba) & 0xff); }
[[nodiscard]] constexpr u32 color_get_A(u32 rgba) noexcept { return ((rgba) >> 24); }

[[nodiscard]] inline u32 subst_alpha(u32 rgba, u32 a) noexcept {
    return (rgba & ~color_rgba(0, 0, 0, 0xff)) | color_rgba(0, 0, 0, a);
}
[[nodiscard]] inline u32 bgr2rgb(u32 bgr) noexcept {
    return color_rgba(color_get_B(bgr), color_get_G(bgr), color_get_R(bgr), 0);
}
[[nodiscard]] inline u32 rgb2bgr(u32 rgb) noexcept { return bgr2rgb(rgb); }

template <class T>
struct _color {
    using Self = _color<T>;
    using SelfRef = Self&;
    using SelfCRef = const Self&;

    T r{0}, g{0}, b{0}, a{0};

    inline SelfRef set(u32 dw) noexcept {
        const T f = T(1.0) / T(255.0);
        a = f * T((dw >> 24) & 0xff);
        r = f * T((dw >> 16) & 0xff);
        g = f * T((dw >> 8) & 0xff);
        b = f * T((dw >> 0) & 0xff);
        return *this;
    }

    inline SelfRef set(T _r, T _g, T _b, T _a) noexcept {
        r = _r; g = _g; b = _b; a = _a;
        return *this;
    }

    inline SelfRef set(SelfCRef dw) noexcept {
        r = dw.r; g = dw.g; b = dw.b; a = dw.a;
        return *this;
    }

    [[nodiscard]] inline u32 get() const noexcept { return color_rgba_f(r, g, b, a); }

    [[nodiscard]] inline u32 get_windows() const noexcept {
        auto _a = static_cast<BYTE>(a * 255.f);
        auto _r = static_cast<BYTE>(r * 255.f);
        auto _g = static_cast<BYTE>(g * 255.f);
        auto _b = static_cast<BYTE>(b * 255.f);
        return (static_cast<u32>(_a << 24) | (_b << 16) | (_g << 8) | (_r));
    }

    inline SelfRef set_windows(u32 dw) noexcept {
        const T f = 1.0f / 255.0f;
        a = f * static_cast<T>(static_cast<BYTE>(dw >> 24));
        b = f * static_cast<T>(static_cast<BYTE>(dw >> 16));
        g = f * static_cast<T>(static_cast<BYTE>(dw >> 8));
        r = f * static_cast<T>(static_cast<BYTE>(dw >> 0));
        return *this;
    }

    inline SelfRef adjust_contrast(T f) noexcept {
        r = 0.5f + f * (r - 0.5f);
        g = 0.5f + f * (g - 0.5f);
        b = 0.5f + f * (b - 0.5f);
        return *this;
    }

    inline SelfRef adjust_contrast(SelfCRef in, T f) noexcept {
        r = 0.5f + f * (in.r - 0.5f);
        g = 0.5f + f * (in.g - 0.5f);
        b = 0.5f + f * (in.b - 0.5f);
        return *this;
    }

    inline SelfRef adjust_saturation(T s) noexcept {
        T grey = r * 0.2125f + g * 0.7154f + b * 0.0721f;
        r = grey + s * (r - grey);
        g = grey + s * (g - grey);
        b = grey + s * (b - grey);
        return *this;
    }

    inline SelfRef adjust_saturation(SelfCRef in, T s) noexcept {
        T grey = in.r * 0.2125f + in.g * 0.7154f + in.b * 0.0721f;
        r = grey + s * (in.r - grey);
        g = grey + s * (in.g - grey);
        b = grey + s * (in.b - grey);
        return *this;
    }

    inline SelfRef modulate(SelfCRef in) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            _mm_storeu_ps(&r, _mm_mul_ps(_mm_loadu_ps(&r), _mm_loadu_ps(&in.r)));
        } else {
            r *= in.r; g *= in.g; b *= in.b; a *= in.a;
        }
        return *this;
    }

    inline SelfRef modulate(SelfCRef in1, SelfCRef in2) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            _mm_storeu_ps(&r, _mm_mul_ps(_mm_loadu_ps(&in1.r), _mm_loadu_ps(&in2.r)));
        } else {
            r = in1.r * in2.r; g = in1.g * in2.g; b = in1.b * in2.b; a = in1.a * in2.a;
        }
        return *this;
    }

    inline SelfRef negative(SelfCRef in) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            __m128 ones = _mm_set1_ps(1.0f);
            _mm_storeu_ps(&r, _mm_sub_ps(ones, _mm_loadu_ps(&in.r)));
        } else {
            r = 1.0f - in.r; g = 1.0f - in.g; b = 1.0f - in.b; a = 1.0f - in.a;
        }
        return *this;
    }

    inline SelfRef negative() noexcept {
        if constexpr (std::is_same_v<T, float>) {
            __m128 ones = _mm_set1_ps(1.0f);
            _mm_storeu_ps(&r, _mm_sub_ps(ones, _mm_loadu_ps(&r)));
        } else {
            r = 1.0f - r; g = 1.0f - g; b = 1.0f - b; a = 1.0f - a;
        }
        return *this;
    }

    inline SelfRef sub_rgb(T s) noexcept {
        r -= s; g -= s; b -= s;
        return *this;
    }

    inline SelfRef add_rgb(T s) noexcept {
        r += s; g += s; b += s;
        return *this;
    }

    inline SelfRef add_rgba(T s) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            _mm_storeu_ps(&r, _mm_add_ps(_mm_loadu_ps(&r), _mm_set1_ps(s)));
        } else {
            r += s; g += s; b += s; a += s;
        }
        return *this;
    }

    inline SelfRef mul_rgba(T s) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            _mm_storeu_ps(&r, _mm_mul_ps(_mm_loadu_ps(&r), _mm_set1_ps(s)));
        } else {
            r *= s; g *= s; b *= s; a *= s;
        }
        return *this;
    }

    inline SelfRef mul_rgb(T s) noexcept {
        r *= s; g *= s; b *= s;
        return *this;
    }

    inline SelfRef mul_rgba(SelfCRef c, T s) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            _mm_storeu_ps(&r, _mm_mul_ps(_mm_loadu_ps(&c.r), _mm_set1_ps(s)));
        } else {
            r = c.r * s; g = c.g * s; b = c.b * s; a = c.a * s;
        }
        return *this;
    }

    inline SelfRef mul_rgb(SelfCRef c, T s) noexcept {
        r = c.r * s;
        g = c.g * s;
        b = c.b * s;
        return *this;
    }

    [[nodiscard]] inline T magnitude_sqr_rgb() const noexcept { return r * r + g * g + b * b; }
    [[nodiscard]] inline T magnitude_rgb() const noexcept { return std::sqrt(magnitude_sqr_rgb()); }
    [[nodiscard]] inline T intensity() const noexcept { return (r + g + b) / 3.f; }

    inline SelfRef normalize_rgb() noexcept {
        VERIFY(magnitude_sqr_rgb() > EPS_S);
        return mul_rgb(1.f / magnitude_rgb());
    }

    inline SelfRef normalize_rgb(SelfCRef c) noexcept {
        VERIFY(c.magnitude_sqr_rgb() > EPS_S);
        return mul_rgb(c, 1.f / c.magnitude_rgb());
    }

    inline SelfRef lerp(SelfCRef c1, SelfCRef c2, T t) noexcept {
        if constexpr (std::is_same_v<T, float>) {
            __m128 v1 = _mm_loadu_ps(&c1.r);
            __m128 v2 = _mm_loadu_ps(&c2.r);
            __m128 vt = _mm_set1_ps(t);
            // v1 + (v2 - v1) * t => FMADD
            _mm_storeu_ps(&r, _mm_add_ps(v1, _mm_mul_ps(_mm_sub_ps(v2, v1), vt)));
        } else {
            T invt = 1.f - t;
            r = c1.r * invt + c2.r * t;
            g = c1.g * invt + c2.g * t;
            b = c1.b * invt + c2.b * t;
            a = c1.a * invt + c2.a * t;
        }
        return *this;
    }

    inline SelfRef lerp(SelfCRef c1, SelfCRef c2, SelfCRef c3, T t) noexcept {
        if (t > .5f) {
            return lerp(c2, c3, t * 2.f - 1.f);
        } else {
            return lerp(c1, c2, t * 2.f);
        }
    }

    [[nodiscard]] inline BOOL similar_rgba(SelfCRef v, T E = EPS_L) const noexcept {
        return std::abs(r - v.r) < E && std::abs(g - v.g) < E && std::abs(b - v.b) < E && std::abs(a - v.a) < E;
    }

    [[nodiscard]] inline BOOL similar_rgb(SelfCRef v, T E = EPS_L) const noexcept {
        return std::abs(r - v.r) < E && std::abs(g - v.g) < E && std::abs(b - v.b) < E;
    }
};

using Fcolor = _color<float>;
using Dcolor = _color<double>;

namespace xr {
    template <class T>
    [[nodiscard]] inline bool valid(const _color<T>& c) noexcept {
        return xr::valid(c.r) && xr::valid(c.g) && xr::valid(c.b) && xr::valid(c.a);
    }
} // namespace xr

#endif // __COLOR_H