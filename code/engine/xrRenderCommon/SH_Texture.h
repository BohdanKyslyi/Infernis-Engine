#ifndef SH_TEXTURE_H
#define SH_TEXTURE_H
#pragma once

#include "xrCore/xr_resource.h"

class ENGINE_API CAviPlayerCustom;
class CTheoraSurface;

class ECORE_API CTexture : public xr_resource_named {
public:
    enum ResourceShaderType {
      rstPixel = 0,
      rstVertex = D3DVERTEXTEXTURESAMPLER0,
      rstGeometry = rstVertex + 256,
      rstHull = rstGeometry + 256,
      rstDomain = rstHull + 256,
      rstCompute = rstDomain + 256,
      rstInvalid = rstCompute + 256
    };

public:
    void __stdcall apply_load(u32 stage);
    void __stdcall apply_theora(u32 stage);
    void __stdcall apply_avi(u32 stage);
    void __stdcall apply_seq(u32 stage);
    void __stdcall apply_normal(u32 stage);

    void Preload();
    void Load();
    void PostLoad();
    void Unload();

    void surface_set(ID3DBaseTexture* surf);
    ID3DBaseTexture* surface_get();

    [[nodiscard]] IC BOOL isUser() const { return flags.bUser; }
    [[nodiscard]] IC u32 get_Width() {
        desc_enshure();
        return desc.Width;
    }
    [[nodiscard]] IC u32 get_Height() {
        desc_enshure();
        return desc.Height;
    }

    void video_Sync(u32 _time) { m_play_time = _time; }
    void video_Play(BOOL looped, u32 _time = 0xFFFFFFFF);
    void video_Pause(BOOL state);
    void video_Stop();
    [[nodiscard]] BOOL video_IsPlaying();

    CTexture();
    virtual ~CTexture();

#if defined(USE_DX10) || defined(USE_DX11)
    ID3DShaderResourceView* get_SRView() { return m_pSRView; }
#endif

private:
    IC BOOL desc_valid() const { return pSurface == desc_cache; }
    IC void desc_enshure() {
        if (!desc_valid())
            desc_update();
    }
    void desc_update();
#if defined(USE_DX10) || defined(USE_DX11)
    void Apply(u32 dwStage);
    void ProcessStaging();
    D3D_USAGE GetUsage();
#endif

public:
    // C++11 Aggregate zero-initialization
    struct {
        u32 bLoaded : 1;
        u32 bUser : 1;
        u32 seqCycles : 1;
        u32 MemoryUsage : 28;
#if defined(USE_DX10) || defined(USE_DX11)
        u32 bLoadedAsStaging : 1;
#endif
    } flags{}; 

    fastdelegate::FastDelegate1<u32> bind;

    CAviPlayerCustom* pAVI{nullptr};
    CTheoraSurface* pTheora{nullptr};
    float m_material{1.0f};
    shared_str m_bumpmap;

    union {
        u32 m_play_time{0}; 
        u32 seqMSPF;     
    };

private:
    ID3DBaseTexture* pSurface{nullptr};
    xr_vector<ID3DBaseTexture*> seqDATA;
    ID3DBaseTexture* desc_cache{nullptr};
    D3D_TEXTURE2D_DESC desc{};

#if defined(USE_DX10) || defined(USE_DX11)
    ID3DShaderResourceView* m_pSRView{nullptr};
    xr_vector<ID3DShaderResourceView*> m_seqSRView;
#endif
};

struct resptrcode_texture : public resptr_base<CTexture> {
    void create(LPCSTR _name);
    void destroy() { _set(nullptr); }
    shared_str bump_get() { return _get()->m_bumpmap; }
    bool bump_exist() { return 0 != bump_get().size(); }
};
typedef resptr_core<CTexture, resptrcode_texture> ref_texture;

#endif // SH_TEXTURE_H