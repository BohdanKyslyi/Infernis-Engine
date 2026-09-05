#pragma once
#ifndef _BITWISE_
#define _BITWISE_

#include <cmath>
#include <limits>
#include <xmmintrin.h>

// float values defines
constexpr u32 fdSGN  = 0x080000000;  // mask for sign bit
constexpr u32 fdMABS = 0x07FFFFFFF;  // mask for absolute value (~sgn)
constexpr u32 fdMANT = 0x0007FFFFF;  // mask for mantissa
constexpr u32 fdEXPO = 0x07F800000;  // mask for exponent
constexpr u32 fdONE  = 0x03F800000;  // 1.0f
constexpr u32 fdHALF = 0x03F000000;  // 0.5f
constexpr u32 fdTWO  = 0x040000000;  // 2.0
constexpr u32 fdOOB  = 0x000000000;  // "out of bounds" value
constexpr u32 fdNAN  = 0x07fffffff;  // "Not a number" value
constexpr u32 fdMAX  = 0x07F7FFFFF;  // FLT_MAX
constexpr u32 fdRLE10 = 0x03ede5bdb; // 1/ln10

[[nodiscard]] inline bool negative(const float f) noexcept { return std::signbit(f); }
[[nodiscard]] inline bool positive(const float f) noexcept { return !std::signbit(f); }
inline void set_negative(float& f) noexcept { f = -std::abs(f); }
inline void set_positive(float& f) noexcept { f = std::abs(f); }

// Lowest Bit Mask
[[nodiscard]] constexpr int btwLowestBitMask(int v) noexcept { return (v & -v); }
[[nodiscard]] constexpr u32 btwLowestBitMask(u32 x) noexcept { return x & ~(x - 1); }

// Pow2 checks
[[nodiscard]] constexpr bool btwIsPow2(int v) noexcept { return (btwLowestBitMask(v) == v); }
[[nodiscard]] constexpr bool btwIsPow2(u32 v) noexcept { return (btwLowestBitMask(v) == v); }

[[nodiscard]] inline int btwPow2_Ceil(int v) noexcept {
    int i = btwLowestBitMask(v);
    while (i < v) i <<= 1;
    return i;
}
[[nodiscard]] inline u32 btwPow2_Ceil(u32 v) noexcept {
    u32 i = btwLowestBitMask(v);
    while (i < v) i <<= 1;
    return i;
}

[[nodiscard]] inline u8 btwCount1(u8 v) noexcept {
    u32 value = v;
    value -= (value >> 1) & 0x55u;
    value = (value & 0x33u) + ((value >> 2) & 0x33u);
    return static_cast<u8>((value + (value >> 4)) & 0x0fu);
}

[[nodiscard]] inline u32 btwCount1(u32 v) noexcept {
    v -= (v >> 1) & 0x55555555u;
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0f0f0f0fu;
    return (v * 0x01010101u) >> 24;
}

[[nodiscard]] inline u64 btwCount1(u64 v) noexcept {
    v -= (v >> 1) & 0x5555555555555555ull;
    v = (v & 0x3333333333333333ull) + ((v >> 2) & 0x3333333333333333ull);
    v = (v + (v >> 4)) & 0x0f0f0f0f0f0f0f0full;
    return (v * 0x0101010101010101ull) >> 56;
}

[[nodiscard]] inline int iFloor(float x) noexcept {
    const int truncated = _mm_cvtt_ss2si(_mm_set_ss(x));
    if (truncated == std::numeric_limits<int>::min())
        return truncated;

    return truncated - (static_cast<float>(truncated) > x ? 1 : 0);
}

[[nodiscard]] inline int iCeil(float x) noexcept {
    const int truncated = _mm_cvtt_ss2si(_mm_set_ss(x));
    if (truncated == std::numeric_limits<int>::min())
        return truncated;

    return truncated + (static_cast<float>(truncated) < x ? 1 : 0);
}

[[nodiscard]] inline bool fis_gremlin(const float& f) noexcept {
    u8 value = static_cast<u8>(((*reinterpret_cast<const u32*>(&f) & 0x7f800000) >> 23) - 0x20);
    return value > 0xc0;
}

[[nodiscard]] inline bool fis_denormal(const float& f) noexcept {
    return !(*reinterpret_cast<const u32*>(&f) & 0x7f800000);
}

[[nodiscard]] inline float apx_InvSqrt(const float& n) noexcept {
    u32 tmp = (0xBE800000 - *reinterpret_cast<const u32*>(&n)) >> 1;
    float y = *reinterpret_cast<float*>(&tmp);
    return y * (1.47f - 0.47f * n * y * y);
}

[[nodiscard]] inline float apx_asin(const float x) noexcept {
    constexpr float c1 = 0.892399f;
    constexpr float c3 = 1.693204f;
    constexpr float c5 = -3.853735f;
    constexpr float c7 = 2.838933f;

    const float x2 = x * x;
    return x * (c1 + x2 * (c3 + x2 * (c5 + x2 * c7)));
}

[[nodiscard]] inline float apx_acos(const float x) noexcept { 
    constexpr float PI_DIV_2 = 1.57079632679f;
    return PI_DIV_2 - apx_asin(x); 
}

#endif // _BITWISE_
