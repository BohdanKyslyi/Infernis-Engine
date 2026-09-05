#pragma once

#ifndef _HW_CAPS_
#define _HW_CAPS_

// C++17: Безпечна constexpr функція
[[nodiscard]] constexpr u32 CAP_VERSION(u32 a, u32 b) noexcept {
    return (a * 10 + b);
}

class CHWCaps {
public:
    static constexpr size_t MAX_GPUS = 8;

public:
    struct caps_Geometry {
        u32 dwRegisters : 16;
        u32 dwInstructions : 16;
        u32 bSoftware : 1;
        u32 bPointSprites : 1;
        u32 bVTF : 1; // vertex-texture-fetch
        u32 bNPatches : 1;
        u32 dwClipPlanes : 4;
        u32 dwVertexCache : 8;
    };
    
    struct caps_Raster {
        u32 dwRegisters : 16;
        u32 dwInstructions : 16;
        u32 dwStages : 4; // number of tex-stages
        u32 dwMRT_count : 4;
        u32 b_MRT_mixdepth : 1;
        u32 bNonPow2 : 1;
        u32 bCubemap : 1;
    };

public:
    BOOL bForceGPU_REF{ FALSE };
    BOOL bForceGPU_SW{ FALSE };
    BOOL bForceGPU_NonPure{ FALSE };
    BOOL SceneMode{ FALSE };

    u32 iGPUNum{ 1 };

    D3DFORMAT fTarget{ D3DFMT_UNKNOWN };
    D3DFORMAT fDepth{ D3DFMT_UNKNOWN };
    u32 dwMAC_OQ{ 0 };

    caps_Geometry geometry{};
    caps_Raster raster{};

    u16 geometry_major{ 0 };
    u16 geometry_minor{ 0 };
    u16 raster_major{ 0 };
    u16 raster_minor{ 0 };

    u32 id_vendor{ 0 };
    u32 id_device{ 0 };

    BOOL bStencil{ FALSE };
    BOOL bScissor{ FALSE };
    BOOL bTableFog{ FALSE };

    D3DSTENCILOP soDec{ D3DSTENCILOP_KEEP };
    D3DSTENCILOP soInc{ D3DSTENCILOP_KEEP };
    u32 dwMaxStencilValue{ 0 };

public:
    CHWCaps() = default;
    ~CHWCaps() = default;
    
    void Update();
};

#endif // _HW_CAPS_