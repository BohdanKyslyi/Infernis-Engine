#ifndef LAYERS_XRRENDER_LIGHT_H_INCLUDED
#define LAYERS_XRRENDER_LIGHT_H_INCLUDED

#include "xrcdb/ispatial.h"

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
#include "light_package.h"
#include "light_smapvis.h"
#include "light_GI.h"
#endif //(RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)

class light : public IRender_Light, public ISpatial {
public:
    struct {
        u32 type : 4;
        u32 bStatic : 1;
        u32 bActive : 1;
        u32 bShadow : 1;
        u32 bVolumetric : 1;
        u32 bHudMode : 1;
    } flags{};

    // C++11/17 Default Member Initialization
    Fvector position{0.f, -1000.f, 0.f};
    Fvector direction{0.f, -1.f, 0.f};
    Fvector right{0.f, 0.f, 0.f};
    float range{8.f};
    float cone{deg2rad(60.f)};
    Fcolor color{1.f, 1.f, 1.f, 1.f};

    vis_data hom;
    u32 frame_render{0};

    float m_volumetric_quality{1.f};
    float m_volumetric_intensity{1.f};
    float m_volumetric_distance{1.f};

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    float falloff{0.f};      
    float attenuation0{0.f}; 
    float attenuation1{0.f}; 
    float attenuation2{0.f}; 

    light* omnipart[6]{nullptr}; 
    xr_vector<light_indirect> indirect;
    u32 indirect_photons{0};

    smapvis svis; 

    ref_shader s_spot;
    ref_shader s_point;
    ref_shader s_volumetric;

#if (RENDER == R_R3) || (RENDER == R_R4)
    ref_shader s_spot_msaa[8];
    ref_shader s_point_msaa[8];
    ref_shader s_volumetric_msaa[8];
#endif // (RENDER==R_R3) || (RENDER==R_R4)

    u32 m_xform_frame{0};
    Fmatrix m_xform;

    struct _vis {
        u32 frame2test;  
        u32 query_id;    
        u32 query_order; 
        bool visible;    
        bool pending;    
        u16 smap_ID;
    } vis{};

    union _xform {
        struct _D {
            Fmatrix combine;
            s32 minX, maxX;
            s32 minY, maxY;
            BOOL transluent;
        } D;
        struct _P {
            Fmatrix world;
            Fmatrix view;
            Fmatrix project;
            Fmatrix combine;
        } P;
        struct _S {
            Fmatrix view;
            Fmatrix project;
            Fmatrix combine;
            u32 size;
            u32 posX;
            u32 posY;
            BOOL transluent;
        } S;
    } X;
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)

public:
    light();
    ~light() override;

    void set_type(LT type) override { flags.type = type; }
    void set_active(bool b) override;
    bool get_active() override { return flags.bActive; }
    void set_shadow(bool b) override { flags.bShadow = b; }
    void set_volumetric(bool b) override { flags.bVolumetric = b; }

    void set_volumetric_quality(float fValue) override { m_volumetric_quality = fValue; }
    void set_volumetric_intensity(float fValue) override { m_volumetric_intensity = fValue; }
    void set_volumetric_distance(float fValue) override { m_volumetric_distance = fValue; }

    void set_position(const Fvector& P) override;
    void set_rotation(const Fvector& D, const Fvector& R) override;
    void set_cone(float angle) override;
    void set_range(float R) override;
    void set_virtual_size(float R) override {};
    void set_color(const Fcolor& C) override { color.set(C); }
    void set_color(float r, float g, float b) override { color.set(r, g, b, 1.f); }
    void set_texture(LPCSTR name) override;
    void set_hud_mode(bool b) override { flags.bHudMode = b; }
    bool get_hud_mode() override { return flags.bHudMode; }

    void spatial_move() override;
    Fvector spatial_sector_point() override;

    IRender_Light* dcast_Light() override { return this; }

    vis_data& get_homdata();
#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    void gi_generate();
    void xform_calc();
    void vis_prepare();
    void vis_update();
    void export_(light_Package& dest);
    void set_attenuation_params(float a0, float a1, float a2, float fo);
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)

    [[nodiscard]] float get_LOD() const;
};

#endif // LAYERS_XRRENDER_LIGHT_H_INCLUDED