#include "stdafx.h"
#pragma hdrstop

#include "noise.h"
#include <cmath>
#include <array>
#include <random>

#ifndef _EDITOR
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

// Perlin's noise from Texturing and Modeling...
constexpr int B = 256;

static std::array<int, B + B + 2> p;
static std::array<std::array<float, 3>, B + B + 2> g;

[[nodiscard]] inline constexpr float DOT(const float* a, const float* b) noexcept {
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
}

[[nodiscard]] inline constexpr float AT(float rx, float ry, float rz, const float* q) noexcept {
    return (rx * q[0] + ry * q[1] + rz * q[2]);
}

template <typename T>
[[nodiscard]] inline constexpr T S_CURVE(T t) noexcept {
    return t * t * (3.0f - 2.0f * t);
}

template <typename T>
[[nodiscard]] inline constexpr T LERP(T t, T a, T b) noexcept {
    return a + t * (b - a);
}

void noise3Init() {
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> dist_v(-1.0f, 1.0f);
    std::uniform_int_distribution<int> dist_idx(0, B - 1);

    for (int i = 0; i < B; i++) {
        float s;
        std::array<float, 3> v;
        do {
            for (int j = 0; j < 3; j++) {
                v[j] = dist_v(rng);
            }
            s = DOT(v.data(), v.data());
        } while (s > 1.0f || s == 0.0f);
        
        s = std::sqrt(s);
        for (int j = 0; j < 3; j++) {
            g[i][j] = v[j] / s;
        }
    }

    for (int i = 0; i < B; i++) {
        p[i] = i;
    }

    for (int i = B; i > 0; i -= 2) {
        int rnd = dist_idx(rng);
        int k = p[i];
        p[i] = p[rnd];
        p[rnd] = k;
    }

    for (int i = 0; i < B + 2; i++) {
        p[B + i] = p[i];
        for (int j = 0; j < 3; j++) {
            g[B + i][j] = g[i][j];
        }
    }
}

float noise3(const Fvector& vec) {
    alignas(16) int b0[4], b1[4];
    alignas(16) float r0[4], r1[4];

#ifndef _EDITOR
    // Апаратна акселерація (SSE2)
    // Замість трьох окремих прорахунків X, Y, Z, всі координати обчислюються одночасно в 128-бітному регістрі
    
    __m128 v_vec = _mm_set_ps(0.0f, vec[2], vec[1], vec[0]); // Завантажуємо координати [0, Z, Y, X]
    __m128 v_10k = _mm_set1_ps(10000.0f);
    __m128 v_t = _mm_add_ps(v_vec, v_10k);                   // t = vec[i] + 10000.f
    
    __m128i v_tt = _mm_cvttps_epi32(v_t);                    // tt = iFloor_SSE(t)
    
    __m128 v_tt_float = _mm_cvtepi32_ps(v_tt);
    __m128 v_r0 = _mm_sub_ps(v_t, v_tt_float);               // r0 = t - float(tt)
    
    __m128 v_1 = _mm_set1_ps(1.0f);
    __m128 v_r1 = _mm_sub_ps(v_r0, v_1);                     // r1 = r0 - 1.f

    // Витягування результатів
    alignas(16) int tt_arr[4];
    _mm_store_si128((__m128i*)tt_arr, v_tt);
    _mm_store_ps(r0, v_r0);
    _mm_store_ps(r1, v_r1);

    for (int i = 0; i < 3; ++i) {
        b0[i] = tt_arr[i] & (B - 1);
        b1[i] = (b0[i] + 1) & (B - 1);
    }
#else
    // Fallback для редактора (без SIMD)
    for (int i = 0; i < 3; ++i) {
        float t = vec[i] + 10000.f;
        int tt = static_cast<int>(std::floor(t));
        b0[i] = tt & (B - 1);
        b1[i] = (b0[i] + 1) & (B - 1);
        r0[i] = t - std::floor(t);
        r1[i] = r0[i] - 1.f;
    }
#endif

    int i = p[b0[0]];
    int j = p[b1[0]];

    int b00 = p[i + b0[1]];
    int b10 = p[j + b0[1]];
    int b01 = p[i + b1[1]];
    int b11 = p[j + b1[1]];

    float sx = S_CURVE(r0[0]);
    float sy = S_CURVE(r0[1]);
    float sz = S_CURVE(r0[2]);

    const float* q;
    float a, b, c, d, u, v;

    q = g[b00 + b0[2]].data(); u = AT(r0[0], r0[1], r0[2], q);
    q = g[b10 + b0[2]].data(); v = AT(r1[0], r0[1], r0[2], q);
    a = LERP(sx, u, v);

    q = g[b01 + b0[2]].data(); u = AT(r0[0], r1[1], r0[2], q);
    q = g[b11 + b0[2]].data(); v = AT(r1[0], r1[1], r0[2], q);
    b = LERP(sx, u, v);

    c = LERP(sy, a, b);

    q = g[b00 + b1[2]].data(); u = AT(r0[0], r0[1], r1[2], q);
    q = g[b10 + b1[2]].data(); v = AT(r1[0], r0[1], r1[2], q);
    a = LERP(sx, u, v);

    q = g[b01 + b1[2]].data(); u = AT(r0[0], r1[1], r1[2], q);
    q = g[b11 + b1[2]].data(); v = AT(r1[0], r1[1], r1[2], q);
    b = LERP(sx, u, v);

    d = LERP(sy, a, b);

    return 1.5f * LERP(sz, c, d);
}

float fractalsum3(const Fvector& v, float freq, int octaves) {
    float sum = 0.0f;
    Fvector v_;
    float boost = freq;
    
    v_[0] = v[0] * freq;
    v_[1] = v[1] * freq;
    v_[2] = v[2] * freq;

    for (int i = 0; i < octaves; i++) {
        sum += noise3(v_) / freq;
        freq *= 2.059f;
        v_[0] = v[0] * freq;
        v_[1] = v[1] * freq;
        v_[2] = v[2] * freq;
    }
    return sum * boost;
}

float turbulence3(const Fvector& v, float freq, int octaves) {
    float sum = 0.0f;
    Fvector v_;
    float boost = freq;
    
    v_[0] = v[0] * freq;
    v_[1] = v[1] * freq;
    v_[2] = v[2] * freq;

    for (int i = 0; i < octaves; i++) {
        sum += std::abs(noise3(v_)) / freq;
        freq *= 2.059f;
        v_[0] = v[0] * freq;
        v_[1] = v[1] * freq;
        v_[2] = v[2] * freq;
    }
    return sum * boost;
}