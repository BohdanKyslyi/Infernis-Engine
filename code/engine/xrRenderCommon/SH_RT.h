#ifndef SH_RT_H
#define SH_RT_H
#pragma once

//////////////////////////////////////////////////////////////////////////
class CRT : public xr_resource_named {
public:
    CRT();
    ~CRT();

#ifdef USE_DX11
    void create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1, bool useUAV = false);
#else
    void create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1);
#endif

    void destroy();
    void reset_begin();
    void reset_end();
    
    [[nodiscard]] inline BOOL valid() const { return !!pTexture; }

public:
    // C++11 Безпечна ініціалізація
    ID3DTexture2D* pSurface{ nullptr };
    ID3DRenderTargetView* pRT{ nullptr };

#if defined(USE_DX10) || defined(USE_DX11)
    ID3DDepthStencilView* pZRT{ nullptr };

#ifdef USE_DX11
    ID3D11UnorderedAccessView* pUAView{ nullptr };
#endif
#endif // USE_DX10

    ref_texture pTexture;

    u32 dwWidth{ 0 };
    u32 dwHeight{ 0 };
    D3DFORMAT fmt{ D3DFMT_UNKNOWN };

    u64 _order{ 0 };
};

struct resptrcode_crt : public resptr_base<CRT> {
#ifdef USE_DX11
    void create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1, bool useUAV = false);
#else
    void create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1);
#endif
    void destroy() { _set(nullptr); }
};

typedef resptr_core<CRT, resptrcode_crt> ref_rt;

#endif // SH_RT_H