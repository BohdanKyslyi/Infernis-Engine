#include "stdafx.h"

#include "xrRenderCommon/light.h"
#include "xrRenderCommon/Light_Package.h"

#include <algorithm>

// Screen Space Shaders render passes, ported from Monolith:
//   Layers\xrRender\rendertarget_phase_blur.cpp   - SSR, volumetric blur, water, SSS, fog, motion,
//                                                   and phase_blur itself ( end of this file )
//   Layers\xrRender\rendertarget_phase_dof.cpp    - depth of field ( end of this file )
//   Layers\xrRender\rendertarget_phase_smaa.cpp   - TAA
//   Layers\xrRenderPC_R4\r4_rendertarget_phase_ssao.cpp  - AO and indirect light
//   Layers\xrRenderPC_R4\r4_rendertarget_phase_rain.cpp  - rain refraction
//   Layers\xrRenderPC_R4\r4_rendertarget_phase_bloom.cpp - bloom
// They are kept together in one file so that Infernis's own SSAO, rain and bloom passes stay
// untouched. phase_blur and phase_dof are Monolith's own passes rather than SSFX ones, but SSFX
// depends on both: phase_blur fills rt_blur_2 for the SSFX bloom and depth of field, and
// phase_dof is the pass that runs the SSFX depth_of_field.ps. Monolith's phase_smaa is not SSFX
// and is not here.

// --------------------------------------------------------------------------------------------
// SSS light slots
//
// Monolith keeps the slot table and its release callback inside phase_ssfx_sss_ext as a function
// static plus a lambda. A captureless lambda decays to a __cdecl function pointer, and
// FastDelegate1::bind wants __stdcall, which does not compile on a 32 bit build - so the same two
// objects live at file scope here, with the callback declared __stdcall. Behaviour is unchanged.
static light* ssfx_sss_light_slot[8] = {};

static void xr_stdcall ssfx_sss_on_light_destroy(light* l) {
    for (int i = 0; i < 8; i++) {
        if (ssfx_sss_light_slot[i] == l)
            ssfx_sss_light_slot[i] = nullptr;
    }
}

IC bool ssfx_sort_lights(light* i, light* j) {
    return (i->distance < j->distance && i->sss_priority < j->sss_priority);
}

// --------------------------------------------------------------------------------------------
// Screen space reflections
void CRenderTarget::phase_ssfx_ssr() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    float ScaleFactor = std::min(std::max(ps_ssfx_ssr.x, 1.0f), 2.0f);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    // GLOSS /////////////////////////////////////////////////////////////////
    u_setrt(rt_ssfx_temp3, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_ssr->E[5]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    p1.set(1.0f / ScaleFactor, 1.0f / ScaleFactor);

    float scale_X = w / ScaleFactor;
    float scale_Y = h / ScaleFactor;

    // SSR ///////////////////////////////////////////////////////////////////
    u_setrt(rt_ssfx, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    if (ScaleFactor > 1.0f)
        set_viewport_size(HW.pContext, scale_X, scale_Y);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_ssr->E[0]);
    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_c("cam_pos", ::Random.randF(-1.0, 1.0), ::Random.randF(-1.0, 1.0), 0.0f, 0.0f);

    RCache.set_c("ssr_setup", ps_ssfx_ssr);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // COPY SSR RESULT ( ACC ) ///////////////////////////////////////////////
    HW.pContext->CopyResource(rt_ssfx_ssr->pTexture->surface_get(), rt_ssfx->pTexture->surface_get());

    {
        // BLUR PHASE 1 //////////////////////////////////////////////////////
        u_setrt(rt_ssfx_temp, 0, 0, HW.pBaseZB);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_ssr->E[1]);
        RCache.set_c("blur_params", 1.0, 0.0, scale_X, scale_Y);
        RCache.set_c("ssr_setup", ps_ssfx_ssr);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

        // BLUR PHASE 2 //////////////////////////////////////////////////////
        u_setrt(rt_ssfx_temp2, 0, 0, HW.pBaseZB);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_ssr->E[2]);
        RCache.set_c("blur_params", 0.0, 1.0, w, h);
        RCache.set_c("ssr_setup", ps_ssfx_ssr);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    // COMBINE ///////////////////////////////////////////////////////////////
    if (ScaleFactor > 1.0f)
        set_viewport_size(HW.pContext, w, h);

    p1.set(1.0f, 1.0f);

    if (!RImplementation.o.dx10_msaa)
        u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
    else
        u_setrt(rt_Generic_0_r, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_ssr->E[3]);
    RCache.set_c("ssr_setup", ps_ssfx_ssr);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// --------------------------------------------------------------------------------------------
// Volumetric light blur
void CRenderTarget::phase_ssfx_volumetric_blur() {
    // rt_Generic_2 holds whatever was left there if no volumetric was written, so clear it.
    if (!m_bHasActiveVolumetric) {
        FLOAT ColorRGBA[4] = { 0.0, 0.0, 0.0, 0.0 };
        HW.pContext->ClearRenderTargetView(rt_Generic_2->pRT, ColorRGBA);
    }

    if (!m_bHasActiveVolumetric_spot) {
        FLOAT ColorRGBA[4] = { 0.0, 0.0, 0.0, 0.0 };
        HW.pContext->ClearRenderTargetView(rt_ssfx_volumetric->pRT, ColorRGBA);
        return;
    }

    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    FVF::TL* pv;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    // Volumetric always at 1/8 res
    set_viewport_size(HW.pContext, w / 8, h / 8);

    ref_rt* rt_VolBlur[2] = { &rt_ssfx_volumetric_tmp, &rt_ssfx_volumetric };
    int pixelsize[4] = { 0, 1, 1, 2 }; // half pixel + pixelsize
    float pixelscale[4] = { 2.0f, 0.5f, 2.0f, 0.5f };

    // BLUR //////////////////////////////////////////////////////////////////
    for (int b = 0; b < 4; b++) {
        u_setrt(*rt_VolBlur[b % 2], 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
        pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
        pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
        pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_volumetric_blur->E[b % 2]);
        RCache.set_c("blur_setup", w / 8, h / 8, (float)pixelsize[b], pixelscale[b]);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    set_viewport_size(HW.pContext, w, h);

    // COMBINE ///////////////////////////////////////////////////////////////
    u_setrt(rt_ssfx_accum, 0, 0, NULL);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_volumetric_blur->E[5]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(),
                              rt_ssfx_accum->pTexture->surface_get());
}

// --------------------------------------------------------------------------------------------
// Water blur
void CRenderTarget::phase_ssfx_water_blur() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(0.5f, 0.5f);

    set_viewport_size(HW.pContext, w / 2, h / 2);

    if (ps_ssfx_water.y > 0) {
        // BLUR PHASE 1 //////////////////////////////////////////////////////
        u_setrt(rt_ssfx_temp2, 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_water_blur->E[0]);
        RCache.set_c("blur_setup", 1, 0, 0, 2.0f / ps_ssfx_water.x);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

        // BLUR PHASE 2 //////////////////////////////////////////////////////
        u_setrt(rt_ssfx_temp, 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_water_blur->E[1]);
        RCache.set_c("blur_setup", 0, 1, 0, 1.0f);

        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    } else {
        HW.pContext->CopyResource(rt_ssfx_temp2->pTexture->surface_get(),
                                  rt_ssfx_temp->pTexture->surface_get());

        u_setrt(rt_ssfx_temp, 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_water_blur->E[2]);
        RCache.set_c("blur_setup", 0, 0, 0, 2.0f / ps_ssfx_water.x);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    set_viewport_size(HW.pContext, w, h);
    p1.set(1.0f, 1.0f);
}

// --------------------------------------------------------------------------------------------
// Water waves height map
void CRenderTarget::phase_ssfx_water_waves() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    u32 w = Device.dwWidth;
    u32 h = Device.dwHeight;

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    set_viewport_size(HW.pContext, 512, 512);

    u_setrt(rt_ssfx_water_waves, 0, 0, NULL);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_water_blur->E[5]);
    // NOT AS IN MONOLITH: the same minimum wind as the water surface itself (see water_node).
    RCache.set_c("wind_setup", g_pGamePersistent->Environment().wind_anim.w,
                 std::max(g_pGamePersistent->Environment().CurrentEnv->wind_velocity,
                          ps_ssfx_wind_trees.w * 1000),
                 0, 0);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    set_viewport_size(HW.pContext, float(w), float(h));
}

// --------------------------------------------------------------------------------------------
// Sun shadow occlusion
void CRenderTarget::phase_ssfx_sss() {
    u32 Offset = 0;
    u32 C = color_rgba(255, 255, 255, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    u_setrt(rt_ssfx, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss->E[0]);

    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_c("ssfx_sss", ps_ssfx_sss);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // BLUR
    u_setrt(rt_ssfx_temp, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss->E[1]);

    RCache.set_c("blur_setup", 0, 1, 0, 0);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // BLUR
    u_setrt(rt_ssfx_temp2, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss->E[2]);

    RCache.set_c("blur_setup", 1, 0, 2, 0);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_ssfx_sss->pTexture->surface_get(),
                              rt_ssfx_temp2->pTexture->surface_get());
}

// --------------------------------------------------------------------------------------------
// Omni / spot shadow occlusion. Up to 8 lights are tracked in fixed slots.
void CRenderTarget::phase_ssfx_sss_ext(light_Package& LP) {
    static shared_str strLights("lights_data");
    static u32 sss_currentframe;

    void* LightData;

    u32 Offset = 0;
    u32 C = color_rgba(255, 255, 255, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss_ext->E[0]);

    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_c("ssfx_sss", ps_ssfx_sss);
    RCache.set_c("id_offset", 0);

    Fvector4* Lights_Array;
    {
        RCache.get_ConstantDirect(strLights, 4 * sizeof(Fvector4) * 2, 0, 0, &LightData);
        Lights_Array = (Fvector4*)LightData;
    }

    VERIFY(Lights_Array);

    if (Lights_Array) {
        for (int slot = 0; slot < 8; slot++)
            Lights_Array[slot].set(0, 0, 0, 0);

        xr_vector<light*> LightsSort;
        bool CheckPackage = true;

        if (Device.dwFrame > sss_currentframe) {
            sss_currentframe = Device.dwFrame + 2;

            xr_vector<light*>& source = LP.v_shadowed;
            for (u32 it = 0; it < source.size(); it++) {
                light* L = source[it];

                if (L->omnipart_num == 0 && L->range > 1.5f) {
                    if (L->distance < 800 && L->flags.bActive) {
                        L->distance_lpos = Device.vCameraPosition.distance_to(L->position);

                        if (L->distance_lpos <= L->range)
                            L->sss_priority = 0;
                        else
                            L->sss_priority = 1;

                        LightsSort.push_back(L);
                    }
                }

                // Refresh hierarchy
                if (L->sss_refresh) {
                    L->sss_refresh = false;
                    int done = 0;

                    for (u32 lit = 0; lit < source.size(); lit++) {
                        light* L2 = source[lit];
                        if (L2->omipart_parent == L->omipart_parent) {
                            L2->sss_id = L->sss_id;
                            done++;
                            if (done >= 6)
                                break; // Update done.
                        }
                    }
                }
            }

            // Sort Distance
            std::sort(LightsSort.begin(), LightsSort.end(), ssfx_sort_lights);

            for (u32 x = 0; x < LightsSort.size(); x++) {
                light* L = LightsSort[x];

                bool Add = true;
                int FreeSlot = -1;

                for (int slot = 0; slot < 8; slot++) {
                    if (ssfx_sss_light_slot[slot]) {
                        if (ssfx_sss_light_slot[slot] == L) {
                            Add = false;
                            break;
                        }
                    } else
                        FreeSlot = slot;
                }

                if (Add && FreeSlot > -1) {
                    ssfx_sss_light_slot[FreeSlot] = L;

                    L->sss_id = FreeSlot;
                    L->sss_on_light_destroy.bind(ssfx_sss_on_light_destroy);

                    if (L->flags.type == IRender_Light::OMNIPART)
                        L->sss_refresh = true;
                }
            }
        } else {
            // Don't check the sorted package when the frame is skipped
            CheckPackage = false;
        }

        for (int slot = 0; slot < 8; slot++) {
            if (ssfx_sss_light_slot[slot]) {
                // Check if the light still exists in the sorted Light Package
                bool Remove = true;

                if (CheckPackage) {
                    for (u32 x = 0; x < LightsSort.size(); x++) {
                        light* L = LightsSort[x];

                        if (L == ssfx_sss_light_slot[slot])
                            Remove = false;
                    }
                } else {
                    // The distance calc was skipped, check here instead
                    ssfx_sss_light_slot[slot]->distance_lpos =
                        Device.vCameraPosition.distance_to(ssfx_sss_light_slot[slot]->position);
                    Remove = false;
                }

                float Dist = ssfx_sss_light_slot[slot]->distance_lpos;

                if (Dist > (ssfx_sss_light_slot[slot]->range * 2.0f))
                    Remove = true;

                // Remove Light
                if (!ssfx_sss_light_slot[slot]->flags.bActive || Remove) {
                    if (ssfx_sss_light_slot[slot]->flags.type == IRender_Light::OMNIPART)
                        ssfx_sss_light_slot[slot]->sss_refresh = true;

                    ssfx_sss_light_slot[slot]->sss_id = -1;
                    ssfx_sss_light_slot[slot]->sss_on_light_destroy.clear();
                    ssfx_sss_light_slot[slot] = NULL;
                } else {
                    // Update Light
                    Fvector L_pos;

                    Device.mView.transform_tiny(L_pos, ssfx_sss_light_slot[slot]->position);

                    // Distance atte ( MaxAtte when the light range is bigger than the sort range )
                    float MaxAtte =
                        1.0f - (clampr((ssfx_sss_light_slot[slot]->distance - 780) / -100, 0.f, 1.f));
                    float Atte =
                        1.0f - (clampr((Dist - ssfx_sss_light_slot[slot]->range * 1.9f) /
                                           -(ssfx_sss_light_slot[slot]->range / 2.0f),
                                       0.f, 1.f));

                    // ( Reminder ) The value is inverted ( 1.0 = Fadeout ~ 0.0 = Full Visible )
                    Lights_Array[slot].set(L_pos.x, L_pos.y, L_pos.z, std::max(MaxAtte, Atte));
                }
            }
        }
    }

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_ssfx_sss_ext->pTexture->surface_get(),
                              rt_ssfx_sss_tmp->pTexture->surface_get());

    // SSS Ext 2 -------------------------------------------------------------

    u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss_ext->E[1]);

    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_c("id_offset", 1);
    RCache.get_ConstantDirect(strLights, 4 * sizeof(Fvector4) * 2, 0, 0, &LightData);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_ssfx_sss_ext2->pTexture->surface_get(),
                              rt_ssfx_sss_tmp->pTexture->surface_get());

    // Combine ---------------------------------------------------------------

    u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_sss_ext->E[2]);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// --------------------------------------------------------------------------------------------
// Fog scattering
void CRenderTarget::phase_ssfx_fog_scattering() {
    u32 Offset = 0;
    Fvector2 p0, p1;

    u32 C = color_rgba(255, 255, 255, 255);
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    FVF::TL* pv;

    ref_rt* rt_Blur[2] = { &rt_blur_4, &rt_blur_2 };

    for (int blurp = 0; blurp < 2; blurp++) {
        int SampleScale = 1 << (2 - blurp); // 0 = 4 -> 1 = 2

        set_viewport_size(HW.pContext, w / SampleScale, h / SampleScale);

        u_setrt(*rt_Blur[blurp], 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
        pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
        pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
        pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_fog_scattering->E[2 + blurp]);
        RCache.set_c("blur_setup", w / SampleScale, h / SampleScale, 0, 0);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    set_viewport_size(HW.pContext, w, h);

    ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

    // Fog Scattering
    u_setrt(dest_rt, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_fog_scattering->E[0]);
    RCache.set_c("ssfx_scattering_setup", ps_ssfx_fog_scattering, 0, 0, 0);

    RCache.set_Geometry(g_combine);

    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(),
                              dest_rt->pTexture->surface_get());
}

// --------------------------------------------------------------------------------------------
// Motion blur
void CRenderTarget::phase_ssfx_motion_blur() {
    u32 Offset = 0;
    Fvector2 p0, p1;

    u32 C = color_rgba(255, 255, 255, 255);
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

    u_setrt(dest_rt, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_motion_blur->E[0]);

    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(),
                              dest_rt->pTexture->surface_get());
}

// --------------------------------------------------------------------------------------------
// Temporal anti aliasing
void CRenderTarget::phase_ssfx_taa() {
    u32 Offset = 0;
    Fvector2 p0, p1;

    u32 C = color_rgba(255, 255, 255, 255);
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);
    float d_Z = EPS_S;
    float d_W = 1.f;

    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    // Temp motion vectors
    u_setrt(rt_ssfx_accum, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_taa->E[0]);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.pContext->CopyResource(rt_ssfx_taa->pTexture->surface_get(),
                              rt_ssfx_accum->pTexture->surface_get());

    // TAA
    ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

    u_setrt(dest_rt, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_taa->E[1]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Accumulate
    HW.pContext->CopyResource(rt_ssfx_prev_frame->pTexture->surface_get(),
                              dest_rt->pTexture->surface_get());

    // Sharpening phase
    u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_taa->E[2]);

    RCache.set_c("taa_setup", ps_ssfx_taa);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// --------------------------------------------------------------------------------------------
// Ambient occlusion
void CRenderTarget::phase_ssfx_ao() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    float ScaleFactor = std::min(std::max(ps_ssfx_ao.x, 1.0f), 8.0f);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f / ScaleFactor, 1.0f / ScaleFactor);

    float scale_X = w / ScaleFactor;
    float scale_Y = h / ScaleFactor;

    // AO ////////////////////////////////////////////////////////////////////
    u_setrt(rt_ssfx_temp, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    set_viewport_size(HW.pContext, scale_X, scale_Y);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_ao->E[0]);
    RCache.set_c("ao_setup", ps_ssfx_ao);

    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);

    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Save AO frame
    HW.pContext->CopyResource(rt_ssfx_ao->pTexture->surface_get(),
                              rt_ssfx_temp->pTexture->surface_get());

    p1.set(1.0f, 1.0f);
    set_viewport_size(HW.pContext, w, h);

    // Four blur passes, ping-ponging between temp3 and temp
    ref_rt* rt_Blur[4] = { &rt_ssfx_temp3, &rt_ssfx_temp, &rt_ssfx_temp3, &rt_ssfx_temp };
    const int blur_element[4] = { 1, 2, 1, 2 };
    const float blur_step[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

    for (int b = 0; b < 4; b++) {
        u_setrt(*rt_Blur[b], 0, 0, 0);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_ao->E[blur_element[b]]);
        // The first pass carries the AO scale in x, the rest use 1
        RCache.set_c("blur_setup", b == 0 ? ps_ssfx_ao.x : 1.0f, blur_step[b], scale_X, scale_Y);
        RCache.set_c("ao_setup", ps_ssfx_ao);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    set_viewport_size(HW.pContext, w, h);
}

// --------------------------------------------------------------------------------------------
// Indirect light
void CRenderTarget::phase_ssfx_il() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    float ScaleFactor = std::min(std::max(ps_ssfx_il.x, 1.0f), 8.0f);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f / ScaleFactor, 1.0f / ScaleFactor);

    float scale_X = w / ScaleFactor;
    float scale_Y = h / ScaleFactor;

    // IL ////////////////////////////////////////////////////////////////////
    u_setrt(rt_ssfx_temp2, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    set_viewport_size(HW.pContext, scale_X, scale_Y);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_ao->E[3]);
    RCache.set_c("ao_setup", ps_ssfx_il);
    RCache.set_c("m_current", Matrix_current);
    RCache.set_c("m_previous", Matrix_previous);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Save IL frame
    HW.pContext->CopyResource(rt_ssfx_il->pTexture->surface_get(),
                              rt_ssfx_temp2->pTexture->surface_get());

    // Four blur passes, ping-ponging between temp3 and temp2
    ref_rt* rt_Blur[4] = { &rt_ssfx_temp3, &rt_ssfx_temp2, &rt_ssfx_temp3, &rt_ssfx_temp2 };
    const int blur_element[4] = { 4, 5, 4, 5 };
    const float blur_step[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

    for (int b = 0; b < 4; b++) {
        u_setrt(*rt_Blur[b], 0, 0, 0);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_ao->E[blur_element[b]]);
        RCache.set_c("blur_setup", 1, blur_step[b], scale_X, scale_Y);
        RCache.set_c("ao_setup", ps_ssfx_il);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    set_viewport_size(HW.pContext, w, h);
}

// --------------------------------------------------------------------------------------------
// Rain refraction buffer
void CRenderTarget::phase_ssfx_rain() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    set_viewport_size(HW.pContext, w / 8.0f, h / 8.0f);

    u_setrt(rt_ssfx_rain, 0, 0, NULL);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(!RImplementation.o.dx10_msaa ? s_ssfx_rain->E[0] : s_ssfx_rain->E[1]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    set_viewport_size(HW.pContext, w, h);
}

// --------------------------------------------------------------------------------------------
// Bloom
void CRenderTarget::phase_ssfx_bloom() {
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 0);

    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    // BLOOM BUILD ///////////////////////////////////////////////////////////
    // Half resolution is the max size for everything
    set_viewport_size(HW.pContext, w / 2.0f, h / 2.0f);

    u_setrt(rt_ssfx_bloom1, 0, 0, NULL);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_bloom->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // BLOOM LENS ////////////////////////////////////////////////////////////
    if (ps_r2_mask_control.x > 0) {
        set_viewport_size(HW.pContext, w / 4.0f, h / 4.0f);

        u_setrt(rt_ssfx_bloom_tmp4, 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
        pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
        pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
        pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_bloom_lens->E[0]);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

        // Lens 2 Phase blur
        ref_rt* rt_LensBlur[2] = { &rt_ssfx_bloom_tmp4_2, &rt_ssfx_bloom_lens };

        for (int lensblur = 0; lensblur < 2; lensblur++) {
            u_setrt(*rt_LensBlur[lensblur], 0, 0, NULL);
            RCache.set_CullMode(CULL_NONE);
            RCache.set_Stencil(FALSE);

            pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
            pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
            pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
            pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
            pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
            RCache.Vertex.Unlock(4, g_combine->vb_stride);

            RCache.set_Element(s_ssfx_bloom_lens->E[1 + lensblur]);
            RCache.set_c("blur_setup", w / 4, h / 4, 0, 2.0f + (3.0f * lensblur));
            RCache.set_Geometry(g_combine);
            RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
        }
    }

    int SampleScale = 0;

    ref_rt* rt_Down[6] = { &rt_ssfx_bloom_tmp2,  &rt_ssfx_bloom_tmp4,  &rt_ssfx_bloom_tmp8,
                           &rt_ssfx_bloom_tmp16, &rt_ssfx_bloom_tmp32, &rt_ssfx_bloom_tmp64 };

    // BLOOM DOWNSAMPLE //////////////////////////////////////////////////////
    for (int downsample = 0; downsample < 6; downsample++) {
        SampleScale = 1 << (downsample + 1);

        set_viewport_size(HW.pContext, w / SampleScale, h / SampleScale);

        u_setrt(*rt_Down[downsample], 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
        pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
        pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
        pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_bloom_downsample->E[downsample]);
        RCache.set_c("blur_setup", w / SampleScale, h / SampleScale, 0, ps_ssfx_bloom_2.x);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    // Ends with `rt_ssfx_bloom_tmp2`
    ref_rt* rt_Up[5] = { &rt_ssfx_bloom_tmp32_2, &rt_ssfx_bloom_tmp16_2, &rt_ssfx_bloom_tmp8_2,
                         &rt_ssfx_bloom_tmp4_2, &rt_ssfx_bloom_tmp2 };

    // BLOOM UPSAMPLE ////////////////////////////////////////////////////////
    for (int upsample = 0; upsample < 5; upsample++) {
        SampleScale = 1 << (5 - upsample);

        set_viewport_size(HW.pContext, w / SampleScale, h / SampleScale);

        u_setrt(*rt_Up[upsample], 0, 0, NULL);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(FALSE);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
        pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
        pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
        pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_ssfx_bloom_upsample->E[upsample]);
        RCache.set_c("blur_setup", w / SampleScale, h / SampleScale, 0, ps_ssfx_bloom_2.x);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    // BLOOM COMBINE /////////////////////////////////////////////////////////
    u_setrt(rt_ssfx_bloom1, 0, 0, NULL);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_ssfx_bloom->E[1]);
    RCache.set_c("mask_control", ps_r2_mask_control.x, ps_r2_mask_control.y, ps_r2_mask_control.z,
                 ps_r2_mask_control.w);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Restore Viewport
    set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
}

// --------------------------------------------------------------------------------------------
// Blur chain
//
// Monolith Layers\xrRender\rendertarget_phase_blur.cpp:9-184, the DX10 / DX11 side of each #if.
// Six passes of pp_blur.ps: horizontal then vertical at 1/2, 1/4 and 1/8 of the screen. Each quad
// is drawn at the size of its target while the viewport stays at full size, so it covers the
// whole of the smaller target. That is the donor's own way, and it needs the viewport at full
// size on entry, which every pass that runs before this one leaves behind.
void CRenderTarget::phase_blur() {
    // Get common data
    u32 Offset = 0;
    float d_Z = EPS_S;
    float d_W = 1.0f;
    u32 C = color_rgba(0, 0, 0, 255);

    // Full resolution
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    ///////////////////////////////////////////////////////////////////////////////////
    ////Horizontal blur
    ///////////////////////////////////////////////////////////////////////////////////
    w = float(Device.dwWidth) * 0.5f;
    h = float(Device.dwHeight) * 0.5f;

    u_setrt(rt_blur_h_2, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[0]);
    RCache.set_c("blur_params", 1.0, 0.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
    ////Final blur
    ///////////////////////////////////////////////////////////////////////////////////
    u_setrt(rt_blur_2, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[1]);
    RCache.set_c("blur_params", 0.0, 1.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
    ////Horizontal blur / Half res
    ///////////////////////////////////////////////////////////////////////////////////
    w = float(Device.dwWidth) * 0.25f;
    h = float(Device.dwHeight) * 0.25f;

    u_setrt(rt_blur_h_4, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[2]);
    RCache.set_c("blur_params", 1.0, 0.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
    ////Final blur
    ///////////////////////////////////////////////////////////////////////////////////
    u_setrt(rt_blur_4, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[3]);
    RCache.set_c("blur_params", 0.0, 1.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
    ////Horizontal blur
    ///////////////////////////////////////////////////////////////////////////////////
    w = float(Device.dwWidth) * 0.125f;
    h = float(Device.dwHeight) * 0.125f;

    u_setrt(rt_blur_h_8, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[4]);
    RCache.set_c("blur_params", 1.0, 0.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
    ////Final blur
    ///////////////////////////////////////////////////////////////////////////////////
    u_setrt(rt_blur_8, 0, 0, 0);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Draw COLOR
    RCache.set_Element(s_blur->E[5]);
    RCache.set_c("blur_params", 0.0, 1.0, w, h);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ///////////////////////////////////////////////////////////////////////////////////
}

// --------------------------------------------------------------------------------------------
// Depth of field
//
// Monolith Layers\xrRender\rendertarget_phase_dof.cpp, the DX10 / DX11 side of each #if.
// Element 0 runs depth_of_field.ps into rt_dof; element 1 copies rt_dof through rt_Color ( or
// rt_Generic under MSAA ) back into the scene. dof_params and dof_kernel are sent the way the
// donor sends them, but the SSFX depth_of_field.ps does not read them: it is driven by
// ssfx_wpn_dof_1 and ssfx_wpn_dof_2.
void CRenderTarget::phase_dof() {
    // Constants
    u32 Offset = 0;
    u32 C = color_rgba(0, 0, 0, 255);

    float d_Z = EPS_S;
    float d_W = 1.0f;
    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    Fvector2 p0, p1;
    p0.set(0.0f, 0.0f);
    p1.set(1.0f, 1.0f);

    // DoF vectors
    Fvector2 vDofKernel;
    vDofKernel.set(0.5f / Device.dwWidth, 0.5f / Device.dwHeight);
    vDofKernel.mul(ps_r2_dof_kernel_size);
    Fvector3 dof;
    g_pGamePersistent->GetCurrentDof(dof);

    // Set MSAA/NonMSAA rendertarget
    u_setrt(rt_dof, 0, 0, HW.pBaseZB);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Set pass
    RCache.set_Element(s_dof->E[0]);

    // Set paramterers
    // RCache.set_c("taa_params", ps_taa_params.x, ps_taa_params.y, 0, 0);
    RCache.set_c("dof_params", dof.x, dof.y, dof.z, ps_r2_dof_sky);
    RCache.set_c("dof_kernel", vDofKernel.x, vDofKernel.y, ps_r2_dof_kernel_size, 0);

    // Set geometry
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
    u_setrt(dest_rt, nullptr, nullptr, nullptr);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    // Fill vertex buffer
    pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
    pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
    pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
    pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // Set pass
    RCache.set_Element(s_dof->E[1]);

    // Set geometry
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Resolve RT
    HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(),
                              dest_rt->pTexture->surface_get());
}
