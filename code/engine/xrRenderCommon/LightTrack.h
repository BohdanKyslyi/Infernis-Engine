// LightTrack.h: interface for the CLightTrack class.
#pragma once

#ifndef AFX_LIGHTTRACK_H__89914D61_AC0B_4C7C_BA8C_D7D810738CE7__INCLUDED_
#define AFX_LIGHTTRACK_H__89914D61_AC0B_4C7C_BA8C_D7D810738CE7__INCLUDED_

#include <algorithm>

constexpr float lt_inc = 4.f;
constexpr float lt_dec = 2.f;
constexpr int lt_hemisamples = 26;

class CROS_impl : public IRender_ObjectSpecific {
public:
    enum CubeFaces {
        CUBE_FACE_POS_X,
        CUBE_FACE_POS_Y,
        CUBE_FACE_POS_Z,
        CUBE_FACE_NEG_X,
        CUBE_FACE_NEG_Y,
        CUBE_FACE_NEG_Z,
        NUM_FACES
    };

    struct Item {
        u32 frame_touched;        
        light* source;            
        collide::ray_cache cache; 
        float test;               
        float energy;             
    };
    
    struct Light {
        light* source;
        float energy;
        Fcolor color;
    };

public:
    // C++11 Default Member Initialization
    u32 MODE{ TRACE_ALL };
    u32 dwFrame{ u32(-1) };
    u32 dwFrameSmooth{ u32(-1) };

    xr_vector<Item> track;   
    xr_vector<Light> lights; 

    bool result[lt_hemisamples]{};
    collide::ray_cache cache[lt_hemisamples]{};
    collide::ray_cache cache_sun{};
    
    s32 result_count{ 0 };
    u32 result_iterator{ 0 };
    u32 result_frame{ u32(-1) };
    s32 result_sun{ 0 };

    u32 shadow_gen_frame{ u32(-1) };
    u32 shadow_recv_frame{ u32(-1) };
    int shadow_recv_slot{ -1 };

private:
    float hemi_cube[NUM_FACES]{};
    float hemi_cube_smooth[NUM_FACES]{};

    float hemi_value{ 0.5f };
    float hemi_smooth{ 0.5f };
    float sun_value{ 0.2f };
    float sun_smooth{ 0.2f };

    Fvector approximate{ 0.f, 0.f, 0.f };

#if RENDER != R_R1
    Fvector last_position{ 0.f, 0.f, 0.f };
    s32 ticks_to_update{ 0 };
    s32 sky_rays_uptodate{ 0 };
#endif 

public:
    CROS_impl();
    ~CROS_impl() override = default;

    void force_mode(u32 mode) override { MODE = mode; }
    
    float get_luminocity() override {
        float res = std::max({ approximate.x, approximate.y, approximate.z });
        return std::clamp(res, 0.f, 1.f);
    }
    
    float get_luminocity_hemi() override { return get_hemi(); }
    float* get_luminocity_hemi_cube() override { return hemi_cube_smooth; }

    void add(light* L);
    void update(IRenderable* O);
    void update_smooth(IRenderable* O = nullptr);

    inline float get_hemi() {
        if (dwFrameSmooth != Device.dwFrame) update_smooth();
        return hemi_smooth;
    }
    
    inline float get_sun() {
        if (dwFrameSmooth != Device.dwFrame) update_smooth();
        return sun_smooth;
    }
    
    inline Fvector3& get_approximate() {
        if (dwFrameSmooth != Device.dwFrame) update_smooth();
        return approximate;
    }

    inline const float* get_hemi_cube() {
        if (dwFrameSmooth != Device.dwFrame) update_smooth();
        return hemi_cube_smooth;
    }

private:
    static inline void accum_hemi(float* hemi_cube, Fvector3& dir, float scale);
    void calc_sun_value(Fvector& position, CObject* _object);
    void calc_sky_hemi_value(Fvector& position, CObject* _object);
    void prepare_lights(Fvector& position, IRenderable* O);

#if RENDER != R_R1
    void smart_update(IRenderable* O);
#endif 
};

#endif // AFX_LIGHTTRACK_H__89914D61_AC0B_4C7C_BA8C_D7D810738CE7__INCLUDED_