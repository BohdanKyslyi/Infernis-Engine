#include "stdafx.h"
#include "xrEngine/Rain.h"

void CRenderTarget::PhaseRainDrops()
{

	static float rain_drops_factor = 0.f;

	// Reset accumulated wetness whenever automatic control is toggled.
	static bool saved_rain_drops_control = false;
	const bool current_rain_drops_control =
		!!ps_r2_rain_drops_flags.test(R2FLAG_RAIN_DROPS_CONTROL);
	if (saved_rain_drops_control != current_rain_drops_control) {
		saved_rain_drops_control = current_rain_drops_control;

		rain_drops_factor = 0.f;
	}

	if (!current_rain_drops_control)
		return;

	if (!g_pGamePersistent || !g_pGameLevel) {
		rain_drops_factor = 0.f;
		return;
	}

	CEnvironment& environment = g_pGamePersistent->Environment();
	if (!environment.CurrentEnv || !environment.eff_Rain) {
		rain_drops_factor = 0.f;
		return;
	}

	const float rain_density = std::clamp(environment.CurrentEnv->rain_density, 0.f, 1.f);
	const float rain_exposure =
		std::clamp(environment.eff_Rain->GetViewRainExposure(), 0.f, 1.f);
	const float target_factor = rain_density * rain_exposure;

	// A full-strength storm wets the view in 20 seconds and dries in 10.
	// Device.fTimeDelta makes the transition independent of frame rate.
	constexpr float wetting_time = 20.f;
	constexpr float drying_time = 10.f;
	const float transition_time =
		target_factor > rain_drops_factor ? wetting_time : drying_time;
	const float max_change = std::clamp(Device.fTimeDelta, 0.f, 0.25f) / transition_time;

	if (rain_drops_factor < target_factor)
		rain_drops_factor = std::min(rain_drops_factor + max_change, target_factor);
	else
		rain_drops_factor = std::max(rain_drops_factor - max_change, target_factor);

	if (rain_drops_factor <= EPS_L) {
		rain_drops_factor = 0.f;
		return;
	}

	u32 Offset = 0;
	Fvector2 p0, p1;

	struct v_aa {
		Fvector4 p;
		Fvector2 uv0;
		Fvector2 uv1;
		Fvector2 uv2;
		Fvector2 uv3;
		Fvector2 uv4;
		Fvector4 uv5;
		Fvector4 uv6;
	};

	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float ddw = 1.f / _w;
	float ddh = 1.f / _h;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	// Set RT's
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, NULL, NULL, HW.pBaseZB);
#else
	u_setrt(rt_Generic_0, NULL, NULL, HW.pBaseZB);
#endif

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	v_aa* pv = (v_aa*)RCache.Vertex.Lock(4, g_rain_drops->vb_stride, Offset);
	pv->p.set(EPS, float(_h + EPS), EPS, 1.f); pv->uv0.set(p0.x, p1.y); pv->uv1.set(p0.x - ddw, p1.y - ddh); pv->uv2.set(p0.x + ddw, p1.y + ddh); pv->uv3.set(p0.x + ddw, p1.y - ddh); pv->uv4.set(p0.x - ddw, p1.y + ddh); pv->uv5.set(p0.x - ddw, p1.y, p1.y, p0.x + ddw); pv->uv6.set(p0.x, p1.y - ddh, p1.y + ddh, p0.x); pv++;
	pv->p.set(EPS, EPS, EPS, 1.f); pv->uv0.set(p0.x, p0.y); pv->uv1.set(p0.x - ddw, p0.y - ddh); pv->uv2.set(p0.x + ddw, p0.y + ddh); pv->uv3.set(p0.x + ddw, p0.y - ddh); pv->uv4.set(p0.x - ddw, p0.y + ddh); pv->uv5.set(p0.x - ddw, p0.y, p0.y, p0.x + ddw); pv->uv6.set(p0.x, p0.y - ddh, p0.y + ddh, p0.x); pv++;
	pv->p.set(float(_w + EPS), float(_h + EPS), EPS, 1.f); pv->uv0.set(p1.x, p1.y); pv->uv1.set(p1.x - ddw, p1.y - ddh); pv->uv2.set(p1.x + ddw, p1.y + ddh); pv->uv3.set(p1.x + ddw, p1.y - ddh); pv->uv4.set(p1.x - ddw, p1.y + ddh); pv->uv5.set(p1.x - ddw, p1.y, p1.y, p1.x + ddw); pv->uv6.set(p1.x, p1.y - ddh, p1.y + ddh, p1.x); pv++;
	pv->p.set(float(_w + EPS), EPS, EPS, 1.f); pv->uv0.set(p1.x, p0.y); pv->uv1.set(p1.x - ddw, p0.y - ddh); pv->uv2.set(p1.x + ddw, p0.y + ddh); pv->uv3.set(p1.x + ddw, p0.y - ddh); pv->uv4.set(p1.x - ddw, p0.y + ddh); pv->uv5.set(p1.x - ddw, p0.y, p0.y, p1.x + ddw); pv->uv6.set(p1.x, p0.y - ddh, p0.y + ddh, p1.x); pv++;
	RCache.Vertex.Unlock(4, g_rain_drops->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_rain_drops->E[0]);
	RCache.set_c("rain_drops_params", rain_drops_factor, ps_r2_rain_drops_intensity, ps_r2_rain_drops_speed, 0.0f);
	RCache.set_Geometry(g_rain_drops);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
}
