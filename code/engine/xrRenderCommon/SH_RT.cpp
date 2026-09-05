#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"

CRT::CRT() {
    
}

CRT::~CRT() {
    destroy();
    DEV->_DeleteRT(this); // release external reference
}

void CRT::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount) {
    if (pSurface) return;

    R_ASSERT(HW.pDevice && Name && Name[0] && w && h);
    _order = CPU::GetCLK(); 

    dwWidth = w;
    dwHeight = h;
    fmt = f;

    D3DCAPS9 caps;
    R_CHK(HW.pDevice->GetDeviceCaps(&caps));

    if (!btwIsPow2(w) || !btwIsPow2(h)) {
        if (!HW.Caps.raster.bNonPow2) return;
    }

    if (w > caps.MaxTextureWidth) return;
    if (h > caps.MaxTextureHeight) return;

    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Компактна логіка замість каскаду else-if
    u32 usage = D3DUSAGE_RENDERTARGET;
    if (fmt == D3DFMT_D24X8 || fmt == D3DFMT_D24S8 || 
        fmt == D3DFMT_D15S1 || fmt == D3DFMT_D16 || 
        fmt == D3DFMT_D16_LOCKABLE || 
        fmt == static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '2', '4'))) {
        usage = D3DUSAGE_DEPTHSTENCIL;
    }

    HRESULT _hr = HW.pD3D->CheckDeviceFormat(HW.DevAdapter, HW.DevT, HW.Caps.fTarget, usage, D3DRTYPE_TEXTURE, f);
    if (FAILED(_hr)) return;

    DEV->Evict();
    _hr = HW.pDevice->CreateTexture(w, h, 1, usage, f, D3DPOOL_DEFAULT, &pSurface, nullptr);
    HW.stats_manager.increment_stats_rtarget(pSurface);

    if (FAILED(_hr) || !pSurface) return;

#ifdef DEBUG
    Msg("* created RT(%s), %dx%d", Name, w, h);
#endif 

    R_CHK(pSurface->GetSurfaceLevel(0, &pRT));
    pTexture = DEV->_CreateTexture(Name);
    pTexture->surface_set(pSurface);
}

void CRT::destroy() {
    if (pTexture._get()) {
        pTexture->surface_set(nullptr);
        pTexture = nullptr;
    }

    _RELEASE(pRT);

    HW.stats_manager.decrement_stats_rtarget(pSurface);
    _RELEASE(pSurface);
}

void CRT::reset_begin() { destroy(); }
void CRT::reset_end() { create(*cName, dwWidth, dwHeight, fmt); }

void resptrcode_crt::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount) {
    _set(DEV->_CreateRT(Name, w, h, f));
}