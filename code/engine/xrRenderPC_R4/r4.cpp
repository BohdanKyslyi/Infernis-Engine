#include "stdafx.h"
#include "r4.h"
#include "xrRenderCommon/fbasicvisual.h"
#include "xrEngine/xr_object.h"
#include "xrEngine/CustomHUD.h"
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include "xrRenderCommon/SkeletonCustom.h"
#include "xrRenderCommon/LightTrack.h"
#include "xrRenderCommon/dxRenderDeviceRender.h"
#include "xrRenderCommon/dxWallMarkArray.h"
#include "xrRenderCommon/dxUIShader.h"
#include "xrRenderCommon/infernis_pbr_settings.h"
#include "xrEngine/ssfx_console_vars.h"

#include "../xrRenderDX10/3DFluid/dx103DFluidManager.h"
#include "xrRenderCommon/ShaderResourceTraits.h"

#include "D3DX10Core.h"

#include <chrono> // item 9 stage B: the shader build measurement
#include <mutex>

CRender RImplementation;

//////////////////////////////////////////////////////////////////////////
class CGlow : public IRender_Glow {
public:
    bool bActive;

public:
    CGlow() : bActive(false) {}
    virtual void set_active(bool b) { bActive = b; }
    virtual bool get_active() { return bActive; }
    virtual void set_position(const Fvector& P) {}
    virtual void set_direction(const Fvector& D) {}
    virtual void set_radius(float R) {}
    virtual void set_texture(LPCSTR name) {}
    virtual void set_color(const Fcolor& C) {}
    virtual void set_color(float r, float g, float b) {}
};

// Screen Space Shaders. Monolith r4.cpp:62-67, unchanged.
bool CRender::is_sun() {
    if (o.sunstatic)
        return FALSE;
    Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
    return (ps_r2_ls_flags.test(R2FLAG_SUN) &&
            (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS));
}

float r_dtex_range = 50.f;
//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq) {
    int id = SE_R2_SHADOW;
    if (CRender::PHASE_NORMAL == RImplementation.phase) {
        id = ((std::sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range) ? SE_R2_NORMAL_HQ
                                                                        : SE_R2_NORMAL_LQ;
    }
    return pVisual->shader->E[id]._get();
}
//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq) {
    int id = SE_R2_SHADOW;
    if (CRender::PHASE_NORMAL == RImplementation.phase) {
        // Screen Space Shaders - ground relief. Ported from Monolith r4.cpp:84-99.
        //
        // Everything else keeps the stock 50 metre cut between the detailed and the plain shader.
        // The terrain does not: its relief shader is expensive, so SSFX gives it three bands and a
        // distance the player sets - ssfx_terrain_quality.x, in tens of metres, the "Ground: relief
        // distance" slider. Past 240 metres it drops to the third band.
        //
        // Deviation from the donor ( user 2026-10-23 ): in Monolith the third band starts at 240
        // metres whatever the slider says and the slider stops at 40, so the detailed ground ends at
        // 240 metres at the most - that edge is the seam the player sees in the distance. Here the
        // slider goes to 100 ( xr_ioc_cmd.cpp ) and the third band waits until the detailed one has
        // ended; up to 24 on the slider the bands are the donor's.
        //
        // Blender_BmmD raises isLandscape on element 0 when it compiled the SSFX terrain shaders,
        // so this branch is reached only when the addon is installed and the terrain module is on.
        if (pVisual->shader->E[0]->flags.isLandscape) {
            float sec_dist = std::sqrt(cdist_sq) - pVisual->vis.sphere.R;
            id = (sec_dist < ps_ssfx_terrain_quality.x * 10) ? SE_R2_NORMAL_HQ : SE_R2_NORMAL_LQ;

            // Very low shader variation
            if (sec_dist > 240 && sec_dist >= ps_ssfx_terrain_quality.x * 10)
                id = 3;
        } else {
            id = ((std::sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range) ? SE_R2_NORMAL_HQ
                                                                            : SE_R2_NORMAL_LQ;
        }
    }
    return pVisual->shader->E[id]._get();
}
static class cl_parallax : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float h = ps_r2_df_parallax_h;
        RCache.set_c(C, h, -h / 2.f, 1.f / r_dtex_range, 1.f / r_dtex_range);
    }
} binder_parallax;

static class cl_LOD : public R_constant_setup {
    virtual void setup(R_constant* C) { RCache.LOD.set_LOD(C); }
} binder_LOD;

static class cl_pos_decompress_params : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float VertTan = -1.0f * tanf(deg2rad(Device.fFOV / 2.0f));
        float HorzTan = -VertTan / Device.fASPECT;

        RCache.set_c(C, HorzTan, VertTan, (2.0f * HorzTan) / (float)Device.dwWidth,
                     (2.0f * VertTan) / (float)Device.dwHeight);
    }
} binder_pos_decompress_params;

static class cl_pos_decompress_params2 : public R_constant_setup {
    virtual void setup(R_constant* C) {
        RCache.set_c(C, (float)Device.dwWidth, (float)Device.dwHeight, 1.0f / (float)Device.dwWidth,
                     1.0f / (float)Device.dwHeight);
    }
} binder_pos_decompress_params2;

static class cl_ie_pbr_hud_projection_params : public R_constant_setup {
    virtual void setup(R_constant* C) {
        extern ENGINE_API float psHUD_FOV;

        const float worldVertTan = tanf(deg2rad(Device.fFOV / 2.0f));
        const float hudVertTan = tanf(deg2rad(psHUD_FOV * Device.fFOV / 2.0f));
        const float hudToWorldTan = hudVertTan / std::max(worldVertTan, EPS_S);

        RCache.set_c(C, hudToWorldTan, psHUD_FOV, IE_VIEWPORT_NEAR, 0.0f);
    }
} binder_ie_pbr_hud_projection_params;

static class cl_scope_lens_state : public R_constant_setup {
    virtual void setup(R_constant* C) {
        const float active = Device.scopeLensActive && !Device.scopeLensPass ? 1.f : 0.f;
        const float mode = active && g_pGameLevel ? (float)g_pGameLevel->ScopeLensMode() : 0.f;
        const float detector = active && g_pGameLevel && g_pGameLevel->ScopeLensHasDetector() ? 1.f : 0.f;
        static int last_logged_mode = -1;
        if (active && int(mode) != last_logged_mode) {
            Msg("* ScopeLensShader: mode=%d detector=%d", int(mode), int(detector));
            last_logged_mode = int(mode);
        }
        RCache.set_c(C, active, mode, detector, 0.f);
    }
} binder_scope_lens_state;

static class cl_scope_lens_glass : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float params[4] = { 0.12f, 0.025f, 0.40f, 0.f };
        if (Device.scopeLensActive && !Device.scopeLensPass && g_pGameLevel)
            g_pGameLevel->ScopeLensGlass(params);
        RCache.set_c(C, params[0], params[1], params[2], params[3]);
    }
} binder_scope_lens_glass;

static class cl_scope_lens_targets : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float targets[8 * 4];
        for (u32 i = 0; i < 8 * 4; ++i)
            targets[i] = -1.f;
        if (Device.scopeLensActive && !Device.scopeLensPass && g_pGameLevel &&
            g_pGameLevel->ScopeLensHasDetector())
            g_pGameLevel->ScopeLensTargets(targets, 8);
        for (u32 i = 0; i < 8; ++i)
            RCache.set_ca(C, i, targets[i * 4 + 0], targets[i * 4 + 1],
                targets[i * 4 + 2], targets[i * 4 + 3]);
    }
} binder_scope_lens_targets;

static class cl_scope_lens_size : public R_constant_setup {
    virtual void setup(R_constant* C) {
        float chromatic_pixels = 0.f;
        if (Device.scopeLensActive && !Device.scopeLensPass && pSettings &&
            pSettings->section_exist("shader_extensions")) {
            chromatic_pixels = 0.8f;
            if (pSettings->line_exist("shader_extensions", "scope_lens_chromatic_aberration"))
                chromatic_pixels = pSettings->r_float("shader_extensions", "scope_lens_chromatic_aberration");
            clamp(chromatic_pixels, 0.f, 10.f);
        }
        RCache.set_c(C, (float)Device.dwWidth, (float)Device.dwHeight,
                     chromatic_pixels, 0.f);
    }
} binder_scope_lens_size;

static class cl_water_intensity : public R_constant_setup {
    virtual void setup(R_constant* C) {
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        float fValue = E.m_fWaterIntensity;
        RCache.set_c(C, fValue, fValue, fValue, 0);
    }
} binder_water_intensity;

static class cl_sun_shafts_intensity : public R_constant_setup {
    virtual void setup(R_constant* C) {
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        float fValue = E.m_fSunShaftsIntensity;
        RCache.set_c(C, fValue, fValue, fValue, 0);
    }
} binder_sun_shafts_intensity;

static class cl_alpha_ref : public R_constant_setup {
    virtual void setup(R_constant* C) { StateManager.BindAlphaRef(C); }
} binder_alpha_ref;

extern ENGINE_API BOOL r2_sun_static;
extern ENGINE_API BOOL r2_advanced_pp; //	advanced post process and effects
//////////////////////////////////////////////////////////////////////////
// Just two static storage
void CRender::create() {
    Device.seqFrame.Add(this, REG_PRIORITY_HIGH + 0x12345678);

    m_skinning = -1;
    m_MSAASample = -1;

    // hardware
    o.smapsize = 2048;
    o.mrt = (HW.Caps.raster.dwMRT_count >= 3);
    o.mrtmixdepth = (HW.Caps.raster.b_MRT_mixdepth);

    // Check for NULL render target support
    //	DX10 disabled
    // D3DFORMAT	nullrt	= (D3DFORMAT)MAKEFOURCC('N','U','L','L');
    // o.nullrt			= HW.support	(nullrt,			D3DRTYPE_SURFACE,
    // D3DUSAGE_RENDERTARGET);
    o.nullrt = false;
    /*
    if (o.nullrt)		{
    Msg				("* NULLRT supported and used");
    };
    */
    if (o.nullrt) {
        Msg("* NULLRT supported");

        //.	    _tzset			();
        //.		??? _strdate	( date, 128 );	???
        //.		??? if (date < 22-march-07)
        if (0) {
            u32 device_id = HW.Caps.id_device;
            bool disable_nullrt = false;
            switch (device_id) {
            case 0x190:
            case 0x191:
            case 0x192:
            case 0x193:
            case 0x194:
            case 0x197:
            case 0x19D:
            case 0x19E: {
                disable_nullrt = true; // G80
                break;
            }
            case 0x400:
            case 0x401:
            case 0x402:
            case 0x403:
            case 0x404:
            case 0x405:
            case 0x40E:
            case 0x40F: {
                disable_nullrt = true; // G84
                break;
            }
            case 0x420:
            case 0x421:
            case 0x422:
            case 0x423:
            case 0x424:
            case 0x42D:
            case 0x42E:
            case 0x42F: {
                disable_nullrt = true; // G86
                break;
            }
            }
            if (disable_nullrt)
                o.nullrt = false;
        };
        if (o.nullrt)
            Msg("* ...and used");
    };

    // SMAP / DST
    o.HW_smap_FETCH4 = FALSE;
    //	DX10 disabled
    // o.HW_smap			= HW.support	(D3DFMT_D24X8,
    // D3DRTYPE_TEXTURE,D3DUSAGE_DEPTHSTENCIL);
    o.HW_smap = true;
    o.HW_smap_PCF = o.HW_smap;
    if (o.HW_smap) {
        //	For ATI it's much faster on DX10 to use D32F format
        if (HW.Caps.id_vendor == 0x1002)
            o.HW_smap_FORMAT = D3DFMT_D32F_LOCKABLE;
        else
            o.HW_smap_FORMAT = D3DFMT_D24X8;
        Msg("* HWDST/PCF supported and used");
    }

    //	DX10 disabled
    // o.fp16_filter		= HW.support	(D3DFMT_A16B16G16R16F,
    // D3DRTYPE_TEXTURE,D3DUSAGE_QUERY_FILTER);
    // o.fp16_blend		= HW.support	(D3DFMT_A16B16G16R16F,
    // D3DRTYPE_TEXTURE,D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING);
    o.fp16_filter = true;
    o.fp16_blend = true;

    // search for ATI formats
    if (!o.HW_smap && (0 == strstr(Core.Params, "-nodf24"))) {
        o.HW_smap = HW.support((D3DFORMAT)(MAKEFOURCC('D', 'F', '2', '4')), D3DRTYPE_TEXTURE,
                               D3DUSAGE_DEPTHSTENCIL);
        if (o.HW_smap) {
            o.HW_smap_FORMAT = MAKEFOURCC('D', 'F', '2', '4');
            o.HW_smap_PCF = FALSE;
            o.HW_smap_FETCH4 = TRUE;
        }
        Msg("* DF24/F4 supported and used [%X]", o.HW_smap_FORMAT);
    }

    // emulate ATI-R4xx series
    if (strstr(Core.Params, "-r4xx")) {
        o.mrtmixdepth = FALSE;
        o.HW_smap = FALSE;
        o.HW_smap_PCF = FALSE;
        o.fp16_filter = FALSE;
        o.fp16_blend = FALSE;
    }

    VERIFY2(o.mrt && (HW.Caps.raster.dwInstructions >= 256),
            "Hardware doesn't meet minimum feature-level");
    if (o.mrtmixdepth)
        o.albedo_wo = FALSE;
    else if (o.fp16_blend)
        o.albedo_wo = FALSE;
    else
        o.albedo_wo = TRUE;

    // nvstencil on NV40 and up
    o.nvstencil = FALSE;
    // if ((HW.Caps.id_vendor==0x10DE)&&(HW.Caps.id_device>=0x40))	o.nvstencil = TRUE;
    if (strstr(Core.Params, "-nonvs"))
        o.nvstencil = FALSE;

    // nv-dbt
    //	DX10 disabled
    // o.nvdbt				= HW.support	((D3DFORMAT)MAKEFOURCC('N','V','D','B'), D3DRTYPE_SURFACE,
    // 0);
    o.nvdbt = false;
    if (o.nvdbt)
        Msg("* NV-DBT supported and used");

    // options (smap-pool-size)
    if (strstr(Core.Params, "-smap1536"))
        o.smapsize = 1536;
    if (strstr(Core.Params, "-smap2048"))
        o.smapsize = 2048;
    if (strstr(Core.Params, "-smap2560"))
        o.smapsize = 2560;
    if (strstr(Core.Params, "-smap3072"))
        o.smapsize = 3072;
    if (strstr(Core.Params, "-smap4096"))
        o.smapsize = 4096;

    // gloss
    char* g = strstr(Core.Params, "-gloss ");
    o.forcegloss = g ? TRUE : FALSE;
    if (g) {
        o.forcegloss_v = float(atoi(g + xr_strlen("-gloss "))) / 255.f;
    }

    // options
    o.bug = (strstr(Core.Params, "-bug")) ? TRUE : FALSE;
    o.sunfilter = (strstr(Core.Params, "-sunfilter")) ? TRUE : FALSE;
    //.	o.sunstatic			= (strstr(Core.Params,"-sunstatic"))?	TRUE	:FALSE	;
    o.sunstatic = r2_sun_static;
    o.advancedpp = r2_advanced_pp;
    o.volumetricfog = ps_r2_ls_flags.test(R3FLAG_VOLUMETRIC_SMOKE);
    o.sjitter = (strstr(Core.Params, "-sjitter")) ? TRUE : FALSE;
    o.depth16 = (strstr(Core.Params, "-depth16")) ? TRUE : FALSE;
    o.noshadows = (strstr(Core.Params, "-noshadows")) ? TRUE : FALSE;
    o.Tshadows = (strstr(Core.Params, "-tsh")) ? TRUE : FALSE;
    o.mblur = (strstr(Core.Params, "-mblur")) ? TRUE : FALSE;
    o.distortion_enabled = (strstr(Core.Params, "-nodistort")) ? FALSE : TRUE;
    o.distortion = o.distortion_enabled;
    o.disasm = (strstr(Core.Params, "-disasm")) ? TRUE : FALSE;
    o.forceskinw = (strstr(Core.Params, "-skinw")) ? TRUE : FALSE;

    // Screen Space Shaders - the stock video options the addon rides on are the PLAYER'S.
    //
    // There was a block here that forced a combination of them at every start: r2_ssao and
    // r2_soft_water off, r2_volumetric_lights, r2_steep_parallax, r2_detail_bump,
    // r3_dynamic_wet_surfaces and r2_sun_shafts on. It was written for this port and Monolith has
    // nothing like it. Removed, because it was wrong twice over:
    //
    //   - it overwrote the player's own choice in the Video menu on every single start, in memory,
    //     after the menu had already saved the opposite to user.ltx. From the outside that reads as
    //     "the settings do not save".
    //   - r2_steep_parallax and r2_detail_bump change the NAME of the shader the renderer asks for,
    //     and the addon does not ship every variant under those names yet. The missing ones are not
    //     reported anywhere: the draw simply produces nothing, which is what tore the grass and the
    //     branches into streaks after each restart.
    //
    // The switches belong to the player until those variants exist. Anything a module truly cannot
    // run without has to be shown in the menu, not set behind the player's back.

    // Screen Space Shaders: SSS runs without the stock SSAO, anti-aliasing ( MSAA ) and
    // tessellation ( the user, 2026-10-27 ). Not behind the player's back: the video pages show
    // them grey while SSS is ticked ( ui_mm_opt_main.script, SSFXSwitch ), and the profile of the
    // game without SSS, user.ltx, keeps its own values. Set on the console values themselves - as
    // "r2_ssao_mode disabled", "r3_msaa st_opt_off" and "r4_enable_tessellation off" set them - so
    // what runs is what user_sss.ltx and the pages show. MSAA would also break the SSS shaders: they
    // read the motion vectors as a plain Texture2D.
    if (ssfx_session_enable) {
        ps_r_ssao_mode = 0;
        ps_r_ssao = 0;
        ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
        ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
        ps_r3_msaa = 0;
        ps_r2_ls_flags_ext.set(R2FLAGEXT_ENABLE_TESSELLATION, 0);
    }

    o.ssao_blur_on = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_BLUR) && (ps_r_ssao != 0);
    o.ssao_opt_data = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_OPT_DATA) && (ps_r_ssao != 0);
    o.ssao_half_data =
        ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HALF_DATA) && o.ssao_opt_data && (ps_r_ssao != 0);
    o.ssao_hdao = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HDAO) && (ps_r_ssao != 0);
    o.ssao_hbao = !o.ssao_hdao && ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HBAO) && (ps_r_ssao != 0);

    //	TODO: fix hbao shader to allow to perform per-subsample effect!
    o.hbao_vectorized = false;
    if (o.ssao_hbao) {
        if (HW.Caps.id_vendor == 0x1002)
            o.hbao_vectorized = true;
        o.ssao_opt_data = true;
    }

    if (o.ssao_hdao)
        o.ssao_opt_data = false;

    o.dx10_sm4_1 = ps_r2_ls_flags.test((u32)R3FLAG_USE_DX10_1);
    o.dx10_sm4_1 = o.dx10_sm4_1 && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1);

    //	MSAA option dependencies

    o.dx10_msaa = !!ps_r3_msaa;
    o.dx10_msaa_samples = (1 << ps_r3_msaa);

    o.dx10_msaa_opt = ps_r2_ls_flags.test(R3FLAG_MSAA_OPT);
    o.dx10_msaa_opt =
        o.dx10_msaa_opt && o.dx10_msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1) ||
        o.dx10_msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0);

    // o.dx10_msaa_hybrid	= ps_r2_ls_flags.test(R3FLAG_MSAA_HYBRID);
    o.dx10_msaa_hybrid = ps_r2_ls_flags.test((u32)R3FLAG_USE_DX10_1);
    o.dx10_msaa_hybrid &=
        !o.dx10_msaa_opt && o.dx10_msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1);

    //	Allow alpha test MSAA for DX10.0

    // o.dx10_msaa_alphatest= ps_r2_ls_flags.test((u32)R3FLAG_MSAA_ALPHATEST);
    // o.dx10_msaa_alphatest= o.dx10_msaa_alphatest && o.dx10_msaa;

    // o.dx10_msaa_alphatest_atoc= (o.dx10_msaa_alphatest && !o.dx10_msaa_opt &&
    // !o.dx10_msaa_hybrid);

    o.dx10_msaa_alphatest = 0;
    if (o.dx10_msaa) {
        if (o.dx10_msaa_opt || o.dx10_msaa_hybrid) {
            if (ps_r3_msaa_atest == 1)
                o.dx10_msaa_alphatest = MSAA_ATEST_DX10_1_ATOC;
            else if (ps_r3_msaa_atest == 2)
                o.dx10_msaa_alphatest = MSAA_ATEST_DX10_1_NATIVE;
        } else {
            if (ps_r3_msaa_atest)
                o.dx10_msaa_alphatest = MSAA_ATEST_DX10_0_ATOC;
        }
    }

    o.dx10_gbuffer_opt = ps_r2_ls_flags.test(R3FLAG_GBUFFER_OPT);

    o.dx10_minmax_sm = ps_r3_minmax_sm;
    o.dx10_minmax_sm_screenarea_threshold = 1600 * 1200;

    o.dx11_enable_tessellation = HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0 &&
                                 ps_r2_ls_flags_ext.test(R2FLAGEXT_ENABLE_TESSELLATION);

    if (o.dx10_minmax_sm == MMSM_AUTODETECT) {
        o.dx10_minmax_sm = MMSM_OFF;

        //	AMD device
        if (HW.Caps.id_vendor == 0x1002) {
            if (ps_r_sun_quality >= 3)
                o.dx10_minmax_sm = MMSM_AUTO;
            else if (ps_r_sun_shafts >= 2) {
                o.dx10_minmax_sm = MMSM_AUTODETECT;
                //	Check resolution in runtime in use_minmax_sm_this_frame
                o.dx10_minmax_sm_screenarea_threshold = 1600 * 1200;
            }
        }

        //	NVidia boards
        if (HW.Caps.id_vendor == 0x10DE) {
            if ((ps_r_sun_shafts >= 2)) {
                o.dx10_minmax_sm = MMSM_AUTODETECT;
                //	Check resolution in runtime in use_minmax_sm_this_frame
                o.dx10_minmax_sm_screenarea_threshold = 1280 * 1024;
            }
        }
    }

    // Screen Space Shaders - each module stays off unless its shader is actually installed
    string_path ssfx_fn;
    o.ssfx_core = FS.exist(ssfx_fn, "$game_shaders$", "r3\\screenspace_common", ".h") ? 1 : 0;
    o.ssfx_rain = FS.exist(ssfx_fn, "$game_shaders$", "r3\\effects_rain_splash", ".ps") ? 1 : 0;
    o.ssfx_blood = FS.exist(ssfx_fn, "$game_shaders$", "r3\\effects_wallmark_blood", ".ps") ? 1 : 0;
    o.ssfx_branches =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\deffer_tree_branch_aref_bump-hq", ".ps") ? 1 : 0;
    o.ssfx_hud_raindrops =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\deffer_base_hud_bump", ".ps") ? 1 : 0;
    o.ssfx_ssr = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_ssr", ".ps") ? 1 : 0;
    o.ssfx_terrain =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\deffer_terrain_high_flat_d", ".ps") ? 1 : 0;
    o.ssfx_volumetric =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_volumetric_blur", ".ps") ? 1 : 0;
    o.ssfx_water = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_water", ".ps") ? 1 : 0;
    o.ssfx_ao = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_ao", ".ps") ? 1 : 0;
    o.ssfx_il = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_il", ".ps") ? 1 : 0;
    o.ssfx_sss = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_sss", ".ps") ? 1 : 0;
    o.ssfx_bloom = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_bloom", ".ps") ? 1 : 0;
    o.ssfx_taa = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_taa", ".ps") ? 1 : 0;
    o.ssfx_fog = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_fog_scattering", ".ps") ? 1 : 0;
    o.ssfx_motionblur =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_motion_blur", ".ps") ? 1 : 0;
    o.ssfx_motionvectors =
        FS.exist(ssfx_fn, "$game_shaders$", "r3\\screenspace_mvectors", ".h") ? 1 : 0;
    o.ssfx_glass = FS.exist(ssfx_fn, "$game_shaders$", "r3\\ssfx_glass", ".ps") ? 1 : 0;

    // ... and only while SSS is switched on ( ssfx_enable as the game started with it, see
    // ssfx_console_vars.h ). The shaders are installed either way: every gamedata file SSS changes
    // holds both versions, and with the modules off here every shader compiles the game's own -
    // IE_SSFX_ENABLED, which picks the SSS version, follows ssfx_core ( shader_compile ).
    if (!ssfx_session_enable) {
        o.ssfx_core = 0;
        o.ssfx_rain = 0;
        o.ssfx_blood = 0;
        o.ssfx_branches = 0;
        o.ssfx_hud_raindrops = 0;
        o.ssfx_ssr = 0;
        o.ssfx_terrain = 0;
        o.ssfx_volumetric = 0;
        o.ssfx_water = 0;
        o.ssfx_ao = 0;
        o.ssfx_il = 0;
        o.ssfx_sss = 0;
        o.ssfx_bloom = 0;
        o.ssfx_taa = 0;
        o.ssfx_fog = 0;
        o.ssfx_motionblur = 0;
        o.ssfx_motionvectors = 0;
        o.ssfx_glass = 0;

        // The sun cascade sizes were set before this switch was known - by the constructor and by
        // the ssfx_shadow_cascades line of user.ltx, both through init_cacades(). With SSS off that
        // function keeps the game's own sizes; build them again now that it can tell.
        init_cacades();
    }

    // Screen Space Shaders exist only for the packed G-buffer. Monolith removed the unpacked path
    // outright - its whole source tree and both of its shader trees contain no gbuffer_opt and no
    // GBUFFER_OPTIMIZATION at all - which is why its gbuffer_stage.h declares
    // gbuffer_load_data( tc, pos2d, iSample ) unconditionally. This engine still carries the old
    // r3_gbuffer_opt switch, and with it off the donor's own rain_patch_normal.ps, combine_1.ps and
    // ssao_calc.ps find no such overload. A failed shader compile is CHECK_OR_EXIT, so that is not a
    // dimmer picture, it is the game closing. The switch stays for a build without the addon.
    if (o.ssfx_core && !o.dx10_gbuffer_opt) {
        o.dx10_gbuffer_opt = 1;
        Msg("~ r3_gbuffer_opt forced on: Screen Space Shaders require the packed G-buffer");
    }

    Msg("- SSS SWITCHED ON %i ( ssfx_enable )", ssfx_session_enable);
    Msg("- SSS CORE INSTALLED %i", o.ssfx_core);
    Msg("- SSS HUD SHADER INSTALLED %i", o.ssfx_hud_raindrops);
    Msg("- SSS MOTION VECTORS SHADER INSTALLED %i", o.ssfx_motionvectors);
    Msg("- SSS RAIN SHADER INSTALLED %i", o.ssfx_rain);
    Msg("- SSS BLOOD SHADER INSTALLED %i", o.ssfx_blood);
    Msg("- SSS BRANCHES SHADER INSTALLED %i", o.ssfx_branches);
    Msg("- SSS SSR SHADER INSTALLED %i", o.ssfx_ssr);
    Msg("- SSS TERRAIN SHADER INSTALLED %i", o.ssfx_terrain);
    Msg("- SSS VOLUMETRIC SHADER INSTALLED %i", o.ssfx_volumetric);
    Msg("- SSS WATER SHADER INSTALLED %i", o.ssfx_water);
    Msg("- SSS AO SHADER INSTALLED %i", o.ssfx_ao);
    Msg("- SSS IL SHADER INSTALLED %i", o.ssfx_il);
    Msg("- SSS SSS SHADER INSTALLED %i", o.ssfx_sss);
    Msg("- SSS BLOOM SHADER INSTALLED %i", o.ssfx_bloom);
    Msg("- SSS FOG SHADER INSTALLED %i", o.ssfx_fog);
    Msg("- SSS GLASS SHADER INSTALLED %i", o.ssfx_glass);
    Msg("- SSS MOTION BLUR SHADER INSTALLED %i", o.ssfx_motionblur);
    Msg("- SSS TAA SHADER INSTALLED %i", o.ssfx_taa);

    // constants
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("parallax", &binder_parallax);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("water_intensity",
                                                                      &binder_water_intensity);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("sun_shafts_intensity",
                                                                      &binder_sun_shafts_intensity);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("m_AlphaRef",
                                                                      &binder_alpha_ref);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "pos_decompression_params", &binder_pos_decompress_params);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "pos_decompression_params2", &binder_pos_decompress_params2);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "ie_pbr_hud_projection_params", &binder_ie_pbr_hud_projection_params);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "scope_lens_state", &binder_scope_lens_state);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "scope_lens_glass", &binder_scope_lens_glass);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "scope_lens_size", &binder_scope_lens_size);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup(
        "scope_lens_targets", &binder_scope_lens_targets);
    dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("triLOD", &binder_LOD);

    c_lmaterial = "L_material";
    c_sbase = "s_base";

    m_bMakeAsyncSS = false;

    Target = xr_new<CRenderTarget>(); // Main target

    Models = xr_new<CModelPool>();
    PSLibrary.OnCreate();
    HWOCC.occq_create(occq_size);

    rmNormal();
    marker = 0;
    D3D_QUERY_DESC qdesc;
    qdesc.MiscFlags = 0;
    qdesc.Query = D3D_QUERY_EVENT;
    std::memset(q_sync_point, 0, sizeof(q_sync_point));
    // R_CHK
    // (HW.pDevice->CreateQuery(&qdesc,&q_sync_point[0]));  R_CHK
    // (HW.pDevice->CreateQuery(&qdesc,&q_sync_point[1])); 	Prevent error on first get data
    // q_sync_point[0]->End();
    // q_sync_point[1]->End();
    // R_CHK
    // (HW.pDevice->CreateQuery(D3DQUERYTYPE_EVENT,&q_sync_point[0]));  R_CHK
    // (HW.pDevice->CreateQuery(D3DQUERYTYPE_EVENT,&q_sync_point[1]));
    for (u32 i = 0; i < HW.Caps.iGPUNum; ++i)
        R_CHK(HW.pDevice->CreateQuery(&qdesc, &q_sync_point[i]));
    HW.pContext->End(q_sync_point[0]);

    xrRender_apply_tf();
    ::PortalTraverser.initialize();
    FluidManager.Initialize(70, 70, 70);
    //	FluidManager.Initialize( 100, 100, 100 );
    FluidManager.SetScreenSize(Device.dwWidth, Device.dwHeight);
}

// Item 9 stage B ( multithreading ): how long building the shaders takes, measured before stage E
// changes anything about it. Every shader the game asks for ( shader_compile below ) is either read
// from the cache ( userdata\shaders_cache ) or compiled with D3DCompile and written there. Counted:
// how many of each and the time they take, the time inside D3DCompile alone, the failures, and the
// ten slowest compiles. Into the log when the renderer goes ( leaving the game ) and by the console
// command "r__shader_build_stats" ( xrRender_console.cpp ). Our own design, the donor has none.
namespace {
using shader_build_clock = std::chrono::steady_clock;

struct shader_build_slow {
    string128 name; // e.g. deffer_base_bump.ps
    double ms;      // inside D3DCompile
};

struct shader_build_data {
    u32 cached = 0, compiled = 0, failed = 0;
    double cached_ms = 0, compiled_ms = 0, compile_ms = 0;
    shader_build_slow slowest[10]; // slowest first
    u32 slowest_count = 0;
};
shader_build_data shader_build;
std::mutex shader_build_lock; // only the thread that builds shaders writes, but that may change

double shader_build_ms(const shader_build_clock::time_point& from) {
    return std::chrono::duration<double, std::milli>(shader_build_clock::now() - from).count();
}

// One shader: from the cache, or compiled ( compile_ms: the time inside D3DCompile ); total_ms is
// all of shader_compile for it.
void shader_build_note(LPCSTR name, LPCSTR target, bool cached, bool ok, double compile_ms,
                       double total_ms) {
    std::lock_guard<std::mutex> lock(shader_build_lock);
    shader_build_data& s = shader_build;
    if (cached) {
        s.cached++;
        s.cached_ms += total_ms;
        return;
    }
    if (!ok) {
        s.failed++;
        return;
    }
    s.compiled++;
    s.compiled_ms += total_ms;
    if (compile_ms < 0)
        return;
    s.compile_ms += compile_ms;
    u32 at = s.slowest_count;
    while (at && s.slowest[at - 1].ms < compile_ms)
        --at;
    if (at >= 10)
        return;
    for (u32 i = (s.slowest_count < 10 ? s.slowest_count : 9); i > at; --i)
        s.slowest[i] = s.slowest[i - 1];
    xr_sprintf(s.slowest[at].name, "%s.%.2s", name, target);
    s.slowest[at].ms = compile_ms;
    if (s.slowest_count < 10)
        s.slowest_count++;
}
} // namespace

void shader_build_stats_print() {
    std::lock_guard<std::mutex> lock(shader_build_lock);
    const shader_build_data& s = shader_build;
    Msg("* Shader build: %u compiled in %.2f s ( inside D3DCompile %.2f s ), %u read from the "
        "cache in %.2f s, %u failed",
        s.compiled, s.compiled_ms / 1000.0, s.compile_ms / 1000.0, s.cached, s.cached_ms / 1000.0,
        s.failed);
    for (u32 i = 0; i < s.slowest_count; ++i)
        Msg("* Shader build: slowest %u - %s, %.1f ms", i + 1, s.slowest[i].name, s.slowest[i].ms);
}

// Item 9 stage E ( multithreading plan: the shader build ). Two changes, both as the donor does
// them ( Monolith MT: r4.cpp shader_compile, xrCore\ShaderSourceCRC.cpp ), our own code:
//
// 1. Shaders are compiled by d3dcompiler_47.dll, about three times faster than the
//    d3dcompiler_43.dll the engine links ( the 273 shaders of a full cache with SSS: 47 s ->
//    14-15 s ). Only the compile call changes. Reading a compiled shader back stays on _43
//    ( D3DReflect, D3DGetInputSignatureBlob, D3DDisassemble in create_shader ): _47 refuses the
//    June 2010 interface id the engine passes to D3DReflect, and _43 reads the code of _47 exactly
//    as its own ( checked on all 273 ). The donor links _47 outright, which needs the newer SDK
//    headers; here it is loaded at run time - the game's bin first, then Windows - and when it is
//    missing ( an old Windows 7 ) the linked _43 compiles, as before.
//
// 2. Each file of the generated cache ( userdata\shaders_cache ) starts with a CRC of the shader's
//    source and of every file it includes, so a changed shader or include is compiled again by
//    itself. Before, the cache key held only the defines, and a stale file had to be deleted by
//    hand. Unlike the donor, each include is read and summed once per renderer, not again for
//    every shader that includes it ( 370 files instead of about 25 000 reads for the 273 ).
//
// Both are used by shader_compile only, on the main thread.
namespace {
pD3DCompile shader_compile_fn = nullptr;

pD3DCompile shader_compiler() {
    if (shader_compile_fn)
        return shader_compile_fn;
    HMODULE const dll = LoadLibraryA("d3dcompiler_47.dll"); // kept loaded, like a linked one
    if (dll)
        shader_compile_fn = (pD3DCompile)GetProcAddress(dll, "D3DCompile");
    if (shader_compile_fn) {
        string_path path = "";
        GetModuleFileNameA(dll, path, sizeof(path));
        Msg("* Shader compiler: %s", path);
    } else {
        shader_compile_fn = &D3DCompile;
        Msg("* Shader compiler: d3dcompiler_43.dll ( d3dcompiler_47.dll not found )");
    }
    return shader_compile_fn;
}

// Include name ( lower case, as written between the quotes ) -> CRC of the file and its includes.
xr_map<xr_string, u32> shader_include_crcs;

u32 shader_text_crc(const char* text, u32 size, u32 depth);

u32 shader_include_crc(const xr_string& include_name, u32 depth) {
    auto const found = shader_include_crcs.find(include_name);
    if (found != shader_include_crcs.end())
        return found->second;
    shader_include_crcs[include_name] = 0; // a file that includes itself back adds nothing
    // the two places the includer of the compiler looks in, in its order ( class includer below )
    string_path pname;
    strconcat(sizeof(pname), pname, ::Render->getShaderPath(), include_name.c_str());
    IReader* R = FS.r_open("$game_shaders$", pname);
    if (!R)
        R = FS.r_open("$game_shaders$", include_name.c_str());
    u32 crc = 0;
    if (R) {
        crc = shader_text_crc((const char*)R->pointer(), (u32)R->length(), depth + 1);
        FS.r_close(R);
    } else
        Msg("~ Shader source CRC: can't find include '%s', skipping it for cache validation",
            include_name.c_str());
    shader_include_crcs[include_name] = crc;
    return crc;
}

// The text itself, then the CRC of each included file in the order the lines name them. As the
// donor, every line that starts with '#' and holds "#include" counts, inside an #if or not: a
// header behind a switched-off #if that changes costs a needless rebuild at worst, never a stale
// shader.
u32 shader_text_crc(const char* text, u32 size, u32 depth) {
    u32 crc = crc32(text, size);
    if (depth > 32) // deeper than any real chain; only a broken tree gets here
        return crc;
    const char* p = text;
    const char* const end = text + size;
    while (p < end) {
        const char* line = p;
        while (p < end && *p != '\r' && *p != '\n')
            ++p;
        const char* const line_end = p;
        while (p < end && (*p == '\r' || *p == '\n'))
            ++p;
        while (line < line_end && (*line == ' ' || *line == '\t'))
            ++line;
        if (line == line_end || *line != '#')
            continue;
        const xr_string s(line, line_end);
        if (s.find("#include") == xr_string::npos)
            continue;
        size_t const q1 = s.find('"');
        if (q1 == xr_string::npos)
            continue;
        size_t const q2 = s.find('"', q1 + 1);
        if (q2 == xr_string::npos || q2 == q1 + 1)
            continue;
        xr_string include_name = s.substr(q1 + 1, q2 - q1 - 1);
        for (char& c : include_name)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        u32 const include_crc = shader_include_crc(include_name, depth);
        crc = crc32(&include_crc, (u32)sizeof(include_crc), crc);
    }
    return crc;
}

// For a shader source handed to shader_compile.
u32 shader_source_crc(const void* source, u32 size) {
    return shader_text_crc((const char*)source, size, 0);
}

void shader_source_crcs_reset() { shader_include_crcs.clear(); }
} // namespace

void CRender::destroy() {
    shader_build_stats_print(); // item 9 stage B: the shader build measurement into the log
    shader_source_crcs_reset(); // item 9 stage E: a new renderer reads the includes again
    m_bMakeAsyncSS = false;
    FluidManager.Destroy();
    ::PortalTraverser.destroy();
    //_RELEASE					(q_sync_point[1]);
    //_RELEASE					(q_sync_point[0]);
    for (u32 i = 0; i < HW.Caps.iGPUNum; ++i)
        _RELEASE(q_sync_point[i]);

    HWOCC.occq_destroy();
    xr_delete(Models);
    xr_delete(Target);
    PSLibrary.OnDestroy();
    Device.seqFrame.Remove(this);
    r_dsgraph_destroy();
}

void CRender::reset_begin() {
    // Update incremental shadowmap-visibility solver
    // BUG-ID: 10646
    {
        u32 it = 0;
        for (it = 0; it < Lights_LastFrame.size(); it++) {
            if (0 == Lights_LastFrame[it])
                continue;
            try {
                Lights_LastFrame[it]->svis.resetoccq();
            } catch (...) {
                Msg("! Failed to flush-OCCq on light [%d] %X", it, *(u32*)(&Lights_LastFrame[it]));
            }
        }
        Lights_LastFrame.clear();
    }

    xr_delete(Target);
    HWOCC.occq_destroy();
    //_RELEASE					(q_sync_point[1]);
    //_RELEASE					(q_sync_point[0]);
    for (u32 i = 0; i < HW.Caps.iGPUNum; ++i)
        _RELEASE(q_sync_point[i]);
}

void CRender::reset_end() {
    D3D_QUERY_DESC qdesc;
    qdesc.MiscFlags = 0;
    qdesc.Query = D3D_QUERY_EVENT;
    // R_CHK
    // (HW.pDevice->CreateQuery(&qdesc,&q_sync_point[0]));  R_CHK
    // (HW.pDevice->CreateQuery(&qdesc,&q_sync_point[1]));
    for (u32 i = 0; i < HW.Caps.iGPUNum; ++i)
        R_CHK(HW.pDevice->CreateQuery(&qdesc, &q_sync_point[i]));
    //	Prevent error on first get data
    HW.pContext->End(q_sync_point[0]);
    // q_sync_point[1]->End();
    // R_CHK
    // (HW.pDevice->CreateQuery(D3DQUERYTYPE_EVENT,&q_sync_point[0]));  R_CHK
    // (HW.pDevice->CreateQuery(D3DQUERYTYPE_EVENT,&q_sync_point[1]));
    HWOCC.occq_create(occq_size);

    Target = xr_new<CRenderTarget>();

    xrRender_apply_tf();
    FluidManager.SetScreenSize(Device.dwWidth, Device.dwHeight);

    // Set this flag true to skip the first render frame,
    // that some data is not ready in the first frame (for example device camera position)
    m_bFirstFrameAfterReset = true;
}
/*
void CRender::OnFrame()
{
        Models->DeleteQueue			();
        if (ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))	{
                Device.seqParallel.insert	(Device.seqParallel.begin(),
                        fastdelegate::FastDelegate0<>(&HOM,&CHOM::MT_RENDER));
        }
}*/
void CRender::OnFrame() {
    Models->DeleteQueue();
    if (ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC)) {
        // MT-details (@front)
        Device.seqParallel.insert(Device.seqParallel.begin(),
                                  fastdelegate::FastDelegate0<>(Details, &CDetailManager::MT_CALC));

        // MT-HOM (@front)
        Device.seqParallel.insert(Device.seqParallel.begin(),
                                  fastdelegate::FastDelegate0<>(&HOM, &CHOM::MT_RENDER));
    }

    // SSFX interactive grass: advance the bender animations once per frame. Donor r4.cpp:677-678.
    if (Details)
        g_pGamePersistent->GrassBendersUpdateAnimations();
}

// Implementation
IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent) { return xr_new<CROS_impl>(); }
void CRender::ros_destroy(IRender_ObjectSpecific*& p) { xr_delete(p); }
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data) {
    return Models->Create(name, data);
}
IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data) {
    return Models->CreateChild(name, data);
}
IRenderVisual* CRender::model_Duplicate(IRenderVisual* V) {
    return Models->Instance_Duplicate((dxRender_Visual*)V);
}
void CRender::model_Delete(IRenderVisual*& V, BOOL bDiscard) {
    dxRender_Visual* pVisual = (dxRender_Visual*)V;
    Models->Delete(pVisual, bDiscard);
    V = 0;
}
IRender_DetailModel* CRender::model_CreateDM(IReader* F) {
    CDetail* D = xr_new<CDetail>();
    D->Load(F);
    return D;
}
void CRender::model_Delete(IRender_DetailModel*& F) {
    if (F) {
        CDetail* D = (CDetail*)F;
        D->Unload();
        xr_delete(D);
        F = NULL;
    }
}
IRenderVisual* CRender::model_CreatePE(LPCSTR name) {
    PS::CPEDef* SE = PSLibrary.FindPED(name);
    R_ASSERT3(SE, "Particle effect doesn't exist", name);
    return Models->CreatePE(SE);
}
IRenderVisual* CRender::model_CreateParticles(LPCSTR name) {
    PS::CPEDef* SE = PSLibrary.FindPED(name);
    if (SE)
        return Models->CreatePE(SE);
    else {
        PS::CPGDef* SG = PSLibrary.FindPGD(name);
        R_ASSERT3(SG, "Particle effect or group doesn't exist", name);
        return Models->CreatePG(SG);
    }
}
void CRender::models_Prefetch() { Models->Prefetch(); }
void CRender::models_Clear(BOOL b_complete) { Models->ClearPool(b_complete); }

ref_shader CRender::getShader(int id) {
    VERIFY(id < int(Shaders.size()));
    return Shaders[id];
}
IRender_Portal* CRender::getPortal(int id) {
    VERIFY(id < int(Portals.size()));
    return Portals[id];
}
IRender_Sector* CRender::getSector(int id) {
    VERIFY(id < int(Sectors.size()));
    return Sectors[id];
}
IRender_Sector* CRender::getSectorActive() { return pLastSector; }
IRenderVisual* CRender::getVisual(int id) {
    VERIFY(id < int(Visuals.size()));
    return Visuals[id];
}
D3DVERTEXELEMENT9* CRender::getVB_Format(int id, BOOL _alt) {
    if (_alt) {
        VERIFY(id < int(xDC.size()));
        return xDC[id].begin();
    } else {
        VERIFY(id < int(nDC.size()));
        return nDC[id].begin();
    }
}
ID3DVertexBuffer* CRender::getVB(int id, BOOL _alt) {
    if (_alt) {
        VERIFY(id < int(xVB.size()));
        return xVB[id];
    } else {
        VERIFY(id < int(nVB.size()));
        return nVB[id];
    }
}
ID3DIndexBuffer* CRender::getIB(int id, BOOL _alt) {
    if (_alt) {
        VERIFY(id < int(xIB.size()));
        return xIB[id];
    } else {
        VERIFY(id < int(nIB.size()));
        return nIB[id];
    }
}
FSlideWindowItem* CRender::getSWI(int id) {
    VERIFY(id < int(SWIs.size()));
    return &SWIs[id];
}
IRender_Target* CRender::getTarget() { return Target; }

IRender_Light* CRender::light_create() { return Lights.Create(); }
IRender_Glow* CRender::glow_create() { return xr_new<CGlow>(); }

void CRender::flush() { r_dsgraph_render_graph(0); }

BOOL CRender::occ_visible(vis_data& P) { return HOM.visible(P); }
BOOL CRender::occ_visible(sPoly& P) { return HOM.visible(P); }
BOOL CRender::occ_visible(Fbox& P) { return HOM.visible(P); }

void CRender::add_Visual(IRenderVisual* V) { add_leafs_Dynamic((dxRender_Visual*)V); }
void CRender::add_Geometry(IRenderVisual* V) { add_Static((dxRender_Visual*)V, View->getMask()); }
void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T,
                                 Fvector* verts) {
    if (T->suppress_wm)
        return;
    VERIFY2(xr::valid(P) && xr::valid(s) && T && verts && (s > EPS_L), "Invalid static wallmark params");
    Wallmarks->AddStaticWallmark(T, verts, P, &*S, s);
}

void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T,
                                 Fvector* V) {
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader)
        add_StaticWallmark(*pShader, P, s, T, V);
}

void CRender::add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T,
                                 Fvector* V) {
    dxUIShader* pShader = (dxUIShader*)&*S;
    add_StaticWallmark(pShader->hShader, P, s, T, V);
}

void CRender::clear_static_wallmarks() { Wallmarks->clear(); }

void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm) {
    Wallmarks->AddSkeletonWallmark(wm);
}
void CRender::add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh,
                                   const Fvector& start, const Fvector& dir, float size) {
    Wallmarks->AddSkeletonWallmark(xf, obj, sh, start, dir, size);
}
void CRender::add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray,
                                   const Fvector& start, const Fvector& dir, float size) {
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader)
        add_SkeletonWallmark(xf, (CKinematics*)obj, *pShader, start, dir, size);
}
void CRender::add_Occluder(Fbox2& bb_screenspace) { HOM.occlude(bb_screenspace); }
void CRender::set_Object(IRenderable* O) { val_pObject = O; }
void CRender::rmNear() {
    IRender_Target* T = getTarget();
    D3D_VIEWPORT VP = { 0, 0, (float)T->get_width(), (float)T->get_height(), 0, 0.02f };

    HW.pContext->RSSetViewports(1, &VP);
    // CHK_DX				(HW.pDevice->SetViewport(&VP));
}
void CRender::rmFar() {
    IRender_Target* T = getTarget();
    D3D_VIEWPORT VP = { 0, 0, (float)T->get_width(), (float)T->get_height(), 0.99999f, 1.f };

    HW.pContext->RSSetViewports(1, &VP);
    // CHK_DX				(HW.pDevice->SetViewport(&VP));
}
void CRender::rmNormal() {
    IRender_Target* T = getTarget();
    D3D_VIEWPORT VP = { 0, 0, (float)T->get_width(), (float)T->get_height(), 0, 1.f };

    HW.pContext->RSSetViewports(1, &VP);
    // CHK_DX				(HW.pDevice->SetViewport(&VP));
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CRender::CRender() : m_bFirstFrameAfterReset(false) { init_cacades(); }

CRender::~CRender() {}

#include "xrEngine/GameFont.h"
void CRender::Statistics(CGameFont* _F) {
    CGameFont& F = *_F;
    F.OutNext(" **** LT:%2d,LV:%2d **** ", stats.l_total, stats.l_visible);
    stats.l_visible = 0;
    F.OutNext("    S(%2d)   | (%2d)NS   ", stats.l_shadowed, stats.l_unshadowed);
    F.OutNext("smap use[%2d], merge[%2d], finalclip[%2d]", stats.s_used,
              stats.s_merged - stats.s_used, stats.s_finalclip);
    stats.s_used = 0;
    stats.s_merged = 0;
    stats.s_finalclip = 0;
    F.OutSkip();
    F.OutNext(" **** Occ-Q(%03.1f) **** ",
              100.f * f32(stats.o_culled) / f32(stats.o_queries ? stats.o_queries : 1));
    F.OutNext(" total  : %2d", stats.o_queries);
    stats.o_queries = 0;
    F.OutNext(" culled : %2d", stats.o_culled);
    stats.o_culled = 0;
    F.OutSkip();
    u32 ict = stats.ic_total + stats.ic_culled;
    F.OutNext(" **** iCULL(%03.1f) **** ", 100.f * f32(stats.ic_culled) / f32(ict ? ict : 1));
    F.OutNext(" visible: %2d", stats.ic_total);
    stats.ic_total = 0;
    F.OutNext(" culled : %2d", stats.ic_culled);
    stats.ic_culled = 0;
#ifdef DEBUG
    HOM.stats();
#endif
}

/*
extern "C"
{
LPCSTR WINAPI	D3DXGetPixelShaderProfile	(LPDIRECT3DDEVICE9  pDevice);
LPCSTR WINAPI	D3DXGetVertexShaderProfile	(LPDIRECT3DDEVICE9	pDevice);
};
*/

void CRender::addShaderOption(const char* name, const char* value) {
    D3D_SHADER_MACRO macro = { name, value };
    m_ShaderOptions.push_back(macro);
}

template <typename T>
static HRESULT create_shader(LPCSTR const pTarget, DWORD const* buffer, u32 const buffer_size,
                             LPCSTR const file_name, T*& result, bool const disasm) {
    result->sh = ShaderTypeTraits<T>::CreateHWShader(buffer, buffer_size);

    ID3DShaderReflection* pReflection = 0;

    HRESULT const _hr =
        D3DReflect(buffer, buffer_size, IID_ID3DShaderReflection, (void**)&pReflection);
    if (SUCCEEDED(_hr) && pReflection) {
        // Parse constant table data
        result->constants.parse(pReflection, ShaderTypeTraits<T>::GetShaderDest());

        _RELEASE(pReflection);
    } else {
        Msg("! D3DReflectShader %s hr == 0x%08x", file_name, _hr);
    }

    return _hr;
}

static HRESULT create_shader(LPCSTR const pTarget, DWORD const* buffer, u32 const buffer_size,
                             LPCSTR const file_name, void*& result, bool const disasm) {
    HRESULT _result = E_FAIL;
    if (pTarget[0] == 'p') {
        SPS* sps_result = (SPS*)result;
#ifdef USE_DX11
        _result = HW.pDevice->CreatePixelShader(buffer, buffer_size, 0, &sps_result->ps);
#else  // #ifdef USE_DX11
        _result = HW.pDevice->CreatePixelShader(buffer, buffer_size, &sps_result->ps);
#endif // #ifdef USE_DX11
        if (!SUCCEEDED(_result)) {
            Log("! PS: ", file_name);
            Msg("! CreatePixelShader hr == 0x%08x", _result);
            return E_FAIL;
        }

        ID3DShaderReflection* pReflection = 0;

#ifdef USE_DX11
        _result = D3DReflect(buffer, buffer_size, IID_ID3DShaderReflection, (void**)&pReflection);
#else
        _result = D3D10ReflectShader(buffer, buffer_size, &pReflection);
#endif

        //	Parse constant, texture, sampler binding
        //	Store input signature blob
        if (SUCCEEDED(_result) && pReflection) {
            //	Let constant table parse it's data
            sps_result->constants.parse(pReflection, RC_dest_pixel);

            _RELEASE(pReflection);
        } else {
            Log("! PS: ", file_name);
            Msg("! D3DReflectShader hr == 0x%08x", _result);
        }
    } else if (pTarget[0] == 'v') {
        SVS* svs_result = (SVS*)result;
#ifdef USE_DX11
        _result = HW.pDevice->CreateVertexShader(buffer, buffer_size, 0, &svs_result->vs);
#else  // #ifdef USE_DX11
        _result = HW.pDevice->CreateVertexShader(buffer, buffer_size, &svs_result->vs);
#endif // #ifdef USE_DX11

        if (!SUCCEEDED(_result)) {
            Log("! VS: ", file_name);
            Msg("! CreatePixelShader hr == 0x%08x", _result);
            return E_FAIL;
        }

        ID3DShaderReflection* pReflection = 0;
#ifdef USE_DX11
        _result = D3DReflect(buffer, buffer_size, IID_ID3DShaderReflection, (void**)&pReflection);
#else
        _result = D3D10ReflectShader(buffer, buffer_size, &pReflection);
#endif

        //	Parse constant, texture, sampler binding
        //	Store input signature blob
        if (SUCCEEDED(_result) && pReflection) {
            //	TODO: DX10: share the same input signatures

            //	Store input signature (need only for VS)
            // CHK_DX( D3DxxGetInputSignatureBlob(pShaderBuf->GetBufferPointer(),
            // pShaderBuf->GetBufferSize(), &_vs->signature) );
            ID3DBlob* pSignatureBlob;
            CHK_DX(D3DGetInputSignatureBlob(buffer, buffer_size, &pSignatureBlob));
            VERIFY(pSignatureBlob);

            svs_result->signature =
                dxRenderDeviceRender::Instance().Resources->_CreateInputSignature(pSignatureBlob);

            _RELEASE(pSignatureBlob);

            //	Let constant table parse it's data
            svs_result->constants.parse(pReflection, RC_dest_vertex);

            _RELEASE(pReflection);
        } else {
            Log("! VS: ", file_name);
            Msg("! D3DXFindShaderComment hr == 0x%08x", _result);
        }
    } else if (pTarget[0] == 'g') {
        SGS* sgs_result = (SGS*)result;
#ifdef USE_DX11
        _result = HW.pDevice->CreateGeometryShader(buffer, buffer_size, 0, &sgs_result->gs);
#else  // #ifdef USE_DX11
        _result = HW.pDevice->CreateGeometryShader(buffer, buffer_size, &sgs_result->gs);
#endif // #ifdef USE_DX11
        if (!SUCCEEDED(_result)) {
            Log("! GS: ", file_name);
            Msg("! CreateGeometryShaderhr == 0x%08x", _result);
            return E_FAIL;
        }

        ID3DShaderReflection* pReflection = 0;

#ifdef USE_DX11
        _result = D3DReflect(buffer, buffer_size, IID_ID3DShaderReflection, (void**)&pReflection);
#else
        _result = D3D10ReflectShader(buffer, buffer_size, &pReflection);
#endif

        //	Parse constant, texture, sampler binding
        //	Store input signature blob
        if (SUCCEEDED(_result) && pReflection) {
            //	Let constant table parse it's data
            sgs_result->constants.parse(pReflection, RC_dest_geometry);

            _RELEASE(pReflection);
        } else {
            Log("! PS: ", file_name);
            Msg("! D3DReflectShader hr == 0x%08x", _result);
        }
    }
    //	else if (pTarget[0] == 'c') {
    //		SCS* scs_result = (SCS*)result;
    //#ifdef USE_DX11
    //		_result			= HW.pDevice->CreateComputeShader(buffer, buffer_size, 0,
    //&scs_result->sh); #else // #ifdef USE_DX11 		_result			=
    //HW.pDevice->CreateComputeShader(buffer, buffer_size, &scs_result->sh); #endif // #ifdef
    //USE_DX11 		if ( !SUCCEEDED(_result) ) {
    //			Log			("! CS: ", file_name);
    //			Msg			("! CreateComputeShaderhr == 0x%08x", _result);
    //			return		E_FAIL;
    //		}
    //
    //		ID3DShaderReflection *pReflection = 0;
    //
    //#ifdef USE_DX11
    //		_result			= D3DReflect( buffer, buffer_size, IID_ID3DShaderReflection,
    //(void**)&pReflection); #else
    //		_result			= D3D10ReflectShader( buffer, buffer_size, &pReflection);
    //#endif
    //
    //		//	Parse constant, texture, sampler binding
    //		//	Store input signature blob
    //		if (SUCCEEDED(_result) && pReflection)
    //		{
    //			//	Let constant table parse it's data
    //			scs_result->constants.parse(pReflection,RC_dest_pixel);
    //
    //			_RELEASE(pReflection);
    //		}
    //		else
    //		{
    //			Log	("! PS: ", file_name);
    //			Msg	("! D3DReflectShader hr == 0x%08x", _result);
    //		}
    //	}
    else if (pTarget[0] == 'c') {
        _result = create_shader(pTarget, buffer, buffer_size, file_name, (SCS*&)result, disasm);
    } else if (pTarget[0] == 'h') {
        _result = create_shader(pTarget, buffer, buffer_size, file_name, (SHS*&)result, disasm);
    } else if (pTarget[0] == 'd') {
        _result = create_shader(pTarget, buffer, buffer_size, file_name, (SDS*&)result, disasm);
    } else {
        NODEFAULT;
    }

    if (disasm) {
        ID3DBlob* disasm = 0;
        D3DDisassemble(buffer, buffer_size, FALSE, 0, &disasm);
        // D3DXDisassembleShader		(LPDWORD(code->GetBufferPointer()), FALSE, 0, &disasm
        // );
        string_path dname;
        strconcat(sizeof(dname), dname, "disasm\\", file_name,
                  ('v' == pTarget[0]) ? ".vs" : ('p' == pTarget[0]) ? ".ps" : ".gs");
        IWriter* W = FS.w_open("$logs$", dname);
        W->w(disasm->GetBufferPointer(), (u32)disasm->GetBufferSize());
        FS.w_close(W);
        _RELEASE(disasm);
    }

    return _result;
}

//--------------------------------------------------------------------------------------------------------------
class includer : public ID3DInclude {
public:
    HRESULT __stdcall Open(D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData,
                           LPCVOID* ppData, UINT* pBytes) {
        string_path pname;
        strconcat(sizeof(pname), pname, ::Render->getShaderPath(), pFileName);
        IReader* R = FS.r_open("$game_shaders$", pname);
        if (0 == R) {
            // possibly in shared directory or somewhere else - open directly
            R = FS.r_open("$game_shaders$", pFileName);
            if (0 == R)
                return E_FAIL;
        }

        // duplicate and zero-terminate
        u32 size = R->length();
        u8* data = xr_alloc<u8>(size + 1);
        std::memcpy(data, R->pointer(), size);
        data[size] = 0;
        FS.r_close(R);

        *ppData = data;
        *pBytes = size;
        return D3D_OK;
    }
    HRESULT __stdcall Close(LPCVOID pData) {
        xr_free(pData);
        return D3D_OK;
    }
};

static inline bool match_shader_id(LPCSTR const debug_shader_id, LPCSTR const full_shader_id,
                                   FS_FileSet const& file_set, string_path& result);

HRESULT CRender::shader_compile(LPCSTR name, DWORD const* pSrcData, UINT SrcDataLen,
                                LPCSTR pFunctionName, LPCSTR pTarget, DWORD Flags, void*& result) {
    // item 9 stage B: the shader build measurement ( shader_build_note at the end )
    const shader_build_clock::time_point build_start = shader_build_clock::now();
    D3D_SHADER_MACRO defines[128];
    int def_it = 0;
    char c_smapsize[32];
    char c_sun_shafts[32];
    char c_ssao[32];
    char c_sun_quality[32];
    char c_infernis_pbr[2];
    char c_infernis_sslr[2];
    // Screen Space Shaders preprocessor values
    char c_ssfx_sss_dir_quality[32];
    char c_ssfx_sss_omni_quality[32];
    char c_ssfx_terrain_pom_refine[32];
    char c_ssfx_pom_refine[32];
    char c_ssfx_il[32];
    char c_ssfx_ao[32];
    char c_ssfx_water[32];
    char c_ssfx_water_parallax[32];
    char c_ssr_quality[32];
    char c_rain_quality[32];
    char c_inter_grass[32];
    char c_ssfx_es_burley[2];
    char c_ssfx_es_ggx[2];

    char sh_name[MAX_PATH] = "";

    for (u32 i = 0; i < m_ShaderOptions.size(); ++i) {
        defines[def_it++] = m_ShaderOptions[i];
    }

    u32 len = xr_strlen(sh_name);

    const bool infernisPbrEnabled = infernis_pbr_rendering_enabled();
    const u32 infernisSslrMode = infernis_pbr_sslr_mode();
    c_infernis_pbr[0] = infernisPbrEnabled ? '1' : '0';
    c_infernis_pbr[1] = 0;
    c_infernis_sslr[0] = '0' + char(infernisSslrMode);
    c_infernis_sslr[1] = 0;

    defines[def_it++] = { "IE_PBR_RENDERING_ENABLED", c_infernis_pbr };
    defines[def_it++] = { "IE_PBR_SSLR_MODE", c_infernis_sslr };
    sh_name[len++] = c_infernis_pbr[0];
    sh_name[len++] = c_infernis_sslr[0];
    // options
    {
        xr_sprintf(c_smapsize, "%04d", u32(o.smapsize));
        defines[def_it].Name = "SMAP_size";
        defines[def_it].Definition = c_smapsize;
        def_it++;
        VERIFY(xr_strlen(c_smapsize) == 4);
        xr_strcat(sh_name, c_smapsize);
        len += 4;
    }

    if (o.fp16_filter) {
        defines[def_it].Name = "FP16_FILTER";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.fp16_filter);
    ++len;

    if (o.fp16_blend) {
        defines[def_it].Name = "FP16_BLEND";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.fp16_blend);
    ++len;

    if (o.HW_smap) {
        defines[def_it].Name = "USE_HWSMAP";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.HW_smap);
    ++len;

    if (o.HW_smap_PCF) {
        defines[def_it].Name = "USE_HWSMAP_PCF";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.HW_smap_PCF);
    ++len;

    if (o.HW_smap_FETCH4) {
        defines[def_it].Name = "USE_FETCH4";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.HW_smap_FETCH4);
    ++len;

    if (o.sjitter) {
        defines[def_it].Name = "USE_SJITTER";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.sjitter);
    ++len;

    if (HW.Caps.raster_major >= 3) {
        defines[def_it].Name = "USE_BRANCHING";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(HW.Caps.raster_major >= 3);
    ++len;

    if (HW.Caps.geometry.bVTF) {
        defines[def_it].Name = "USE_VTF";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(HW.Caps.geometry.bVTF);
    ++len;

    if (o.Tshadows) {
        defines[def_it].Name = "USE_TSHADOWS";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.Tshadows);
    ++len;

    if (o.mblur) {
        defines[def_it].Name = "USE_MBLUR";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.mblur);
    ++len;

    if (o.sunfilter) {
        defines[def_it].Name = "USE_SUNFILTER";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.sunfilter);
    ++len;

    if (o.sunstatic) {
        defines[def_it].Name = "USE_R2_STATIC_SUN";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.sunstatic);
    ++len;

    if (o.forceskinw) {
        defines[def_it].Name = "SKIN_COLOR";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.forceskinw);
    ++len;

    if (o.ssao_blur_on) {
        defines[def_it].Name = "USE_SSAO_BLUR";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.ssao_blur_on);
    ++len;

    if (o.ssao_hdao) {
        defines[def_it].Name = "HDAO";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0' + char(o.ssao_hbao);
        ++len;
        sh_name[len] = '0' + char(o.ssao_half_data);
        ++len;
        if (o.ssao_hbao) {
            defines[def_it].Name = "SSAO_OPT_DATA";
            if (o.ssao_half_data) {
                defines[def_it].Definition = "2";
            } else {
                defines[def_it].Definition = "1";
            }
            def_it++;

            if (o.hbao_vectorized) {
                defines[def_it].Name = "VECTORIZED_CODE";
                defines[def_it].Definition = "1";
                def_it++;
            }

            defines[def_it].Name = "USE_HBAO";
            defines[def_it].Definition = "1";
            def_it++;
        }
    }

    if (o.dx10_msaa) {
        static char def[256];
        // if( m_MSAASample < 0 )
        //{
        def[0] = '0';
        //	sh_name[len]='0'; ++len;
        //}
        // else
        //{
        //	def[0]= '0' + char(m_MSAASample);
        //	sh_name[len]='0' + char(m_MSAASample); ++len;
        //}
        def[1] = 0;
        defines[def_it].Name = "ISAMPLE";
        defines[def_it].Definition = def;
        def_it++;
        sh_name[len] = '0';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    // skinning
    if (m_skinning < 0) {
        defines[def_it].Name = "SKIN_NONE";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (0 == m_skinning) {
        defines[def_it].Name = "SKIN_0";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(0 == m_skinning);
    ++len;

    if (1 == m_skinning) {
        defines[def_it].Name = "SKIN_1";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(1 == m_skinning);
    ++len;

    if (2 == m_skinning) {
        defines[def_it].Name = "SKIN_2";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(2 == m_skinning);
    ++len;

    if (3 == m_skinning) {
        defines[def_it].Name = "SKIN_3";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(3 == m_skinning);
    ++len;

    if (4 == m_skinning) {
        defines[def_it].Name = "SKIN_4";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(4 == m_skinning);
    ++len;

    //	Igor: need restart options
    if (RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_SOFT_WATER)) {
        defines[def_it].Name = "USE_SOFT_WATER";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_SOFT_PARTICLES)) {
        defines[def_it].Name = "USE_SOFT_PARTICLES";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_DOF)) {
        defines[def_it].Name = "USE_DOF";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r_sun_shafts) {
        xr_sprintf(c_sun_shafts, "%d", ps_r_sun_shafts);
        defines[def_it].Name = "SUN_SHAFTS_QUALITY";
        defines[def_it].Definition = c_sun_shafts;
        def_it++;
        sh_name[len] = '0' + char(ps_r_sun_shafts);
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r_ssao) {
        xr_sprintf(c_ssao, "%d", ps_r_ssao);
        defines[def_it].Name = "SSAO_QUALITY";
        defines[def_it].Definition = c_ssao;
        def_it++;
        sh_name[len] = '0' + char(ps_r_ssao);
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r_sun_quality) {
        xr_sprintf(c_sun_quality, "%d", ps_r_sun_quality);
        defines[def_it].Name = "SUN_QUALITY";
        defines[def_it].Definition = c_sun_quality;
        def_it++;
        sh_name[len] = '0' + char(ps_r_sun_quality);
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_STEEP_PARALLAX)) {
        defines[def_it].Name = "ALLOW_STEEPPARALLAX";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    } else {
        sh_name[len] = '0';
        ++len;
    }

    if (o.dx10_gbuffer_opt) {
        defines[def_it].Name = "GBUFFER_OPTIMIZATION";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.dx10_gbuffer_opt);
    ++len;

    // R_ASSERT						( !o.dx10_sm4_1 );
    if (o.dx10_sm4_1) {
        defines[def_it].Name = "SM_4_1";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + 0 * char(o.dx10_sm4_1);
    ++len;

    R_ASSERT(HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0);
    if (HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0) {
        defines[def_it].Name = "SM_5";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0);
    ++len;

    if (o.dx10_minmax_sm) {
        defines[def_it].Name = "USE_MINMAX_SM";
        defines[def_it].Definition = "1";
        def_it++;
    }
    sh_name[len] = '0' + char(o.dx10_minmax_sm != 0);
    ++len;

    // Be carefull!!!!! this should be at the end to correctly generate
    // compiled shader name;
    // add a #define for DX10_1 MSAA support
    if (o.dx10_msaa) {
        defines[def_it].Name = "USE_MSAA";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;

        static char samples[2];

        defines[def_it].Name = "MSAA_SAMPLES";
        samples[0] = char(o.dx10_msaa_samples) + '0';
        samples[1] = 0;
        defines[def_it].Definition = samples;
        def_it++;
        sh_name[len] = '0' + char(o.dx10_msaa_samples);
        ++len;

        if (o.dx10_msaa_opt) {
            defines[def_it].Name = "MSAA_OPTIMIZATION";
            defines[def_it].Definition = "1";
            def_it++;
        }
        sh_name[len] = '0' + char(o.dx10_msaa_opt);
        ++len;

        switch (o.dx10_msaa_alphatest) {
        case MSAA_ATEST_DX10_0_ATOC:
            defines[def_it].Name = "MSAA_ALPHATEST_DX10_0_ATOC";
            defines[def_it].Definition = "1";
            def_it++;
            sh_name[len] = '1';
            ++len;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
            break;
        case MSAA_ATEST_DX10_1_ATOC:
            defines[def_it].Name = "MSAA_ALPHATEST_DX10_1_ATOC";
            defines[def_it].Definition = "1";
            def_it++;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '1';
            ++len;
            sh_name[len] = '0';
            ++len;
            break;
        case MSAA_ATEST_DX10_1_NATIVE:
            defines[def_it].Name = "MSAA_ALPHATEST_DX10_1";
            defines[def_it].Definition = "1";
            def_it++;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '1';
            ++len;
            break;
        default:
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
        }
    } else {
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
        sh_name[len] = '0';
        ++len;
    }

    // Screen Space Shaders ( Monolith )
    //
    // sh_name is the key the compiled shaders are cached under, so every entry below writes a
    // fixed number of characters. Values that can have two digits always take two characters in
    // the key, while the define gets the plain number: HLSL reads a leading zero as an octal
    // literal. Module switches ( SSFX_AO, SSFX_SSR, SSFX_WIND, ... ) are not set here: every
    // installed module defines its own in its check_screenspace_*.h, as in Monolith.
    {
        // Every gamedata file SSS changes holds both versions under the one name: the SSS one
        // where IE_SSFX_ENABLED is defined, the game's own otherwise. The shader cache keys a
        // compiled shader by its name and this string only - never by its source. Without this
        // char, "SSS on" and "SSS off" would build identical keys from different sources, and one
        // would load the other's cached bytecode.
        sh_name[len] = '0' + char(o.ssfx_core);
        ++len;
        if (o.ssfx_core) {
            defines[def_it].Name = "IE_SSFX_ENABLED";
            defines[def_it].Definition = "1";
            def_it++;
        }

        if (ps_ssfx_rain_1.w > 0) {
            xr_sprintf(c_rain_quality, "%d", u8(ps_ssfx_rain_1.w));
            defines[def_it].Name = "SSFX_RAIN_QUALITY";
            defines[def_it].Definition = c_rain_quality;
            def_it++;
            sh_name[len] = '1';
            ++len;
            xr_strcat(sh_name, c_rain_quality);
            len += xr_strlen(c_rain_quality);
        } else {
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
        }

        if (ps_ssfx_grass_interactive.y > 0) {
            const u8 inter_grass = u8(ps_ssfx_grass_interactive.y);
            xr_sprintf(c_inter_grass, "%d", inter_grass);
            defines[def_it].Name = "SSFX_INT_GRASS";
            defines[def_it].Definition = c_inter_grass;
            def_it++;
            sh_name[len] = '1';
            ++len;
            sh_name[len] = '0' + char(inter_grass / 10);
            ++len;
            sh_name[len] = '0' + char(inter_grass % 10);
            ++len;
        } else {
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
            sh_name[len] = '0';
            ++len;
        }

        xr_sprintf(c_ssr_quality, "%d", u8(std::min(std::max(ps_ssfx_ssr_quality, 0), 5)));
        defines[def_it].Name = "SSFX_SSR_QUALITY";
        defines[def_it].Definition = c_ssr_quality;
        def_it++;
        xr_strcat(sh_name, c_ssr_quality);
        len += xr_strlen(c_ssr_quality);

        xr_sprintf(c_ssfx_water, "%d",
                   u8(std::min(std::max(ps_ssfx_water_quality.x, 0.0f), 4.0f)));
        defines[def_it].Name = "SSFX_WATER_QUALITY";
        defines[def_it].Definition = c_ssfx_water;
        def_it++;
        xr_strcat(sh_name, c_ssfx_water);
        len += xr_strlen(c_ssfx_water);

        xr_sprintf(c_ssfx_water_parallax, "%d",
                   u8(std::min(std::max(ps_ssfx_water_quality.y, 0.0f), 3.0f)));
        defines[def_it].Name = "SSFX_WATER_PARALLAX";
        defines[def_it].Definition = c_ssfx_water_parallax;
        def_it++;
        xr_strcat(sh_name, c_ssfx_water_parallax);
        len += xr_strlen(c_ssfx_water_parallax);

        const u8 il_quality = u8(std::min(std::max(ps_ssfx_il_quality, 0), 64));
        xr_sprintf(c_ssfx_il, "%d", il_quality);
        defines[def_it].Name = "SSFX_IL_QUALITY";
        defines[def_it].Definition = c_ssfx_il;
        def_it++;
        sh_name[len] = '0' + char(il_quality / 10);
        ++len;
        sh_name[len] = '0' + char(il_quality % 10);
        ++len;

        xr_sprintf(c_ssfx_ao, "%d", u8(std::min(std::max(ps_ssfx_ao_quality, 2), 8)));
        defines[def_it].Name = "SSFX_AO_QUALITY";
        defines[def_it].Definition = c_ssfx_ao;
        def_it++;
        xr_strcat(sh_name, c_ssfx_ao);
        len += xr_strlen(c_ssfx_ao);

        xr_sprintf(c_ssfx_pom_refine, "%d", u8(std::min(std::max(ps_ssfx_pom_refine, 0), 1)));
        defines[def_it].Name = "SSFX_POM_REFINE";
        defines[def_it].Definition = c_ssfx_pom_refine;
        def_it++;
        xr_strcat(sh_name, c_ssfx_pom_refine);
        len += xr_strlen(c_ssfx_pom_refine);

        xr_sprintf(c_ssfx_terrain_pom_refine, "%d",
                   u8(std::min(std::max(ps_ssfx_terrain_pom_refine, 0), 1)));
        defines[def_it].Name = "SSFX_TERRA_POM_REFINE";
        defines[def_it].Definition = c_ssfx_terrain_pom_refine;
        def_it++;
        xr_strcat(sh_name, c_ssfx_terrain_pom_refine);
        len += xr_strlen(c_ssfx_terrain_pom_refine);

        const u8 sss_dir_quality = u8(std::min(std::max((int)ps_ssfx_sss_quality.x, 1), 24));
        xr_sprintf(c_ssfx_sss_dir_quality, "%d", sss_dir_quality);
        defines[def_it].Name = "SSFX_SSS_DIR_QUALITY";
        defines[def_it].Definition = c_ssfx_sss_dir_quality;
        def_it++;
        sh_name[len] = '0' + char(sss_dir_quality / 10);
        ++len;
        sh_name[len] = '0' + char(sss_dir_quality % 10);
        ++len;

        const u8 sss_omni_quality = u8(std::min(std::max((int)ps_ssfx_sss_quality.y, 1), 12));
        xr_sprintf(c_ssfx_sss_omni_quality, "%d", sss_omni_quality);
        defines[def_it].Name = "SSFX_SSS_OMNI_QUALITY";
        defines[def_it].Definition = c_ssfx_sss_omni_quality;
        def_it++;
        sh_name[len] = '0' + char(sss_omni_quality / 10);
        ++len;
        sh_name[len] = '0' + char(sss_omni_quality % 10);
        ++len;

        // SSFX_BEEFS_NVG is deliberately never defined here. Monolith has no such switch: the
        // Beefs NVGs patch defines it in its own check_screenspace_NVG.h, exactly like every other
        // SSFX module switch. The patch is not part of this port, so the #ifdef SSFX_BEEFS_NVG
        // blocks in combine_1 / combine_2 / pp_blur simply never compile.

        // The pseudo-PBR module's two compiled-in switches ( pbr_settings.h: USE_BURLEY_DIFFUSE,
        // USE_GGX_SPECULAR ), from ssfx_es_burley / ssfx_es_ggx. One char each in the key, so the
        // two choices never share a cached shader. Shaders are compiled when they are created,
        // so a change shows after a restart.
        c_ssfx_es_burley[0] = ps_ssfx_es_burley ? '1' : '0';
        c_ssfx_es_burley[1] = 0;
        defines[def_it].Name = "SSFX_ES_BURLEY";
        defines[def_it].Definition = c_ssfx_es_burley;
        def_it++;
        sh_name[len] = c_ssfx_es_burley[0];
        ++len;

        c_ssfx_es_ggx[0] = ps_ssfx_es_ggx ? '1' : '0';
        c_ssfx_es_ggx[1] = 0;
        defines[def_it].Name = "SSFX_ES_GGX";
        defines[def_it].Definition = c_ssfx_es_ggx;
        def_it++;
        sh_name[len] = c_ssfx_es_ggx[0];
        ++len;

        defines[def_it].Name = "SSFX_MODEXE";
        defines[def_it].Definition = "1";
        def_it++;
        sh_name[len] = '1';
        ++len;
    }

    sh_name[len] = 0;

    // finish
    defines[def_it].Name = 0;
    defines[def_it].Definition = 0;
    def_it++;

    //
    if (0 == xr_strcmp(pFunctionName, "main")) {
        if ('v' == pTarget[0]) {
            if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_0)
                pTarget = "vs_4_0";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_1)
                pTarget = "vs_4_1";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_11_0)
                pTarget = "vs_5_0";
        } else if ('p' == pTarget[0]) {
            if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_0)
                pTarget = "ps_4_0";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_1)
                pTarget = "ps_4_1";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_11_0)
                pTarget = "ps_5_0";
        } else if ('g' == pTarget[0]) {
            if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_0)
                pTarget = "gs_4_0";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_10_1)
                pTarget = "gs_4_1";
            else if (HW.FeatureLevel == D3D_FEATURE_LEVEL_11_0)
                pTarget = "gs_5_0";
        } else if ('c' == pTarget[0]) {
            if (HW.FeatureLevel == D3D_FEATURE_LEVEL_11_0)
                pTarget = "cs_5_0";
        }
    }

    HRESULT _result = E_FAIL;
    // DX10 appends skinning/MSAA indices (e.g. model_scope_lense_0).
    // Both stages must be refreshed when their source or vertex layout changes.
    const LPCSTR lens_shader_name = "model_scope_lense";
    const u32 lens_shader_name_len = xr_strlen(lens_shader_name);
    const bool scope_lens_shader = ('p' == pTarget[0] || 'v' == pTarget[0]) &&
        0 == strncmp(name, lens_shader_name, lens_shader_name_len) &&
        (name[lens_shader_name_len] == 0 ||
         (name[lens_shader_name_len] == '_' &&
          name[lens_shader_name_len + 1] >= '0' && name[lens_shader_name_len + 1] <= '7' &&
          name[lens_shader_name_len + 2] == 0));
    bool loaded_from_cache = false;
    double build_compile_ms = -1.0; // item 9 stage B: the time inside D3DCompile

    string_path folder_name, folder;
    xr_strcpy(folder, "r3\\objects\\r4\\");
    xr_strcat(folder, name);
    xr_strcat(folder, ".");

    char extension[3];
    strncpy_s(extension, pTarget, 2);
    xr_strcat(folder, extension);

    FS.update_path(folder_name, "$game_shaders$", folder);
    xr_strcat(folder_name, "\\");

    m_file_set.clear();
    FS.file_list(m_file_set, folder_name, FS_ListFiles | FS_RootOnly, "*");

    string_path temp_file_name, file_name;
    // item 9 stage E: only the generated cache ( userdata ) keeps the source CRC; match_shader_id
    // never finds a shipped shader ( its #if 1 ), so this is always the generated cache
    bool const generated_cache = !match_shader_id(name, sh_name, m_file_set, temp_file_name);
    if (generated_cache) {
        string_path file;
        xr_strcpy(file, "shaders_cache\\r4\\");
        xr_strcat(file, name);
        xr_strcat(file, ".");
        xr_strcat(file, extension);
        xr_strcat(file, "\\");
        xr_strcat(file, sh_name);
        // The lens shaders are updated alongside gameplay features. Their old
        // cached bytecode can be valid while ignoring new scope_lens_state modes.
        // Keep its cache key tied to the source without invalidating other shaders.
        if (scope_lens_shader) {
            string16 source_crc;
            xr_sprintf(source_crc, "_%08x", crc32(pSrcData, SrcDataLen));
            xr_strcat(file, source_crc);
        }
        FS.update_path(file_name, "$app_data_root$", file);
    } else {
        xr_strcpy(file_name, folder_name);
        xr_strcat(file_name, temp_file_name);
    }

    // item 9 stage E: a generated cache file is [ source CRC ][ code CRC ][ code ], as the donor's.
    // A file of the old layout [ code CRC ][ code ] fails the source test and is built once again.
    u32 const source_crc = generated_cache ? shader_source_crc(pSrcData, SrcDataLen) : 0;

    if (FS.exist(file_name)) {
        IReader* file = FS.r_open(file_name);
        if (generated_cache) {
            if (file->length() > 8) {
                u32 const saved_source_crc = file->r_u32();
                if (saved_source_crc == source_crc) {
                    u32 const crc = file->r_u32();
                    u32 const real_crc = crc32(file->pointer(), file->elapsed());
                    if (real_crc == crc) {
                        _result = create_shader(pTarget, (DWORD*)file->pointer(), file->elapsed(),
                                                file_name, result, o.disasm);
                        loaded_from_cache = SUCCEEDED(_result);
                    }
                } else
                    Msg("~ Shader cache source CRC mismatch for '%s.%s': cached=0x%08x "
                        "current=0x%08x, recompiling",
                        name, extension, saved_source_crc, source_crc);
            }
        } else if (file->length() > 4) {
            u32 crc = 0;
            crc = file->r_u32();

            u32 const real_crc = crc32(file->pointer(), file->elapsed());

            if (real_crc == crc) {
                _result = create_shader(pTarget, (DWORD*)file->pointer(), file->elapsed(),
                                        file_name, result, o.disasm);
                loaded_from_cache = SUCCEEDED(_result);
            }
        }
        file->close();
    }

    if (FAILED(_result)) {
        includer Includer;
        LPD3DBLOB pShaderBuf = NULL;
        LPD3DBLOB pErrorBuf = NULL;
        // item 9 stage B: the shader build measurement
        const shader_build_clock::time_point compile_start = shader_build_clock::now();
        // item 9 stage E: d3dcompiler_47 when it is there ( shader_compiler )
        _result = shader_compiler()(pSrcData, SrcDataLen,
                             "", // NULL, //LPCSTR pFileName,	//	NVPerfHUD bug workaround.
                             defines, &Includer, pFunctionName, pTarget, Flags, 0, &pShaderBuf,
                             &pErrorBuf);
        build_compile_ms = shader_build_ms(compile_start); // item 9 stage B

        if (SUCCEEDED(_result)) {
            IWriter* file = FS.w_open(file_name);

            // boost::crc_32_type		processor;
            // processor.process_block	( pShaderBuf->GetBufferPointer(),
            // ((char*)pShaderBuf->GetBufferPointer()) + pShaderBuf->GetBufferSize() );
            u32 const crc = crc32(pShaderBuf->GetBufferPointer(), pShaderBuf->GetBufferSize());

            if (generated_cache) // item 9 stage E: the source CRC first
                file->w_u32(source_crc);
            file->w_u32(crc);
            file->w(pShaderBuf->GetBufferPointer(), (u32)pShaderBuf->GetBufferSize());
            FS.w_close(file);

            _result = create_shader(pTarget, (DWORD*)pShaderBuf->GetBufferPointer(),
                                    (u32)pShaderBuf->GetBufferSize(), file_name, result, o.disasm);
        } else {
            //			Msg						( "! shader compilation failed"
            //);
            Log("! ", file_name);
            if (pErrorBuf)
                Log("! error: ", (LPCSTR)pErrorBuf->GetBufferPointer());
            else
                Msg("Can't compile shader hr=0x%08x", _result);
        }
        // item 9 stage E: the compiler's buffers go back here; the original engine kept them until
        // the game closed. Nothing needs them any more: the code is in the cache file and in the
        // shader ( create_shader copies it, as for the cache path above, whose buffer file->close()
        // frees right after ), the error text is in the log.
        _RELEASE(pShaderBuf);
        _RELEASE(pErrorBuf);
    }

    if (scope_lens_shader && SUCCEEDED(_result))
        Msg("* ScopeLens%s: %s source_crc=%08x file=%s", pTarget[0] == 'v' ? "VS" : "PS",
            loaded_from_cache ? "cached" : "compiled", crc32(pSrcData, SrcDataLen), file_name);

    // item 9 stage B: the shader build measurement
    shader_build_note(name, pTarget, loaded_from_cache, SUCCEEDED(_result), build_compile_ms,
                      shader_build_ms(build_start));

    return _result;
}

static inline bool match_shader(LPCSTR const debug_shader_id, LPCSTR const full_shader_id,
                                LPCSTR const mask, size_t const mask_length) {
    u32 const full_shader_id_length = xr_strlen(full_shader_id);
    R_ASSERT2(
        full_shader_id_length == mask_length,
        make_string("bad cache for shader %s, [%s], [%s]", debug_shader_id, mask, full_shader_id));
    char const* i = full_shader_id;
    char const* const e = full_shader_id + full_shader_id_length;
    char const* j = mask;
    for (; i != e; ++i, ++j) {
        if (*i == *j)
            continue;

        if (*j == '_')
            continue;

        return false;
    }

    return true;
}

static inline bool match_shader_id(LPCSTR const debug_shader_id, LPCSTR const full_shader_id,
                                   FS_FileSet const& file_set, string_path& result) {
#if 1
	strcpy_s					( result, "" );
	return						false;
#else // #if 1
#ifdef DEBUG
    LPCSTR temp = "";
    bool found = false;
    FS_FileSet::const_iterator i = file_set.begin();
    FS_FileSet::const_iterator const e = file_set.end();
    for (; i != e; ++i) {
        if (match_shader(debug_shader_id, full_shader_id, (*i).name.c_str(), (*i).name.size())) {
            VERIFY(!found);
            found = true;
            temp = (*i).name.c_str();
        }
    }

    xr_strcpy(result, temp);
    return found;
#else  // #ifdef DEBUG
    FS_FileSet::const_iterator i = file_set.begin();
    FS_FileSet::const_iterator const e = file_set.end();
    for (; i != e; ++i) {
        if (match_shader(debug_shader_id, full_shader_id, (*i).name.c_str(), (*i).name.size())) {
            xr_strcpy(result, (*i).name.c_str());
            return true;
        }
    }

    return false;
#endif // #ifdef DEBUG
#endif // #if 1
}
