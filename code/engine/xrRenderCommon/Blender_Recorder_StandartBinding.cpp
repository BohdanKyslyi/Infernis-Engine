#include "stdafx.h"
#pragma hdrstop

#pragma warning(push)
#pragma warning(disable : 4995)
#include <d3dx9.h>
#pragma warning(pop)

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include "xrEngine/ssfx_console_vars.h"
#include "xrEngine/infernis_pbr_console_vars.h"
#include "xrEngine/shader_bus.h"
#ifndef _EDITOR
#include "xrEngine/IGame_Level.h"
#endif

#include "dxRenderDeviceRender.h"

// matrices
#define BIND_DECLARE(xf)                                                 \
    \
class cl_xform_##xf : public R_constant_setup{                           \
        virtual void setup(R_constant * C){ RCache.xforms.set_c_##xf(C); \
    }                                                                    \
    }                                                                    \
    ;                                                                    \
    static cl_xform_##xf binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);

// SSFX: previous frame matrices ( Motion vectors )
BIND_DECLARE(v_prev);
BIND_DECLARE(p_prev);
BIND_DECLARE(wv_prev);
BIND_DECLARE(vp_prev);
BIND_DECLARE(wvp_prev);

#define DECLARE_TREE_BIND(c)                                            \
    class cl_tree_##c : public R_constant_setup {                       \
        virtual void setup(R_constant* C) { RCache.tree.set_c_##c(C); } \
    };                                                                  \
    static cl_tree_##c tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

class cl_hemi_cube_pos_faces : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.hemi.set_c_pos_faces(C); }
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.hemi.set_c_neg_faces(C); }
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.hemi.set_c_material(C); }
};

static cl_material binder_material;

class cl_texgen : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
        Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f,  0.0f, 1.0f };
#else  //	USE_DX10
        float _w = float(RDEVICE.dwWidth);
        float _h = float(RDEVICE.dwHeight);
        float o_w = (.5f / _w);
        float o_h = (.5f / _h);
        Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,       -0.5f,      0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, 0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f };
#endif //	USE_DX10

        mTexgen.mul(mTexelAdjust, RCache.xforms.m_wvp);

        RCache.set_c(C, mTexgen);
    }
};
static cl_texgen binder_texgen;

class cl_VPtexgen : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
        Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f,  0.0f, 1.0f };
#else  //	USE_DX10
        float _w = float(RDEVICE.dwWidth);
        float _h = float(RDEVICE.dwHeight);
        float o_w = (.5f / _w);
        float o_h = (.5f / _h);
        Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,       -0.5f,      0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, 0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f };
#endif //	USE_DX10

        mTexgen.mul(mTexelAdjust, RCache.xforms.m_vp);

        RCache.set_c(C, mTexgen);
    }
};
static cl_VPtexgen binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            // Plane
            Fvector4 plane;
            Fmatrix& M = Device.mFullTransform;
            plane.x = -(M._14 + M._13);
            plane.y = -(M._24 + M._23);
            plane.z = -(M._34 + M._33);
            plane.w = -(M._44 + M._43);
            float denom = -1.0f / std::sqrt(xr::sqr(plane.x) + xr::sqr(plane.y) + xr::sqr(plane.z));
            plane.mul(denom);

            // Near/Far
            float A = g_pGamePersistent->Environment().CurrentEnv->fog_near;
            float B = 1 / (g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
            result.set(-plane.x * B, -plane.y * B, -plane.z * B,
                       1 - (plane.w - A) * B); // view-plane
        }
        RCache.set_c(C, result);
    }
};
static cl_fog_plane binder_fog_plane;

// fog-params
class cl_fog_params : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            // Near/Far
            float n = g_pGamePersistent->Environment().CurrentEnv->fog_near;
            float f = g_pGamePersistent->Environment().CurrentEnv->fog_far;
            float r = 1 / (f - n);
            result.set(-n * r, r, r, r);
        }
        RCache.set_c(C, result);
    }
};
static cl_fog_params binder_fog_params;

// fog-color
class cl_fog_color : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, 0);
        }
        RCache.set_c(C, result);
    }
};
static cl_fog_color binder_fog_color;
#endif

// times
class cl_times : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float t = RDEVICE.fTimeGlobal;
        RCache.set_c(C, t, t * 10, t / 10, std::sin(t));
    }
};
static cl_times binder_times;

// eye-params
class cl_eye_P : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fvector& V = RDEVICE.vCameraPosition;
        RCache.set_c(C, V.x, V.y, V.z, 1);
    }
};
static cl_eye_P binder_eye_P;

// eye-params
class cl_eye_D : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fvector& V = RDEVICE.vCameraDirection;
        RCache.set_c(C, V.x, V.y, V.z, 0);
    }
};
static cl_eye_D binder_eye_D;

// eye-params
class cl_eye_N : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fvector& V = RDEVICE.vCameraTop;
        RCache.set_c(C, V.x, V.y, V.z, 0);
    }
};
static cl_eye_N binder_eye_N;

#ifndef _EDITOR
// D-Light0
class cl_sun0_color : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);
        }
        RCache.set_c(C, result);
    }
};
static cl_sun0_color binder_sun0_color;
class cl_sun0_dir_w : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);
        }
        RCache.set_c(C, result);
    }
};
static cl_sun0_dir_w binder_sun0_dir_w;
class cl_sun0_dir_e : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            Fvector D;
            CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
            Device.mView.transform_dir(D, desc.sun_dir);
            D.normalize();
            result.set(D.x, D.y, D.z, 0);
        }
        RCache.set_c(C, result);
    }
};
static cl_sun0_dir_e binder_sun0_dir_e;

//
class cl_amb_color : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
        }
        RCache.set_c(C, result);
    }
};
static cl_amb_color binder_amb_color;
class cl_hemi_color : public R_constant_setup {
    u32 marker;
    Fvector4 result;
    virtual void setup(R_constant* C) {
        if (marker != Device.dwFrame) {
            CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
        }
        RCache.set_c(C, result);
    }
};
static cl_hemi_color binder_hemi_color;
#endif

static class cl_screen_res : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, (float)RDEVICE.dwWidth, (float)RDEVICE.dwHeight,
                     1.0f / (float)RDEVICE.dwWidth, 1.0f / (float)RDEVICE.dwHeight);
    }
} binder_screen_res;

// Screen Space Shaders - constant binders.
//
// m_inv_V and sky_color have no counterpart in this engine yet; the SSFX headers need them to turn
// view-space normals into world space and to sample the sky cubemaps for reflections.
class cl_inv_v : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fmatrix result;
        result.invert(Device.mView);
        RCache.set_c(C, result);
    }
};
static cl_inv_v binder_inv_v;

#ifndef _EDITOR
class cl_sky_color : public R_constant_setup {
    virtual void setup(R_constant* C) {
        CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
        RCache.set_c(C, desc.sky_color.x, desc.sky_color.y, desc.sky_color.z, desc.sky_rotation);
    }
};
static cl_sky_color binder_sky_color;
#endif

class ssfx_wpn_dof_1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wpn_dof_1); }
};
static ssfx_wpn_dof_1 binder_ssfx_wpn_dof_1;

class ssfx_wpn_dof_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wpn_dof_2, 0.f, 0.f, 0.f); }
};
static ssfx_wpn_dof_2 binder_ssfx_wpn_dof_2;

// Infernis PBR: the numbers of infernis_pbr_lighting.h that the settings page drives.
class ie_pbr_cal_diffuse : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_pbr_cal_diffuse); }
};
static ie_pbr_cal_diffuse binder_ie_pbr_cal_diffuse;

class ie_pbr_cal_specular : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_pbr_cal_specular); }
};
static ie_pbr_cal_specular binder_ie_pbr_cal_specular;

class ie_pbr_highlight : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_pbr_highlight); }
};
static ie_pbr_highlight binder_ie_pbr_highlight;

class ie_pbr_surface : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_pbr_surface); }
};
static ie_pbr_surface binder_ie_pbr_surface;

class ssfx_blood_decals : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_blood_decals); }
};
static ssfx_blood_decals binder_ssfx_blood_decals;

class ssfx_hud_drops_1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_hud_drops_1); }
};
static ssfx_hud_drops_1 binder_ssfx_hud_drops_1;

class ssfx_hud_drops_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_hud_drops_2); }
};
static ssfx_hud_drops_2 binder_ssfx_hud_drops_2;

class ssfx_lightsetup_1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_lightsetup_1); }
};
static ssfx_lightsetup_1 binder_ssfx_lightsetup_1;
#if RENDER == R_R4

// Screen Space Shaders: the pseudo-PBR module's settings ( pbr_settings.h ). DX11 only - the
// module's shaders exist only there.
class ssfx_es_albedo : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_albedo); }
};
static ssfx_es_albedo binder_ssfx_es_albedo;

class ssfx_es_rough : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_rough); }
};
static ssfx_es_rough binder_ssfx_es_rough;

class ssfx_es_spec : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_spec); }
};
static ssfx_es_spec binder_ssfx_es_spec;

class ssfx_es_metal : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_metal); }
};
static ssfx_es_metal binder_ssfx_es_metal;

class ssfx_es_light : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_light); }
};
static ssfx_es_light binder_ssfx_es_light;

class ssfx_es_flora : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_flora); }
};
static ssfx_es_flora binder_ssfx_es_flora;

class ssfx_es_water : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_es_water); }
};
static ssfx_es_water binder_ssfx_es_water;
#endif

class ssfx_is_underground : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, (float)ps_ssfx_is_underground, 0.f, 0.f, 0.f);
    }
};
static ssfx_is_underground binder_ssfx_is_underground;

class ssfx_wetsurfaces_1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wetsurfaces_1); }
};
static ssfx_wetsurfaces_1 binder_ssfx_wetsurfaces_1;

class ssfx_wetsurfaces_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wetsurfaces_2); }
};
static ssfx_wetsurfaces_2 binder_ssfx_wetsurfaces_2;

class ssfx_gloss : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, ps_ssfx_gloss_minmax.x, ps_ssfx_gloss_minmax.y, ps_ssfx_gloss_factor,
                     ps_ssfx_gloss_minmax.w);
    }
};
static ssfx_gloss binder_ssfx_gloss;

class ssfx_florafixes_1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_florafixes_1); }
};
static ssfx_florafixes_1 binder_ssfx_florafixes_1;

class ssfx_florafixes_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_florafixes_2); }
};
static ssfx_florafixes_2 binder_ssfx_florafixes_2;

class ssfx_volumetric : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_volumetric); }
};
static ssfx_volumetric binder_ssfx_volumetric;

class ssfx_ssr_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_ssr_2); }
};
static ssfx_ssr_2 binder_ssfx_ssr_2;

class ssfx_terrain_offset : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_terrain_offset); }
};
static ssfx_terrain_offset binder_ssfx_terrain_offset;

class ssfx_shadow_bias : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, ps_ssfx_shadow_bias.x, ps_ssfx_shadow_bias.y, 0.f, 0.f);
    }
};
static ssfx_shadow_bias binder_ssfx_shadow_bias;

// These read the weather descriptor and the game persistent, so they follow the same rule as the
// fog and sun binders above and are left out of editor builds.
#ifndef _EDITOR
class ssfx_wind_params : public R_constant_setup {
    virtual void setup(R_constant* C) {
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        RCache.set_c(C, E.wind_direction, E.wind_velocity, 0.f, 0.f);
    }
};
static ssfx_wind_params binder_wind_params;

class ssfx_wind_anim : public R_constant_setup {
    virtual void setup(R_constant* C) {
        const Fvector4& WindAni = g_pGamePersistent->Environment().wind_anim;
        RCache.set_c(C, WindAni.x, WindAni.y, WindAni.z, WindAni.w);
    }
};
static ssfx_wind_anim binder_ssfx_wind_anim;

class cl_rain_params : public R_constant_setup {
    virtual void setup(R_constant* C) {
        CEnvironment& E = g_pGamePersistent->Environment();
        RCache.set_c(C, E.CurrentEnv->rain_density, E.wetness_factor, 0.f, 0.f);
    }
};
static cl_rain_params binder_rain_params;

class cl_pda_params : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float pda_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
        float pda_psy_factor = g_pGamePersistent->pda_shader_data.pda_psy_influence;
        float pda_display_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;
        RCache.set_c(C, pda_factor, pda_psy_factor, pda_display_brightness, 0.f);
    }
};
static cl_pda_params binder_pda_params;
#endif

class ssfx_wind_anim_prev : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, Device.wind_anim_prev); }
};
static ssfx_wind_anim_prev binder_ssfx_wind_anim_prev;

class ssfx_wind_grass : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wind_grass); }
};
static ssfx_wind_grass binder_ssfx_wind_grass;

class ssfx_wind_trees : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_wind_trees); }
};
static ssfx_wind_trees binder_ssfx_wind_trees;

class ssfx_lut : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_lut); }
};
static ssfx_lut binder_ssfx_lut;

class ssfx_ao_setup : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_ao); }
};
static ssfx_ao_setup binder_ssfx_ao_setup;

class ssfx_ao_setup2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_ao_setup1); }
};
static ssfx_ao_setup2 binder_ssfx_ao_setup2;

class ssfx_il_setup : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_il); }
};
static ssfx_il_setup binder_ssfx_il_setup;

class ssfx_il_setup2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_il_setup1); }
};
static ssfx_il_setup2 binder_ssfx_il_setup2;

// Threshold, exposure and sky intensity come from the weather when ssfx_bloom_use_presets is on.
class ssfx_bloom_1 : public R_constant_setup {
    virtual void setup(R_constant* C) {
        Fvector4 BloomSetup = { 0.f, 0.f, 0.f, 0.f };
        if (ps_ssfx_bloom_use_presets) {
            BloomSetup.x = g_pGamePersistent->Environment().CurrentEnv->bloom_threshold;
            BloomSetup.y = g_pGamePersistent->Environment().CurrentEnv->bloom_exposure;
            BloomSetup.w = g_pGamePersistent->Environment().CurrentEnv->bloom_sky_intensity;
        } else {
            BloomSetup.x = ps_ssfx_bloom_1.x;
            BloomSetup.y = ps_ssfx_bloom_1.y;
            BloomSetup.w = ps_ssfx_bloom_1.w;
        }
        RCache.set_c(C, BloomSetup);
    }
};
static ssfx_bloom_1 binder_ssfx_bloom_1;

class ssfx_bloom_2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_bloom_2); }
};
static ssfx_bloom_2 binder_ssfx_bloom_2;

// 1 while the 3D-scope lens view is rendered ( Monolith: second viewport frame ).
class ssfx_issvp : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, Device.scopeLensPass ? 1.f : 0.f, 0.f, 0.f, 0.f);
    }
};
static ssfx_issvp binder_ssfx_issvp;

#ifndef _EDITOR
static class cl_scope_lens_electronics : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float params[] = {0.f, 1.f / 3.f, 1.f}; // reticle blend, NV gain, noise scale
        if (Device.scopeLensActive && !Device.scopeLensPass && g_pGameLevel)
            g_pGameLevel->ScopeLensElectronics(params);
        RCache.set_c(C, params[0], params[1], params[2], Device.fTimeGlobal);
    }
} binder_scope_lens_electronics;

static class cl_scope_lens_gauss : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float params[] = {0.f, 0.f}; // aberration, electronic interference
        if (Device.scopeLensActive && !Device.scopeLensPass && g_pGameLevel)
            g_pGameLevel->ScopeLensGauss(params);
        RCache.set_c(C, params[0], params[1], 0.f, 0.f);
    }
} binder_scope_lens_gauss;

static class cl_weapon_computer_display : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float params[] = {1.f, 0.f, 1.f}; // charge, interference, condition
        if (!Device.scopeLensPass && g_pGameLevel)
            g_pGameLevel->WeaponComputerDisplay(params);
        RCache.set_c(C, params[0], params[1], params[2], 0.f);
    }
} binder_weapon_computer_display;
#endif

class ssfx_pom : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_pom); }
};
static ssfx_pom binder_ssfx_pom;

class ssfx_terrain_pom : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_terrain_pom); }
};
static ssfx_terrain_pom binder_ssfx_terrain_pom;

// TAA jitter ( xy ), zw = ssfx_taa.x, ssfx_taa.w. The jitter stays zero unless the TAA module is installed.
class ssfx_jitter : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float JitterX = 0;
        float JitterY = 0;
#if defined(USE_DX11)
#if RENDER == R_R4
        // NOT IN MONOLITH ( user 2026-10-23 ): no TAA jitter, the lamps flickered with it.
        // ssfx_jitter.xy stays zero; the donor's jitter is the #else branch.
#else
        if (ps_ssfx_taa.x > 0 && RImplementation.o.ssfx_taa) {
            static Fvector2 TAA_Offset[4] = { { 0.0f, -1.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
            JitterX = TAA_Offset[Device.dwFrame % 4].x / Device.dwWidth;
            JitterY = TAA_Offset[Device.dwFrame % 4].y / Device.dwHeight;
        }
#endif
#endif
        RCache.set_c(C, JitterX * ps_ssfx_taa.y, JitterY * ps_ssfx_taa.y, ps_ssfx_taa.x, ps_ssfx_taa.w);
    }
};
static ssfx_jitter binder_ssfx_jitter;

class ssfx_floravariation : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_floravariation); }
};
static ssfx_floravariation binder_ssfx_floravariation;

class ssfx_fog : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_fog); }
};
static ssfx_fog binder_ssfx_fog;

class ssfx_timedelta : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, Device.fTimeDelta, (float)Device.dwFrame, 0.f, 0.f);
    }
};
static ssfx_timedelta binder_ssfx_timedelta;

class ssfx_motionblur : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_motionblur); }
};
static ssfx_motionblur binder_ssfx_motionblur;

class ssfx_hud_hemi : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_hud_hemi, 0.f, 0.f, 0.f); }
};
static ssfx_hud_hemi binder_ssfx_hud_hemi;

class ssfx_water : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_water); }
};
static ssfx_water binder_ssfx_water;

class ssfx_water_setup1 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_water_setup1); }
};
static ssfx_water_setup1 binder_ssfx_water_setup1;

class ssfx_water_setup2 : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.set_c(C, ps_ssfx_water_setup2); }
};
static ssfx_water_setup2 binder_ssfx_water_setup2;

// Image corrections ( r__exposure, r__gamma, r__saturation, r__color_grading )
class pp_image_corrections : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, ps_r2_img_exposure, ps_r2_img_gamma, ps_r2_img_saturation, 1.f);
    }
};
static pp_image_corrections binder_pp_image_corrections;

class pp_color_grading : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, ps_r2_img_cg.x, ps_r2_img_cg.y, ps_r2_img_cg.z, 1.f);
    }
};
static pp_color_grading binder_pp_color_grading;

// Shader params ( shader_param_1..8 console commands )
class cl_shader_param : public R_constant_setup {
    Fvector4* value;

public:
    cl_shader_param(Fvector4* v) : value(v) {}
    virtual void setup(R_constant* C) { RCache.set_c(C, *value); }
};
static cl_shader_param binder_shader_param_1(&ps_dev_param_1);
static cl_shader_param binder_shader_param_2(&ps_dev_param_2);
static cl_shader_param binder_shader_param_3(&ps_dev_param_3);
static cl_shader_param binder_shader_param_4(&ps_dev_param_4);
static cl_shader_param binder_shader_param_5(&ps_dev_param_5);
static cl_shader_param binder_shader_param_6(&ps_dev_param_6);
static cl_shader_param binder_shader_param_7(&ps_dev_param_7);
static cl_shader_param binder_shader_param_8(&ps_dev_param_8);

// Shader Bus: HLSL constants named bus_* read the lane value latched for this frame.
class bus_binder : public R_constant_setup {
    ShaderBus::lane* lane;

public:
    bus_binder(ShaderBus::lane* l) : lane(l) {}
    virtual void setup(R_constant* C) {
        lane->bound_frame = Device.dwFrame;
        RCache.set_c(C, lane->bound.x, lane->bound.y, lane->bound.z, lane->bound.w);
    }
};

// one binder per lane so the pass table dedup, which compares handler pointers, still matches
static xr_vector<bus_binder*> bus_binders;

// Standart constant-binding
void CBlender_Compile::SetMapping() {
    // matrices
    r_Constant("m_W", &binder_w);
    r_Constant("m_invW", &binder_invw);
    r_Constant("m_V", &binder_v);
    r_Constant("m_P", &binder_p);
    r_Constant("m_WV", &binder_wv);
    r_Constant("m_VP", &binder_vp);
    r_Constant("m_WVP", &binder_wvp);

    r_Constant("m_v_prev", &binder_v_prev);
    r_Constant("m_p_prev", &binder_p_prev);
    r_Constant("m_wv_prev", &binder_wv_prev);
    r_Constant("m_vp_prev", &binder_vp_prev);
    r_Constant("m_wvp_prev", &binder_wvp_prev);

    r_Constant("m_xform_v", &tree_binder_m_xform_v);
    r_Constant("m_xform", &tree_binder_m_xform);
    r_Constant("consts", &tree_binder_consts);
    r_Constant("wave", &tree_binder_wave);
    r_Constant("wind", &tree_binder_wind);
    r_Constant("c_scale", &tree_binder_c_scale);
    r_Constant("c_bias", &tree_binder_c_bias);
    r_Constant("c_sun", &tree_binder_c_sun);

    // hemi cube
    r_Constant("L_material", &binder_material);
    r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
    r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

    //	Igor	temp solution for the texgen functionality in the shader
    r_Constant("m_texgen", &binder_texgen);
    r_Constant("mVPTexgen", &binder_VPtexgen);

#ifndef _EDITOR
    // fog-params
    r_Constant("fog_plane", &binder_fog_plane);
    r_Constant("fog_params", &binder_fog_params);
    r_Constant("fog_color", &binder_fog_color);
#endif
    // time
    r_Constant("timers", &binder_times);

    // eye-params
    r_Constant("eye_position", &binder_eye_P);
    r_Constant("eye_direction", &binder_eye_D);
    r_Constant("eye_normal", &binder_eye_N);

#ifndef _EDITOR
    // global-lighting (env params)
    r_Constant("L_sun_color", &binder_sun0_color);
    r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
    r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
    //	r_Constant				("L_lmap_color",	&binder_lm_color);
    r_Constant("L_hemi_color", &binder_hemi_color);
    r_Constant("L_ambient", &binder_amb_color);
#endif
    r_Constant("screen_res", &binder_screen_res);
#ifndef _EDITOR
    r_Constant("scope_lens_electronics", &binder_scope_lens_electronics);
    r_Constant("scope_lens_gauss", &binder_scope_lens_gauss);
    r_Constant("weapon_computer_display", &binder_weapon_computer_display);
#endif

    // Screen Space Shaders
    r_Constant("m_inv_V", &binder_inv_v);
#ifndef _EDITOR
    r_Constant("sky_color", &binder_sky_color);
#endif
    r_Constant("ssfx_wpn_dof_1", &binder_ssfx_wpn_dof_1);
    r_Constant("ssfx_wpn_dof_2", &binder_ssfx_wpn_dof_2);
    r_Constant("ie_pbr_cal_diffuse", &binder_ie_pbr_cal_diffuse);
    r_Constant("ie_pbr_cal_specular", &binder_ie_pbr_cal_specular);
    r_Constant("ie_pbr_highlight", &binder_ie_pbr_highlight);
    r_Constant("ie_pbr_surface", &binder_ie_pbr_surface);
    r_Constant("ssfx_blood_decals", &binder_ssfx_blood_decals);
    r_Constant("ssfx_hud_drops_1", &binder_ssfx_hud_drops_1);
    r_Constant("ssfx_hud_drops_2", &binder_ssfx_hud_drops_2);
    r_Constant("ssfx_lightsetup_1", &binder_ssfx_lightsetup_1);
#if RENDER == R_R4
    r_Constant("ssfx_es_albedo", &binder_ssfx_es_albedo);
    r_Constant("ssfx_es_rough", &binder_ssfx_es_rough);
    r_Constant("ssfx_es_spec", &binder_ssfx_es_spec);
    r_Constant("ssfx_es_metal", &binder_ssfx_es_metal);
    r_Constant("ssfx_es_light", &binder_ssfx_es_light);
    r_Constant("ssfx_es_flora", &binder_ssfx_es_flora);
    r_Constant("ssfx_es_water", &binder_ssfx_es_water);
#endif
    r_Constant("ssfx_is_underground", &binder_ssfx_is_underground);
    r_Constant("ssfx_wetsurfaces_1", &binder_ssfx_wetsurfaces_1);
    r_Constant("ssfx_wetsurfaces_2", &binder_ssfx_wetsurfaces_2);
    r_Constant("ssfx_gloss", &binder_ssfx_gloss);
    r_Constant("ssfx_florafixes_1", &binder_ssfx_florafixes_1);
    r_Constant("ssfx_florafixes_2", &binder_ssfx_florafixes_2);
    r_Constant("ssfx_volumetric", &binder_ssfx_volumetric);
    r_Constant("ssfx_ssr_2", &binder_ssfx_ssr_2);
    r_Constant("ssfx_terrain_offset", &binder_ssfx_terrain_offset);
    r_Constant("ssfx_shadow_bias", &binder_ssfx_shadow_bias);
#ifndef _EDITOR
    r_Constant("wind_params", &binder_wind_params);
    r_Constant("ssfx_wind_anim", &binder_ssfx_wind_anim);
    r_Constant("rain_params", &binder_rain_params);
    r_Constant("pda_params", &binder_pda_params);
#endif
    r_Constant("ssfx_wind_anim_prev", &binder_ssfx_wind_anim_prev);
    r_Constant("ssfx_wsetup_grass", &binder_ssfx_wind_grass);
    r_Constant("ssfx_wsetup_trees", &binder_ssfx_wind_trees);
    r_Constant("ssfx_lut", &binder_ssfx_lut);
    r_Constant("ssfx_ao_setup", &binder_ssfx_ao_setup);
    r_Constant("ssfx_ao_setup2", &binder_ssfx_ao_setup2);
    r_Constant("ssfx_il_setup", &binder_ssfx_il_setup);
    r_Constant("ssfx_il_setup2", &binder_ssfx_il_setup2);
    r_Constant("ssfx_bloom_1", &binder_ssfx_bloom_1);
    r_Constant("ssfx_bloom_2", &binder_ssfx_bloom_2);
    r_Constant("ssfx_issvp", &binder_ssfx_issvp);
    r_Constant("ssfx_pom", &binder_ssfx_pom);
    r_Constant("ssfx_terrain_pom", &binder_ssfx_terrain_pom);
    r_Constant("ssfx_jitter", &binder_ssfx_jitter);
    r_Constant("ssfx_floravariation", &binder_ssfx_floravariation);
    r_Constant("ssfx_fog", &binder_ssfx_fog);
    r_Constant("ssfx_timedelta", &binder_ssfx_timedelta);
    r_Constant("ssfx_motionblur", &binder_ssfx_motionblur);
    r_Constant("ssfx_hud_hemi", &binder_ssfx_hud_hemi);
    r_Constant("ssfx_water", &binder_ssfx_water);
    r_Constant("ssfx_water_setup1", &binder_ssfx_water_setup1);
    r_Constant("ssfx_water_setup2", &binder_ssfx_water_setup2);

    // Image corrections
    r_Constant("pp_img_corrections", &binder_pp_image_corrections);
    r_Constant("pp_img_cg", &binder_pp_color_grading);

    // Shader params
    r_Constant("shader_param_1", &binder_shader_param_1);
    r_Constant("shader_param_2", &binder_shader_param_2);
    r_Constant("shader_param_3", &binder_shader_param_3);
    r_Constant("shader_param_4", &binder_shader_param_4);
    r_Constant("shader_param_5", &binder_shader_param_5);
    r_Constant("shader_param_6", &binder_shader_param_6);
    r_Constant("shader_param_7", &binder_shader_param_7);
    r_Constant("shader_param_8", &binder_shader_param_8);

    // detail
    // if (bDetail	&& detail_scaler)
    //	Igor: bDetail can be overridden by no_detail_texture option.
    //	But shader can be deatiled implicitly, so try to set this parameter
    //	anyway.
    if (detail_scaler)
        r_Constant("dt_params", detail_scaler);

    // other common
    for (u32 it = 0; it < DEV->v_constant_setup.size(); it++) {
        std::pair<shared_str, R_constant_setup*> cs = DEV->v_constant_setup[it];
        r_Constant(*cs.first, cs.second);
    }

    // Shader Bus lanes
    for (u32 it = 0; it < ctable.table.size(); it++) {
        R_constant* C = &*ctable.table[it];
        if (C->type != RC_float)
            continue;

        LPCSTR cname = C->name.c_str();
        if (!cname || 0 != strncmp(cname, "bus_", 4))
            continue;

        ShaderBus::lane* lane = ShaderBus::declare(cname);
        if (!lane)
            continue;

        if (bus_binders.size() <= lane->index)
            bus_binders.resize(lane->index + 1, nullptr);
        if (!bus_binders[lane->index])
            bus_binders[lane->index] = xr_new<bus_binder>(lane);

        C->handler = bus_binders[lane->index];
    }
}
