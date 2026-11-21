#include "stdafx.h"

// OGSE screen-space sun shafts.
//
// Ported from Monolith src\Layers\xrRender\rendertarget_phase_sunshafts.cpp. Five full-screen
// passes: a mask built from the depth buffer, three radial blurs that smear it away from the sun,
// and a final pass that adds the result to the scene.
//
// There are two deviations from the donor, both forced by this engine, and nothing else changes.
//
// 1. GEOMETRY. The donor locks its quad against g_KD, a POSITIONT + TEXCOORD0 declaration it keeps
//    for exactly this kind of pass. This engine has no g_KD; every full-screen pass here goes
//    through g_combine, which is POSITIONT + COLOR + TEXCOORD0 ( FVF::TL ). That is safe: a D3D11
//    input layout binds by semantic, not by order or offset, so the extra COLOR element is simply
//    not read by ssss_notransform.vs, which declares only P : POSITIONT and tc0 : TEXCOORD0. The
//    colour written into it is never sampled.
//
// 2. DESTINATION OF THE LAST PASS. The final pass samples the scene out of r2_RT_generic0 and has
//    to write the result back to it, which no API allows in one draw. The donor renders into
//    rt_Generic and copies. In this engine rt_Generic is only created when MSAA is on
//    ( r4_rendertarget.cpp:475-479 ), so the destination follows the same rule the engine's own
//    screen-space passes already use - rt_Generic under MSAA, rt_Color without it - and the copy
//    back into rt_Generic_0 is unchanged. See phase_ssfx_fog_scattering, which does exactly this.

void CRenderTarget::phase_sunshafts() {
    // The donor tests only the weather intensity, because over there the two render targets and
    // the shader exist on every machine. Here they are created under advancedpp, next to
    // rt_Generic_2, so the same condition has to hold before any of them is touched -
    // need_to_render_sunshafts() is exactly that test plus the weather one.
    if (!need_to_render_sunshafts())
        return;

    CEnvDescriptor& env = *g_pGamePersistent->Environment().CurrentEnv;
    if (env.m_fSunShaftsIntensity <= 0.001f)
        return;

    u32 Offset = 0;
    Fvector2 p0, p1;

    const u32 C = color_rgba(255, 255, 255, 255);

    float _w = float(Device.dwWidth);
    float _h = float(Device.dwHeight);

    p0.set(.5f / _w, .5f / _h);
    p1.set((_w + .5f) / _w, (_h + .5f) / _h);

    // The donor writes these four vertices out five times, once per pass, identically. Folded into
    // one lambda here; the values are the donor's, vertex for vertex.
    auto fill_quad = [&]() {
        FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(EPS, _h + EPS, EPS, 1.f, C, p0.x, p1.y);
        pv++;
        pv->set(EPS, EPS, EPS, 1.f, C, p0.x, p0.y);
        pv++;
        pv->set(_w + EPS, _h + EPS, EPS, 1.f, C, p1.x, p1.y);
        pv++;
        pv->set(_w + EPS, EPS, EPS, 1.f, C, p1.x, p0.y);
        pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);
    };

    // constants
    Fvector4 params = { 0, 0, 0, 0 };
    params.x = env.m_fSunShaftsIntensity < 1.f ? (env.m_fSunShaftsIntensity * 2.0f) : 1.f;
    params.y = ps_r2_ss_sunshafts_length;
    params.w = ps_r2_ss_sunshafts_radius;

    // *** MASK GENERATION ***
    // Builds the geometry mask the blurs work on.
    u_setrt(rt_sunshafts_0, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    fill_quad();

    RCache.set_Element(s_sunshafts->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // *** FIRST PASS ***
    u_setrt(rt_sunshafts_1, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    fill_quad();

    params.z = 1.0f;

    RCache.set_Element(s_sunshafts->E[1]);
    RCache.set_c("c_sunshafts", params);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // *** SECOND PASS ***
    u_setrt(rt_sunshafts_0, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    fill_quad();

    params.z = 0.7f;

    RCache.set_Element(s_sunshafts->E[2]);
    RCache.set_c("c_sunshafts", params);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // *** THIRD PASS ***
    u_setrt(rt_sunshafts_1, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    fill_quad();

    params.z = 0.3f;

    RCache.set_Element(s_sunshafts->E[3]);
    RCache.set_c("c_sunshafts", params);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    RCache.set_Stencil(FALSE);

    // *** BLEND PASS ***
    // Combines the shafts with the scene image. Deviation 2 above.
    ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

    u_setrt(dest_rt, 0, 0, HW.pBaseZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);

    fill_quad();

    params.z = 0.0f;

    RCache.set_Element(s_sunshafts->E[4]);
    RCache.set_c("c_sunshafts", params);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    RCache.set_Stencil(FALSE);

    HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(),
                              dest_rt->pTexture->surface_get());
}
