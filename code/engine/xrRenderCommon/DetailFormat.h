#pragma once

#ifndef _DETAIL_FORMAT_H_
#define _DETAIL_FORMAT_H_

#include <algorithm> // Для std::clamp

#pragma pack(push, 1)

constexpr u32 DETAIL_VERSION = 3;
constexpr float DETAIL_SLOT_SIZE = 2.f;
constexpr float DETAIL_SLOT_SIZE_2 = DETAIL_SLOT_SIZE * 0.5f;

/*
0 - Header(version,obj_count(max255),size_x,size_z,min_x,min_z)
1 - Objects
        0
        1
        2
        ..
        obj_count-1
2 - slots
*/

constexpr u32 DO_NO_WAVING = 0x0001;

struct DetailHeader {
    u32 version;
    u32 object_count;
    int offs_x, offs_z;
    u32 size_x, size_z;
};

struct DetailPalette {
    u16 a0 : 4;
    u16 a1 : 4;
    u16 a2 : 4;
    u16 a3 : 4;
};

struct DetailSlot {
    u32 y_base : 12;  // 11  // 1 unit = 20 cm, low = -200m, high = 4096*20cm - 200 = 619.2m
    u32 y_height : 8; // 20  // 1 unit = 10 cm, low = 0,     high = 256*10 ~= 25.6m
    u32 id0 : 6;      // 26  // 0x3F(63) = empty
    u32 id1 : 6;      // 32  // 0x3F(63) = empty
    u32 id2 : 6;      // 38  // 0x3F(63) = empty
    u32 id3 : 6;      // 42  // 0x3F(63) = empty
    u32 c_dir : 4;    // 48  // 0..1 q
    u32 c_hemi : 4;   // 52  // 0..1 q
    u32 c_r : 4;      // 56  // rgb = 4.4.4
    u32 c_g : 4;      // 60  // rgb = 4.4.4
    u32 c_b : 4;      // 64  // rgb = 4.4.4
    
    DetailPalette palette[4];

public:
    static constexpr u8 ID_Empty = 0x3f;

public:
    inline void w_y(float base, float height) {
        const s32 _base = iFloor((base + 200.f) / 0.2f);
        y_base = static_cast<u32>(std::clamp(_base, 0, 4095));
        
        const float _error = base - r_ybase();
        const s32 _height = iCeil((height + _error) / 0.1f);
        y_height = static_cast<u32>(std::clamp(_height, 0, 255));
    }

    [[nodiscard]] inline float r_ybase() const { 
        return static_cast<float>(y_base) * 0.2f - 200.f; 
    }
    
    [[nodiscard]] inline float r_yheight() const { 
        return static_cast<float>(y_height) * 0.1f; 
    }
    
    [[nodiscard]] static inline u32 w_qclr(float v, u32 range) {
        const s32 _v = iFloor(v * static_cast<float>(range));
        return static_cast<u32>(std::clamp(_v, 0, static_cast<s32>(range)));
    }
    
    [[nodiscard]] static inline float r_qclr(u32 v, u32 range) { 
        return static_cast<float>(v) / static_cast<float>(range); 
    }

    inline void color_editor() {
        c_dir = w_qclr(0.5f, 15);
        c_hemi = w_qclr(0.5f, 15);
        c_r = w_qclr(0.f, 15);
        c_g = w_qclr(0.f, 15);
        c_b = w_qclr(0.f, 15);
    }
    
    [[nodiscard]] inline u8 r_id(u32 idx) const {
        switch (idx) {
        case 0: return static_cast<u8>(id0);
        case 1: return static_cast<u8>(id1);
        case 2: return static_cast<u8>(id2);
        case 3: return static_cast<u8>(id3);
        default: return ID_Empty;
        }
    }
    
    inline void w_id(u32 idx, u8 val) {
        switch (idx) {
        case 0: id0 = val; break;
        case 1: id1 = val; break;
        case 2: id2 = val; break;
        case 3: id3 = val; break;
        default: break;
        }
    }
};

#pragma pack(pop)
#endif // _DETAIL_FORMAT_H_