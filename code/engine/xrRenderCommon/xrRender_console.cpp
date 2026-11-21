#include "stdafx.h"
#pragma hdrstop

#include "xrRender_console.h"
#include "dxRenderDeviceRender.h"

u32 ps_Preset = 2;
xr_token qpreset_token[] = { { "Minimum", 0 }, { "Low", 1 },     { "Default", 2 },
                             { "High", 3 },    { "Extreme", 4 }, { 0, 0 } };

// NOT STOCK: starts switched off ( stock value 2, hdao ). The standard settings of this build
// keep the stock ambient occlusion off, because the Screen Space Shaders set brings its own. A
// profile whose user.ltx already holds r2_ssao_mode is not affected.
u32 ps_r_ssao_mode = 0;
xr_token qssao_mode_token[] = {
    { "disabled", 0 }, { "default", 1 }, { "hdao", 2 }, { "hbao", 3 }, { 0, 0 }
};

u32 ps_r_sun_shafts = 2;
xr_token qsun_shafts_token[] = {
    { "st_opt_off", 0 }, { "st_opt_low", 1 }, { "st_opt_medium", 2 }, { "st_opt_high", 3 }, { 0, 0 }
};

// Monolith xrRender_console.cpp:36-43, table and default both. screen_space is the donor's own
// default and the OGSE phase that draws it is installed now
// ( r4_rendertarget_phase_sunshafts.cpp ), so there is nothing left to hold it back.
//   r2_sunshafts_mode off           - no sun shafts at all
//   r2_sunshafts_mode volumetric    - the shafts this engine drew before, accum_direct_volumetric
//   r2_sunshafts_mode screen_space  - the OGSE screen-space ones ( default, as in Monolith )
//   r2_sunshafts_mode combined      - both
u32 ps_sunshafts_mode = R2SS_SCREEN_SPACE;
xr_token sunshafts_mode_token[] = {
    { "off", 0 }, { "volumetric", 1 }, { "screen_space", 2 }, { "combined", 3 }, { 0, 0 }
};

float ps_r2_ss_sunshafts_length = 1.f;
float ps_r2_ss_sunshafts_radius = 1.f;

// NOT STOCK: starts switched off ( stock value 3, high ), for the same reason as ps_r_ssao_mode.
u32 ps_r_ssao = 0;
xr_token qssao_token[] = { { "st_opt_off", 0 },
                           { "st_opt_low", 1 },
                           { "st_opt_medium", 2 },
                           { "st_opt_high", 3 },
#if defined(USE_DX10) || defined(USE_DX11)
                           { "st_opt_ultra", 4 },
#endif
                           { 0, 0 } };

u32 ps_r_sun_quality = 1; //	=	0;
xr_token qsun_quality_token[] = { { "st_opt_low", 0 },
                                  { "st_opt_medium", 1 },
                                  { "st_opt_high", 2 },
#if defined(USE_DX10) || defined(USE_DX11)
                                  { "st_opt_ultra", 3 },
                                  { "st_opt_extreme", 4 },
#endif //	USE_DX10
                                  { 0, 0 } };

u32 ps_r3_msaa = 0; //	=	0;
xr_token qmsaa_token[] = { { "st_opt_off", 0 },
                           { "2x", 1 },
                           { "4x", 2 },
                           //	{ "8x",							3
                           //},
                           { 0, 0 } };

u32 ps_r3_msaa_atest = 0; //	=	0;
xr_token qmsaa__atest_token[] = { { "st_opt_off", 0 },
                                  { "st_opt_atest_msaa_dx10_0", 1 },
                                  { "st_opt_atest_msaa_dx10_1", 2 },
                                  { 0, 0 } };

u32 ps_r3_minmax_sm = 3; //	=	0;
xr_token qminmax_sm_token[] = {
    { "off", 0 }, { "on", 1 }, { "auto", 2 }, { "autodetect", 3 }, { 0, 0 }
};

//	�Off�
//	�DX10.0 style [Standard]�
//	�DX10.1 style [Higher quality]�

// Common
extern int psSkeletonUpdate;
extern float r__dtex_range;

// int		ps_r__Supersample			= 1		;
int ps_r__LightSleepFrames = 10;

float ps_r__Detail_l_ambient = 0.9f;
float ps_r__Detail_l_aniso = 0.25f;
float ps_r__Detail_density = 0.3f;
// Screen Space Shaders. Grass height, Monolith xrRender_console.cpp:500 ( ps_current_detail_height ).
// Call of Pripyat has no such variable at all. The donor keeps two of them and only
// ps_current_detail_height is ever read by the decompressor, so that is the one ported here.
float ps_r__Detail_height = 1.0f;
// Screen Space Shaders. Grass draw distance in metres, read in DetailManager.cpp::UpdateVisibleM.
// The grass cache is sized at compile time ( dm_size in DetailManager.h, 152 m ), so this only
// shortens the radius the cache already holds and is clamped to dm_fade there. Starts at the full
// radius, because stock Call of Pripyat's 48 m is what made grass look like it ended in front of
// the actor.
float ps_r__Detail_radius = 152.0f;
float ps_r__Detail_rainbow_hemi = 0.75f;

// Screen Space Shaders. The second word of render flags, declared in xrEngine/defines.h and
// defined here exactly as the donor does it - Monolith xrRender_console.cpp:481. Every bit starts
// clear, so nothing changes until a command or the settings menu sets one.
Flags32 psDeviceFlags2 = { 0 };

float ps_r__Tree_w_rot = 10.0f;
float ps_r__Tree_w_speed = 1.00f;
float ps_r__Tree_w_amp = 0.005f;
Fvector ps_r__Tree_Wave = { .1f, .01f, .11f };
float ps_r__Tree_SBC = 1.5f; // scale bias correct

float ps_r__WallmarkTTL = 50.f;
float ps_r__WallmarkSHIFT = 0.0001f;
float ps_r__WallmarkSHIFT_V = 0.0001f;

float ps_r__GLOD_ssa_start = 256.f;
float ps_r__GLOD_ssa_end = 64.f;
float ps_r__LOD = 0.75f;
//. float		ps_r__LOD_Power				=  1.5f	;
float ps_r__ssaDISCARD = 3.5f;  // RO
float ps_r__ssaDONTSORT = 32.f; // RO
float ps_r__ssaHZBvsTEX = 96.f; // RO

int ps_r__tf_Anisotropic = 8;

// R1
float ps_r1_ssaLOD_A = 64.f;
float ps_r1_ssaLOD_B = 48.f;
float ps_r1_tf_Mipbias = 0.0f;
Flags32 ps_r1_flags = { R1FLAG_DLIGHTS }; // r1-only
float ps_r1_lmodel_lerp = 0.1f;
float ps_r1_dlights_clip = 40.f;
float ps_r1_pps_u = 0.f;
float ps_r1_pps_v = 0.f;

// R1-specific
int ps_r1_GlowsPerFrame = 16;     // r1-only
float ps_r1_fog_luminance = 1.1f; // r1-only
int ps_r1_SoftwareSkinning = 0;   // r1-only

// R2
float ps_r2_ssaLOD_A = 64.f;
float ps_r2_ssaLOD_B = 48.f;
float ps_r2_tf_Mipbias = 0.0f;

// R2-specific
Flags32 ps_r2_ls_flags = { R2FLAG_SUN
                           //| R2FLAG_SUN_IGNORE_PORTALS
                           | R2FLAG_EXP_DONT_TEST_UNSHADOWED | R2FLAG_USE_NVSTENCIL |
                           R2FLAG_EXP_SPLIT_SCENE | R2FLAG_EXP_MT_CALC | R3FLAG_DYN_WET_SURF |
                           R3FLAG_VOLUMETRIC_SMOKE
                           //| R3FLAG_MSAA
                           //| R3FLAG_MSAA_OPT
                           | R3FLAG_GBUFFER_OPT | R2FLAG_DETAIL_BUMP | R2FLAG_DOF |
                           R2FLAG_SOFT_PARTICLES | R2FLAG_SOFT_WATER | R2FLAG_STEEP_PARALLAX |
                           R2FLAG_SUN_FOCUS | R2FLAG_SUN_TSM | R2FLAG_TONEMAP |
                           R2FLAG_VOLUMETRIC_LIGHTS }; // r2-only

// NOT STOCK: tessellation starts switched off ( stock has R2FLAGEXT_ENABLE_TESSELLATION in this
// list ), the same standard settings as the ambient occlusion above. A profile whose user.ltx
// already holds r4_enable_tessellation is not affected.
Flags32 ps_r2_ls_flags_ext = {
    /*R2FLAGEXT_SSAO_OPT_DATA |*/ R2FLAGEXT_SSAO_HALF_DATA /*| R2FLAGEXT_ENABLE_TESSELLATION*/
};

float ps_r2_df_parallax_h = 0.02f;
float ps_r2_df_parallax_range = 75.f;
float ps_r2_tonemap_middlegray = 1.f;        // r2-only
float ps_r2_tonemap_adaptation = 1.f;        // r2-only
float ps_r2_tonemap_low_lum = 0.0001f;       // r2-only
float ps_r2_tonemap_amount = 0.7f;           // r2-only
float ps_r2_ls_bloom_kernel_g = 3.f;         // r2-only
float ps_r2_ls_bloom_kernel_b = .7f;         // r2-only
float ps_r2_ls_bloom_speed = 100.f;          // r2-only
float ps_r2_ls_bloom_kernel_scale = .7f;     // r2-only	// gauss
float ps_r2_ls_dsm_kernel = .7f;             // r2-only
float ps_r2_ls_psm_kernel = .7f;             // r2-only
float ps_r2_ls_ssm_kernel = .7f;             // r2-only
float ps_r2_ls_bloom_threshold = .00001f;    // r2-only
Fvector ps_r2_aa_barier = { .8f, .1f, 0 };   // r2-only
Fvector ps_r2_aa_weight = { .25f, .25f, 0 }; // r2-only
float ps_r2_aa_kernel = .5f;                 // r2-only
float ps_r2_mblur = .0f;                     // .5f
int ps_r2_GI_depth = 1;                      // 1..5
int ps_r2_GI_photons = 16;                   // 8..64
float ps_r2_GI_clip = EPS_L;                 // EPS
float ps_r2_GI_refl = .9f;                   // .9f
float ps_r2_ls_depth_scale = 1.00001f;       // 1.00001f
float ps_r2_ls_depth_bias = -0.0003f;        // -0.0001f
float ps_r2_ls_squality = 1.0f;              // 1.00f
float ps_r2_sun_tsm_projection = 0.3f;       // 0.18f
float ps_r2_sun_tsm_bias = -0.01f;           //
float ps_r2_sun_near = 20.f;                 // 12.0f

extern float OLES_SUN_LIMIT_27_01_07; //	actually sun_far

float ps_r2_sun_near_border = 0.75f;        // 1.0f
float ps_r2_sun_depth_far_scale = 1.00000f; // 1.00001f
float ps_r2_sun_depth_far_bias = -0.00002f; // -0.0000f
float ps_r2_sun_depth_near_scale = 1.0000f; // 1.00001f
float ps_r2_sun_depth_near_bias = 0.00001f; // -0.00005f
float ps_r2_sun_lumscale = 1.0f;            // 1.0f
float ps_r2_sun_lumscale_hemi = 1.0f;       // 1.0f
float ps_r2_sun_lumscale_amb = 1.0f;
float ps_r2_gmaterial = 2.2f; //
float ps_r2_zfill = 0.25f;    // .1f

float ps_r2_dhemi_sky_scale = 0.08f; // 1.5f
float ps_r2_dhemi_light_scale = 0.2f;
float ps_r2_dhemi_light_flow = 0.1f;
int ps_r2_dhemi_count = 5; // 5
int ps_r2_wait_sleep = 0;

float ps_r2_lt_smooth = 1.f;    // 1.f
float ps_r2_slight_fade = 0.5f; // 1.f

//	x - min (0), y - focus (1.4), z - max (100)
Fvector3 ps_r2_dof = Fvector3().set(-1.25f, 1.4f, 600.f);
float ps_r2_dof_sky = 30;           //	distance to sky
float ps_r2_dof_kernel_size = 5.0f; //	7.0f

float ps_r3_dyn_wet_surf_near = 10.f; // 10.0f
float ps_r3_dyn_wet_surf_far = 30.f;  // 30.0f
int ps_r3_dyn_wet_surf_sm_res = 256;  // 256

float ps_r2_rain_drops_intensity = 0.00025f;
float ps_r2_rain_drops_speed = 1.25f;

Flags32 ps_r2_rain_drops_flags = { R2FLAG_RAIN_DROPS | R2FLAG_RAIN_DROPS_CONTROL };

//- Mad Max
float ps_r2_gloss_factor = 4.0f;
//- Mad Max

// Minimum-gloss floor for the legacy gloss method ( ssfx_gloss_method 0 ).
// Ported from Monolith xrRender_console.cpp:514.
float ps_r2_gloss_min = 0.0f;

// Screen Space Shaders
// The ps_ssfx_* variables are NOT defined here. Parts of them are read by xrEngine itself
// (Environment wind animation and wetness), and this file is compiled separately into every
// renderer DLL. They live in xrEngine/xr_ioc_cmd.cpp as ENGINE_API instead, so there is a single
// shared instance.

#ifndef _EDITOR
#include "xrEngine/xr_ioconsole.h"
#include "xrEngine/xr_ioc_cmd.h"
#include "xrEngine/ssfx_console_vars.h"

#if defined(USE_DX10) || defined(USE_DX11)
#include "../xrRenderDX10/StateManager/dx10SamplerStateCache.h"
#endif //	USE_DX10

//-----------------------------------------------------------------------
class CCC_tf_Aniso : public CCC_Integer {
public:
    void apply() {
        if (0 == HW.pDevice)
            return;
        int val = *value;
        clamp(val, 1, 16);
#if defined(USE_DX10) || defined(USE_DX11)
        SSManager.SetMaxAnisotropy(val);
#else  //	USE_DX10
        for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, val));
#endif //	USE_DX10
    }
    CCC_tf_Aniso(LPCSTR N, int* v) : CCC_Integer(N, v, 1, 16){};
    virtual void Execute(LPCSTR args) {
        CCC_Integer::Execute(args);
        apply();
    }
    virtual void Status(TStatus& S) {
        CCC_Integer::Status(S);
        apply();
    }
};
class CCC_tf_MipBias : public CCC_Float {
public:
    void apply() {
        if (0 == HW.pDevice)
            return;

#if defined(USE_DX10) || defined(USE_DX11)
//	TODO: DX10: Implement mip bias control
// VERIFY(!"apply not implmemented.");
#else  //	USE_DX10
        for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
            CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, *((LPDWORD)value)));
#endif //	USE_DX10
    }

    CCC_tf_MipBias(LPCSTR N, float* v) : CCC_Float(N, v, -0.5f, +0.5f){};
    virtual void Execute(LPCSTR args) {
        CCC_Float::Execute(args);
        apply();
    }
    virtual void Status(TStatus& S) {
        CCC_Float::Status(S);
        apply();
    }
};
class CCC_R2GM : public CCC_Float {
public:
    CCC_R2GM(LPCSTR N, float* v) : CCC_Float(N, v, 0.f, 4.f) { *v = 0; };
    virtual void Execute(LPCSTR args) {
        if (0 == xr_strcmp(args, "on")) {
            ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL, TRUE);
        } else if (0 == xr_strcmp(args, "off")) {
            ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL, FALSE);
        } else {
            CCC_Float::Execute(args);
            if (ps_r2_ls_flags.test(R2FLAG_GLOBALMATERIAL)) {
                static LPCSTR name[4] = { "oren", "blin", "phong", "metal" };
                float mid = *value;
                int m0 = iFloor(mid) % 4;
                int m1 = (m0 + 1) % 4;
                float frc = mid - float(iFloor(mid));
                Msg("* material set to [%s]-[%s], with lerp of [%f]", name[m0], name[m1], frc);
            }
        }
    }
};
class CCC_Screenshot : public IConsole_Command {
public:
    CCC_Screenshot(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args) {
        string_path name;
        name[0] = 0;
        sscanf(args, "%s", name);
        LPCSTR image = xr_strlen(name) ? name : 0;
        ::Render->Screenshot(IRender_interface::SM_NORMAL, image);
    }
};

class CCC_RestoreQuadIBData : public IConsole_Command {
public:
    CCC_RestoreQuadIBData(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args) { RCache.RestoreQuadIBData(); }
};

class CCC_ModelPoolStat : public IConsole_Command {
public:
    CCC_ModelPoolStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { RImplementation.Models->dump(); }
};

class CCC_SSAO_Mode : public CCC_Token {
public:
    CCC_SSAO_Mode(LPCSTR N, u32* V, xr_token* T) : CCC_Token(N, V, T){};

    virtual void Execute(LPCSTR args) {
        CCC_Token::Execute(args);

        switch (*value) {
        case 0: {
            ps_r_ssao = 0;
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            break;
        }
        case 1: {
            if (ps_r_ssao == 0) {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
            break;
        }
        case 2: {
            if (ps_r_ssao == 0) {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 1);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
            break;
        }
        case 3: {
            if (ps_r_ssao == 0) {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 1);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 1);
            break;
        }
        }
    }
};

//-----------------------------------------------------------------------
class CCC_Preset : public CCC_Token {
public:
    CCC_Preset(LPCSTR N, u32* V, xr_token* T) : CCC_Token(N, V, T){};

    virtual void Execute(LPCSTR args) {
        CCC_Token::Execute(args);
        string_path _cfg;
        string_path cmd;

        switch (*value) {
        case 0:
            xr_strcpy(_cfg, "rspec_minimum.ltx");
            break;
        case 1:
            xr_strcpy(_cfg, "rspec_low.ltx");
            break;
        case 2:
            xr_strcpy(_cfg, "rspec_default.ltx");
            break;
        case 3:
            xr_strcpy(_cfg, "rspec_high.ltx");
            break;
        case 4:
            xr_strcpy(_cfg, "rspec_extreme.ltx");
            break;
        }
        FS.update_path(_cfg, "$game_config$", _cfg);
        strconcat(sizeof(cmd), cmd, "cfg_load", " ", _cfg);
        Console->Execute(cmd);
    }
};

class CCC_memory_stats : public IConsole_Command {
protected:
public:
    CCC_memory_stats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };

    virtual void Execute(LPCSTR args) {
        u32 m_base = 0;
        u32 c_base = 0;
        u32 m_lmaps = 0;
        u32 c_lmaps = 0;

        dxRenderDeviceRender::Instance().ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

        Msg("memory usage  mb \t \t video    \t managed      \t system \n");

        float vb_video = (float)HW.stats_manager
                             .memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_DEFAULT] /
                         1024 / 1024;
        float vb_managed =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_MANAGED] /
            1024 / 1024;
        float vb_system =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_SYSTEMMEM] /
            1024 / 1024;
        Msg("vertex buffer      \t \t %f \t %f \t %f ", vb_video, vb_managed, vb_system);

        float ib_video = (float)HW.stats_manager
                             .memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_DEFAULT] /
                         1024 / 1024;
        float ib_managed =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_MANAGED] /
            1024 / 1024;
        float ib_system =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_SYSTEMMEM] /
            1024 / 1024;
        Msg("index buffer      \t \t %f \t %f \t %f ", ib_video, ib_managed, ib_system);

        float textures_managed = (float)(m_base + m_lmaps) / 1024 / 1024;
        Msg("textures          \t \t %f \t %f \t %f ", 0.f, textures_managed, 0.f);

        float rt_video =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_DEFAULT] /
            1024 / 1024;
        float rt_managed =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_MANAGED] /
            1024 / 1024;
        float rt_system =
            (float)HW.stats_manager
                .memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_SYSTEMMEM] /
            1024 / 1024;
        Msg("R-Targets         \t \t %f \t %f \t %f ", rt_video, rt_managed, rt_system);

        Msg("\nTotal             \t \t %f \t %f \t %f ", vb_video + ib_video + rt_video,
            textures_managed + vb_managed + ib_managed + rt_managed,
            vb_system + ib_system + rt_system);
    }
};

#if RENDER != R_R1
#include "r__pixel_calculator.h"
class CCC_BuildSSA : public IConsole_Command {
public:
    CCC_BuildSSA(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
#if !defined(USE_DX10) && !defined(USE_DX11)
        //	TODO: DX10: Implement pixel calculator
        r_pixel_calculator c;
        c.run();
#endif //	USE_DX10
    }
};
#endif

class CCC_DofFar : public CCC_Float {
public:
    CCC_DofFar(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
        : CCC_Float(N, V, _min, _max) {}

    virtual void Execute(LPCSTR args) {
        float v = float(atof(args));

        if (v < ps_r2_dof.y + 0.1f) {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]),
                      "float value greater or equal to r2_dof_focus+0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_focus");
        } else {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    //	CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* F) { ; }
};

class CCC_DofNear : public CCC_Float {
public:
    CCC_DofNear(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
        : CCC_Float(N, V, _min, _max) {}

    virtual void Execute(LPCSTR args) {
        float v = float(atof(args));

        if (v > ps_r2_dof.y - 0.1f) {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]),
                      "float value less or equal to r2_dof_focus-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_focus");
        } else {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    //	CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* F) { ; }
};

class CCC_DofFocus : public CCC_Float {
public:
    CCC_DofFocus(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
        : CCC_Float(N, V, _min, _max) {}

    virtual void Execute(LPCSTR args) {
        float v = float(atof(args));

        if (v > ps_r2_dof.z - 0.1f) {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]),
                      "float value less or equal to r2_dof_far-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_far");
        } else if (v < ps_r2_dof.x + 0.1f) {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]),
                      "float value greater or equal to r2_dof_far-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_near");
        } else {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    //	CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* F) { ; }
};

class CCC_Dof : public CCC_Vector3 {
public:
    CCC_Dof(LPCSTR N, Fvector* V, const Fvector _min, const Fvector _max)
        : CCC_Vector3(N, V, _min, _max) {
        ;
    }

    virtual void Execute(LPCSTR args) {
        Fvector v;
        if (3 != sscanf(args, "%f,%f,%f", &v.x, &v.y, &v.z))
            InvalidSyntax();
        else if ((v.x > v.y - 0.1f) || (v.z < v.y + 0.1f)) {
            InvalidSyntax();
            Msg("x <= y - 0.1");
            Msg("y <= z - 0.1");
        } else {
            CCC_Vector3::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }
    virtual void Status(TStatus& S) { xr_sprintf(S, "%f,%f,%f", value->x, value->y, value->z); }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, "vector3 in range [%f,%f,%f]-[%f,%f,%f]", min.x, min.y, min.z, max.x, max.y,
                   max.z);
    }
};

class CCC_DumpResources : public IConsole_Command {
public:
    CCC_DumpResources(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        RImplementation.Models->dump();
        dxRenderDeviceRender::Instance().Resources->Dump(false);
    }
};

//	Allow real-time fog config reload
#if (RENDER == R_R3) || (RENDER == R_R4)
#ifdef DEBUG

#include "../xrRenderDX10/3DFluid/dx103DFluidManager.h"

class CCC_Fog_Reload : public IConsole_Command {
public:
    CCC_Fog_Reload(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { FluidManager.UpdateProfiles(); }
};
#endif //	DEBUG
#endif //	(RENDER == R_R3) || (RENDER == R_R4)

class CCC_ssfx_cascades : public CCC_Vector3 {
public:
    void apply() {
#if defined(USE_DX10) || defined(USE_DX11)
        RImplementation.init_cacades();
#endif
    }

    CCC_ssfx_cascades(LPCSTR N, Fvector3* V, const Fvector3 _min, const Fvector3 _max)
        : CCC_Vector3(N, V, _min, _max){};

    virtual void Execute(LPCSTR args) {
        CCC_Vector3::Execute(args);
        apply();
    }

    virtual void Status(TStatus& S) {
        CCC_Vector3::Status(S);
        apply();
    }
};

// One sun cascade size for the SSFX settings page. ssfx_shadow_cascades carries the three of them
// at once; the page has a slider for each, and the cascades have to be rebuilt after any of them
// moves, exactly as after the whole command.
class CCC_ssfx_ui_cascade : public CCC_Float {
public:
    CCC_ssfx_ui_cascade(LPCSTR N, float* V, float _min, float _max)
        : CCC_Float(N, V, _min, _max){};

    virtual void Execute(LPCSTR args) {
        CCC_Float::Execute(args);
#if defined(USE_DX10) || defined(USE_DX11)
        RImplementation.init_cacades();
#endif
    }
};

// The rain cover map's resolution as the author lists it: his entry N stands for 2^( N + 6 ).
static xr_token tok_wet_cover_res[] = {
    { "ui_mcm_lst_ssfx_cover_64", 64 },
    { "ui_mcm_lst_ssfx_cover_128", 128 },
    { "ui_mcm_lst_ssfx_cover_256", 256 },
    { "ui_mcm_lst_ssfx_cover_512", 512 },
    { "ui_mcm_lst_ssfx_cover_1024", 1024 },
    { "ui_mcm_lst_ssfx_cover_2048", 2048 },
    { 0, 0 },
};

//-----------------------------------------------------------------------
void xrRender_initconsole() {
    CMD3(CCC_Preset, "_preset", &ps_Preset, qpreset_token);

    CMD4(CCC_Integer, "rs_skeleton_update", &psSkeletonUpdate, 2, 128);
#ifdef DEBUG
    CMD1(CCC_DumpResources, "dump_resources");
#endif //	 DEBUG

    CMD4(CCC_Float, "r__dtex_range", &r__dtex_range, 5, 175);

    // Common
    CMD1(CCC_Screenshot, "screenshot");

    //	Igor: just to test bug with rain/particles corruption
    CMD1(CCC_RestoreQuadIBData, "r_restore_quad_ib_data");
#ifdef DEBUG
#if RENDER != R_R1
    CMD1(CCC_BuildSSA, "build_ssa");
#endif
    CMD4(CCC_Integer, "r__lsleep_frames", &ps_r__LightSleepFrames, 4, 30);
    CMD4(CCC_Float, "r__ssa_glod_start", &ps_r__GLOD_ssa_start, 128, 512);
    CMD4(CCC_Float, "r__ssa_glod_end", &ps_r__GLOD_ssa_end, 16, 96);
    CMD4(CCC_Float, "r__wallmark_shift_pp", &ps_r__WallmarkSHIFT, 0.0f, 1.f);
    CMD4(CCC_Float, "r__wallmark_shift_v", &ps_r__WallmarkSHIFT_V, 0.0f, 1.f);
    CMD1(CCC_ModelPoolStat, "stat_models");
#endif // DEBUG
    CMD4(CCC_Float, "r__wallmark_ttl", &ps_r__WallmarkTTL, 1.0f, 5.f * 60.f);

    CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 8);

    Fvector tw_min, tw_max;

    CMD4(CCC_Float, "r__geometry_lod", &ps_r__LOD, 0.1f, 1.2f);
    //.	CMD4(CCC_Float,		"r__geometry_lod_pow",	&ps_r__LOD_Power,			0,		2
    //);

    //.	CMD4(CCC_Float,		"r__detail_density",	&ps_r__Detail_density,		.05f,	0.99f
    //);
    // Grass density. Monolith xrRender_console.cpp:1267 registers this over 0.04 .. 1.0; Infernis
    // clamped it to 0.2 .. 0.6, so two thirds of the donor's travel was unreachable and the slider
    // could neither thin the grass out nor fill a field. The bounds below are the donor's own.
    // The default stays 0.3 rather than the donor's 0.6: the grass radius here is 152 m instead of
    // the donor's 48 m, so doubling the density would triple the blade count over ten times the
    // area on a profile that never touched the slider. Anyone who wants 0.6 can reach it now.
#if RENDER == R_R4
    CMD4(CCC_Float, "r__detail_density", &ps_r__Detail_density, 0.04f, 1.0f);
#else
    // The other renderers: the game's own range, Infernis xrRender_console.cpp:637.
    CMD4(CCC_Float, "r__detail_density", &ps_r__Detail_density, .2f, 0.6f);
#endif

    // Screen Space Shaders. Grass height, Monolith xrRender_console.cpp:1269. Same bounds and
    // default as the donor. Like r__detail_density above it writes the variable the decompressor
    // reads, so a new value lands on every slot unpacked after the command, without a map reload.
    CMD4(CCC_Float, "r__detail_height", &ps_r__Detail_height, 0.5f, 2.0f);

    // Screen Space Shaders. Grass draw distance. Monolith exposes this as r__detail_radius and
    // rebuilds the whole grass cache for it; here the cache is a fixed 152 m and this shortens it,
    // which is the same thing to the player and needs no map reload. Upper bound is the cache.
    CMD4(CCC_Float, "r__detail_radius", &ps_r__Detail_radius, 8.0f, 152.0f);

    // Screen Space Shaders. Monolith xrRender_console.cpp:1712-1713. The first switches grass
    // casting shadows on, the second stops a blade shrinking as it fades out in the distance.
    CMD3(CCC_Mask, "r__enable_grass_shadow", &psDeviceFlags2, rsGrassShadow);
    CMD3(CCC_Mask, "r__no_scale_on_fade", &psDeviceFlags2, rsNoScale);

	CMD4(CCC_Float, "r__d_tree_w_amp", &ps_r__Tree_w_amp, .001f, 1.f);
    CMD4(CCC_Float, "r__d_tree_w_rot", &ps_r__Tree_w_rot, .01f, 100.f);
    CMD4(CCC_Float, "r__d_tree_w_speed", &ps_r__Tree_w_speed, 1.0f, 10.f);
	tw_min.set(EPS, EPS, EPS);
    tw_max.set(2, 2, 2);
    CMD4(CCC_Vector3, "r__d_tree_wave", &ps_r__Tree_Wave, tw_min, tw_max);

#ifdef DEBUG
    CMD4(CCC_Float, "r__detail_l_ambient", &ps_r__Detail_l_ambient, .5f, .95f);
    CMD4(CCC_Float, "r__detail_l_aniso", &ps_r__Detail_l_aniso, .1f, .5f);

#endif // DEBUG

    CMD2(CCC_tf_Aniso, "r__tf_aniso", &ps_r__tf_Anisotropic); //	{1..16}

    // R1
    CMD4(CCC_Float, "r1_ssa_lod_a", &ps_r1_ssaLOD_A, 16, 96);
    CMD4(CCC_Float, "r1_ssa_lod_b", &ps_r1_ssaLOD_B, 16, 64);
    CMD4(CCC_Float, "r1_lmodel_lerp", &ps_r1_lmodel_lerp, 0, 0.333f);
    CMD2(CCC_tf_MipBias, "r1_tf_mipbias", &ps_r1_tf_Mipbias); //	{-3 +3}
    CMD3(CCC_Mask, "r1_dlights", &ps_r1_flags, R1FLAG_DLIGHTS);
    CMD4(CCC_Float, "r1_dlights_clip", &ps_r1_dlights_clip, 10.f, 150.f);
    CMD4(CCC_Float, "r1_pps_u", &ps_r1_pps_u, -1.f, +1.f);
    CMD4(CCC_Float, "r1_pps_v", &ps_r1_pps_v, -1.f, +1.f);

    // R1-specific
    CMD4(CCC_Integer, "r1_glows_per_frame", &ps_r1_GlowsPerFrame, 2, 32);
    CMD3(CCC_Mask, "r1_detail_textures", &ps_r2_ls_flags, R1FLAG_DETAIL_TEXTURES);

    CMD4(CCC_Float, "r1_fog_luminance", &ps_r1_fog_luminance, 0.2f, 5.f);

    // Software Skinning
    // 0 - disabled (renderer can override)
    // 1 - enabled
    // 2 - forced hardware skinning (renderer can not override)
    CMD4(CCC_Integer, "r1_software_skinning", &ps_r1_SoftwareSkinning, 0, 2);

    // R2
    CMD4(CCC_Float, "r2_ssa_lod_a", &ps_r2_ssaLOD_A, 16, 96);
    CMD4(CCC_Float, "r2_ssa_lod_b", &ps_r2_ssaLOD_B, 32, 64);
    CMD2(CCC_tf_MipBias, "r2_tf_mipbias", &ps_r2_tf_Mipbias);

    // R2-specific
    CMD2(CCC_R2GM, "r2em", &ps_r2_gmaterial);
    CMD3(CCC_Mask, "r2_tonemap", &ps_r2_ls_flags, R2FLAG_TONEMAP);
    CMD4(CCC_Float, "r2_tonemap_middlegray", &ps_r2_tonemap_middlegray, 0.0f, 2.0f);
    CMD4(CCC_Float, "r2_tonemap_adaptation", &ps_r2_tonemap_adaptation, 0.01f, 10.0f);
    CMD4(CCC_Float, "r2_tonemap_lowlum", &ps_r2_tonemap_low_lum, 0.0001f, 1.0f);
    CMD4(CCC_Float, "r2_tonemap_amount", &ps_r2_tonemap_amount, 0.0000f, 1.0f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_scale", &ps_r2_ls_bloom_kernel_scale, 0.5f, 2.f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_g", &ps_r2_ls_bloom_kernel_g, 1.f, 7.f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_b", &ps_r2_ls_bloom_kernel_b, 0.01f, 1.f);
    CMD4(CCC_Float, "r2_ls_bloom_threshold", &ps_r2_ls_bloom_threshold, 0.f, 1.f);
    CMD4(CCC_Float, "r2_ls_bloom_speed", &ps_r2_ls_bloom_speed, 0.f, 100.f);
    CMD3(CCC_Mask, "r2_ls_bloom_fast", &ps_r2_ls_flags, R2FLAG_FASTBLOOM);
    CMD4(CCC_Float, "r2_ls_dsm_kernel", &ps_r2_ls_dsm_kernel, .1f, 3.f);
    CMD4(CCC_Float, "r2_ls_psm_kernel", &ps_r2_ls_psm_kernel, .1f, 3.f);
    CMD4(CCC_Float, "r2_ls_ssm_kernel", &ps_r2_ls_ssm_kernel, .1f, 3.f);
    // Screen Space Shaders: the upper limit is the donor's 3, not stock CoP's 1. The author's
    // "shadow LOD quality" slider runs 0.5 .. 3 and with the old limit everything above 1 was
    // refused by the console. The value is only a multiplier in Light_Render_Direct_ComputeXFS
    // and the resulting size is clamped to ssfx_shadows there, same as in the donor.
#if RENDER == R_R4
    CMD4(CCC_Float, "r2_ls_squality", &ps_r2_ls_squality, .5f, 3.f);
#else
    // The other renderers: the game's own range, Infernis xrRender_console.cpp:697.
    CMD4(CCC_Float, "r2_ls_squality", &ps_r2_ls_squality, .5f, 1.f);
#endif

    CMD3(CCC_Mask, "r2_zfill", &ps_r2_ls_flags, R2FLAG_ZFILL);
    CMD4(CCC_Float, "r2_zfill_depth", &ps_r2_zfill, .001f, .5f);
    CMD3(CCC_Mask, "r2_allow_r1_lights", &ps_r2_ls_flags, R2FLAG_R1LIGHTS);

    //- Mad Max
    CMD4(CCC_Float, "r2_gloss_factor", &ps_r2_gloss_factor, .0f, 10.f);
    // Ported from Monolith xrRender_console.cpp:1343, same range.
    CMD4(CCC_Float, "r2_gloss_min", &ps_r2_gloss_min, .001f, 1.0f);
//- Mad Max

#ifdef DEBUG
    CMD3(CCC_Mask, "r2_use_nvdbt", &ps_r2_ls_flags, R2FLAG_USE_NVDBT);
    CMD3(CCC_Mask, "r2_mt", &ps_r2_ls_flags, R2FLAG_EXP_MT_CALC);
#endif // DEBUG

    CMD3(CCC_Mask, "r2_sun", &ps_r2_ls_flags, R2FLAG_SUN);
    CMD3(CCC_Mask, "r2_sun_details", &ps_r2_ls_flags, R2FLAG_SUN_DETAILS);
    CMD3(CCC_Mask, "r2_sun_focus", &ps_r2_ls_flags, R2FLAG_SUN_FOCUS);
    //	CMD3(CCC_Mask,		"r2_sun_static",		&ps_r2_ls_flags,
    //R2FLAG_SUN_STATIC); 	CMD3(CCC_Mask,		"r2_exp_splitscene",	&ps_r2_ls_flags,
    //R2FLAG_EXP_SPLIT_SCENE); 	CMD3(CCC_Mask,		"r2_exp_donttest_uns",	&ps_r2_ls_flags,
    //R2FLAG_EXP_DONT_TEST_UNSHADOWED);
    CMD3(CCC_Mask, "r2_exp_donttest_shad", &ps_r2_ls_flags, R2FLAG_EXP_DONT_TEST_SHADOWED);

    CMD3(CCC_Mask, "r2_sun_tsm", &ps_r2_ls_flags, R2FLAG_SUN_TSM);
    CMD4(CCC_Float, "r2_sun_tsm_proj", &ps_r2_sun_tsm_projection, .001f, 0.8f);
    CMD4(CCC_Float, "r2_sun_tsm_bias", &ps_r2_sun_tsm_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_near", &ps_r2_sun_near, 1.f, 50.f);
#if RENDER != R_R1
    CMD4(CCC_Float, "r2_sun_far", &OLES_SUN_LIMIT_27_01_07, 51.f, 180.f);
#endif
    CMD4(CCC_Float, "r2_sun_near_border", &ps_r2_sun_near_border, .5f, 1.0f);
    CMD4(CCC_Float, "r2_sun_depth_far_scale", &ps_r2_sun_depth_far_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_sun_depth_far_bias", &ps_r2_sun_depth_far_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_depth_near_scale", &ps_r2_sun_depth_near_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_sun_depth_near_bias", &ps_r2_sun_depth_near_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_lumscale", &ps_r2_sun_lumscale, -1.0, +3.0);
    CMD4(CCC_Float, "r2_sun_lumscale_hemi", &ps_r2_sun_lumscale_hemi, 0.0, +3.0);
    CMD4(CCC_Float, "r2_sun_lumscale_amb", &ps_r2_sun_lumscale_amb, 0.0, +3.0);

    CMD3(CCC_Mask, "r2_aa", &ps_r2_ls_flags, R2FLAG_AA);
    CMD4(CCC_Float, "r2_aa_kernel", &ps_r2_aa_kernel, 0.3f, 0.7f);
    CMD4(CCC_Float, "r2_mblur", &ps_r2_mblur, 0.0f, 1.0f);

    CMD3(CCC_Mask, "r2_gi", &ps_r2_ls_flags, R2FLAG_GI);
    CMD4(CCC_Float, "r2_gi_clip", &ps_r2_GI_clip, EPS, 0.1f);
    CMD4(CCC_Integer, "r2_gi_depth", &ps_r2_GI_depth, 1, 5);
    CMD4(CCC_Integer, "r2_gi_photons", &ps_r2_GI_photons, 8, 256);
    CMD4(CCC_Float, "r2_gi_refl", &ps_r2_GI_refl, EPS_L, 0.99f);

    CMD4(CCC_Integer, "r2_wait_sleep", &ps_r2_wait_sleep, 0, 1);

#ifndef MASTER_GOLD
    CMD4(CCC_Integer, "r2_dhemi_count", &ps_r2_dhemi_count, 4, 25);
    CMD4(CCC_Float, "r2_dhemi_sky_scale", &ps_r2_dhemi_sky_scale, 0.0f, 100.f);
    CMD4(CCC_Float, "r2_dhemi_light_scale", &ps_r2_dhemi_light_scale, 0, 100.f);
    CMD4(CCC_Float, "r2_dhemi_light_flow", &ps_r2_dhemi_light_flow, 0, 1.f);
    CMD4(CCC_Float, "r2_dhemi_smooth", &ps_r2_lt_smooth, 0.f, 10.f);
    CMD3(CCC_Mask, "rs_hom_depth_draw", &ps_r2_ls_flags_ext, R_FLAGEXT_HOM_DEPTH_DRAW);
    CMD3(CCC_Mask, "r2_shadow_cascede_zcul", &ps_r2_ls_flags_ext, R2FLAGEXT_SUN_ZCULLING);
    CMD3(CCC_Mask, "r2_shadow_cascede_old", &ps_r2_ls_flags_ext, R2FLAGEXT_SUN_OLD);

#endif // DEBUG

    CMD4(CCC_Float, "r2_ls_depth_scale", &ps_r2_ls_depth_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_ls_depth_bias", &ps_r2_ls_depth_bias, -0.5, +0.5);

    CMD4(CCC_Float, "r2_parallax_h", &ps_r2_df_parallax_h, .0f, .5f);
    //	CMD4(CCC_Float,		"r2_parallax_range",	&ps_r2_df_parallax_range,	5.0f,	175.0f
    //);

    CMD4(CCC_Float, "r2_slight_fade", &ps_r2_slight_fade, .2f, 1.f);

    tw_min.set(0, 0, 0);
    tw_max.set(1, 1, 1);
    CMD4(CCC_Vector3, "r2_aa_break", &ps_r2_aa_barier, tw_min, tw_max);

    tw_min.set(0, 0, 0);
    tw_max.set(1, 1, 1);
    CMD4(CCC_Vector3, "r2_aa_weight", &ps_r2_aa_weight, tw_min, tw_max);

    //	Igor: Depth of field
    tw_min.set(-10000, -10000, 0);
    tw_max.set(10000, 10000, 10000);
    CMD4(CCC_Dof, "r2_dof", &ps_r2_dof, tw_min, tw_max);
    CMD4(CCC_DofNear, "r2_dof_near", &ps_r2_dof.x, tw_min.x, tw_max.x);
    CMD4(CCC_DofFocus, "r2_dof_focus", &ps_r2_dof.y, tw_min.y, tw_max.y);
    CMD4(CCC_DofFar, "r2_dof_far", &ps_r2_dof.z, tw_min.z, tw_max.z);

    CMD4(CCC_Float, "r2_dof_kernel", &ps_r2_dof_kernel_size, .0f, 10.f);
    CMD4(CCC_Float, "r2_dof_sky", &ps_r2_dof_sky, -10000.f, 10000.f);
    CMD3(CCC_Mask, "r2_dof_enable", &ps_r2_ls_flags, R2FLAG_DOF);

    //	float		ps_r2_dof_near			= 0.f;					//
    //0.f 	float		ps_r2_dof_focus			= 1.4f;
    //// 1.4f

    CMD3(CCC_Mask, "r2_volumetric_lights", &ps_r2_ls_flags, R2FLAG_VOLUMETRIC_LIGHTS);
    //	CMD3(CCC_Mask,		"r2_sun_shafts",				&ps_r2_ls_flags,
    //R2FLAG_SUN_SHAFTS);
    CMD3(CCC_Token, "r2_sun_shafts", &ps_r_sun_shafts, qsun_shafts_token);
    CMD3(CCC_Token, "r2_sunshafts_mode", &ps_sunshafts_mode, sunshafts_mode_token);
    CMD4(CCC_Float, "r2_ss_sunshafts_length", &ps_r2_ss_sunshafts_length, .2f, 1.5f);
    CMD4(CCC_Float, "r2_ss_sunshafts_radius", &ps_r2_ss_sunshafts_radius, .5f, 2.f);
    CMD3(CCC_SSAO_Mode, "r2_ssao_mode", &ps_r_ssao_mode, qssao_mode_token);
    CMD3(CCC_Token, "r2_ssao", &ps_r_ssao, qssao_token);
    CMD3(CCC_Mask, "r2_ssao_blur", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_BLUR); // Need restart
    CMD3(CCC_Mask, "r2_ssao_opt_data", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_OPT_DATA); // Need
                                                                                      // restart
    CMD3(CCC_Mask, "r2_ssao_half_data", &ps_r2_ls_flags_ext,
         R2FLAGEXT_SSAO_HALF_DATA);                                           // Need restart
    CMD3(CCC_Mask, "r2_ssao_hbao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HBAO); // Need restart
    CMD3(CCC_Mask, "r2_ssao_hdao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HDAO); // Need restart
    CMD3(CCC_Mask, "r4_enable_tessellation", &ps_r2_ls_flags_ext,
         R2FLAGEXT_ENABLE_TESSELLATION);                                      // Need restart
    CMD3(CCC_Mask, "r4_wireframe", &ps_r2_ls_flags_ext, R2FLAGEXT_WIREFRAME); // Need restart
    CMD3(CCC_Mask, "r2_steep_parallax", &ps_r2_ls_flags, R2FLAG_STEEP_PARALLAX);
    CMD3(CCC_Mask, "r2_detail_bump", &ps_r2_ls_flags, R2FLAG_DETAIL_BUMP);
    CMD3(CCC_Mask, "r2_raindrops", &ps_r2_rain_drops_flags, R2FLAG_RAIN_DROPS);
    CMD3(CCC_Mask, "r2_rain_drops_control", &ps_r2_rain_drops_flags, R2FLAG_RAIN_DROPS_CONTROL);
    CMD4(CCC_Float, "r2_rain_drops_intensity", &ps_r2_rain_drops_intensity, 0.f, 1.f);
    CMD4(CCC_Float, "r2_rain_drops_speed", &ps_r2_rain_drops_speed, 0.8f, 5.f);

    CMD3(CCC_Token, "r2_sun_quality", &ps_r_sun_quality, qsun_quality_token);

    //	Igor: need restart
    CMD3(CCC_Mask, "r2_soft_water", &ps_r2_ls_flags, R2FLAG_SOFT_WATER);
    CMD3(CCC_Mask, "r2_soft_particles", &ps_r2_ls_flags, R2FLAG_SOFT_PARTICLES);

    // CMD3(CCC_Mask,		"r3_msaa",						&ps_r2_ls_flags,
    // R3FLAG_MSAA);
    CMD3(CCC_Token, "r3_msaa", &ps_r3_msaa, qmsaa_token);
    // CMD3(CCC_Mask,		"r3_msaa_hybrid",				&ps_r2_ls_flags,
    // R3FLAG_MSAA_HYBRID);  CMD3(CCC_Mask,		"r3_msaa_opt",
    // &ps_r2_ls_flags,			R3FLAG_MSAA_OPT);
    CMD3(CCC_Mask, "r3_gbuffer_opt", &ps_r2_ls_flags, R3FLAG_GBUFFER_OPT);
    CMD3(CCC_Mask, "r3_use_dx10_1", &ps_r2_ls_flags, (u32)R3FLAG_USE_DX10_1);
    // CMD3(CCC_Mask,		"r3_msaa_alphatest",			&ps_r2_ls_flags,
    // (u32)R3FLAG_MSAA_ALPHATEST);
    CMD3(CCC_Token, "r3_msaa_alphatest", &ps_r3_msaa_atest, qmsaa__atest_token);
    CMD3(CCC_Token, "r3_minmax_sm", &ps_r3_minmax_sm, qminmax_sm_token);

//	Allow real-time fog config reload
#if (RENDER == R_R3) || (RENDER == R_R4)
#ifdef DEBUG
    CMD1(CCC_Fog_Reload, "r3_fog_reload");
#endif //	DEBUG
#endif //	(RENDER == R_R3) || (RENDER == R_R4)

    CMD3(CCC_Mask, "r3_dynamic_wet_surfaces", &ps_r2_ls_flags, R3FLAG_DYN_WET_SURF);
    CMD4(CCC_Float, "r3_dynamic_wet_surfaces_near", &ps_r3_dyn_wet_surf_near, 10, 70);
    CMD4(CCC_Float, "r3_dynamic_wet_surfaces_far", &ps_r3_dyn_wet_surf_far, 30, 100);
    CMD4(CCC_Integer, "r3_dynamic_wet_surfaces_sm_res", &ps_r3_dyn_wet_surf_sm_res, 64, 2048);

    CMD3(CCC_Mask, "r3_volumetric_smoke", &ps_r2_ls_flags, R3FLAG_VOLUMETRIC_SMOKE);
    CMD1(CCC_memory_stats, "render_memory_stats");

    //	CMD3(CCC_Mask,		"r2_sun_ignore_portals",		&ps_r2_ls_flags,
    //R2FLAG_SUN_IGNORE_PORTALS);

    // Screen Space Shaders commands are registered by xrEngine, next to their definitions. This one
    // lives here because it has to re-init the sun cascades.
    CMD4(CCC_ssfx_cascades, "ssfx_shadow_cascades", &ps_ssfx_shadow_cascades,
         Fvector3().set(1.0f, 1.0f, 1.0f), Fvector3().set(300, 300, 300));

    // Screen Space Shaders: the settings-page controls whose variables live in this module. What
    // these commands are, and the rest of them, is in xrEngine\xr_ioc_cmd.cpp.

    // --- shadows - ssfx_shadows_mcm.script
#if RENDER == R_R4
    CMD4(CCC_Float, "ssfx_ui_shw_lod_quality", &ps_r2_ls_squality, 0.5f, 3.0f); // lod_quality
#else
    // The other renderers: it writes r2_ls_squality, so the game's range of that, as above.
    CMD4(CCC_Float, "ssfx_ui_shw_lod_quality", &ps_r2_ls_squality, 0.5f, 1.0f); // lod_quality
#endif

    // --- shw_cascades - ssfx_shadow_cascades_mcm.script
    // size_1 ( range limited: ssfx_shadow_cascades.script:47-51 )
    CMD4(CCC_ssfx_ui_cascade, "ssfx_ui_cas_size_1", &ps_ssfx_shadow_cascades.x, 10.0f, 30.0f);
    // size_2 ( range limited: ssfx_shadow_cascades.script:53-57 )
    CMD4(CCC_ssfx_ui_cascade, "ssfx_ui_cas_size_2", &ps_ssfx_shadow_cascades.y, 40.0f, 110.0f);
    // size_3 ( range limited: ssfx_shadow_cascades.script:59-63 )
    CMD4(CCC_ssfx_ui_cascade, "ssfx_ui_cas_size_3", &ps_ssfx_shadow_cascades.z, 120.0f, 300.0f);

    // --- ssfx_wet_surf - ssfx_wetness_mcm.script
    // cover_res ( list ids run through 'pow(2, X + 6)' )
    CMD3(CCC_ssfx_ui_list_i, "ssfx_ui_wet_cover_res", &ps_r3_dyn_wet_surf_sm_res,
         tok_wet_cover_res);
    // cover_distance
    CMD4(CCC_Float, "ssfx_ui_wet_cover_distance", &ps_r3_dyn_wet_surf_far, 30.0f, 100.0f);
}

void xrRender_apply_tf() {
    Console->Execute("r__tf_aniso");
#if RENDER == R_R1
    Console->Execute("r1_tf_mipbias");
#else
    Console->Execute("r2_tf_mipbias");
#endif
}

#endif
