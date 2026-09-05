#include "stdafx.h"
#pragma hdrstop

#include "HWCaps.h"
#include "HW.h"
#include <algorithm> 

#ifndef _EDITOR
#include "NVAPI/nvapi.h"
#include "ATI/atimgpud.h"
#endif

namespace {

#ifndef _EDITOR
u32 GetNVGpuNum() {
    NvLogicalGpuHandle logicalGPUs[NVAPI_MAX_LOGICAL_GPUS]{};
    NvU32 logicalGPUCount = 0;
    NvPhysicalGpuHandle physicalGPUs[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32 physicalGPUCount = 0;
    int iGpuNum = 0;

    if (NvAPI_Initialize() != NVAPI_OK) {
        Msg("* NVAPI is missing.");
        return iGpuNum;
    }

    if (NvAPI_EnumLogicalGPUs(logicalGPUs, &logicalGPUCount) != NVAPI_OK) {
        Msg("* NvAPI_EnumLogicalGPUs failed!");
        return iGpuNum;
    }

    if (NvAPI_EnumPhysicalGPUs(physicalGPUs, &physicalGPUCount) != NVAPI_OK) {
        Msg("* NvAPI_EnumPhysicalGPUs failed!");
        return iGpuNum;
    }

    Msg("* NVidia MGPU: Logical(%u), Physical(%u)", logicalGPUCount, physicalGPUCount);

    for (u32 i = 0; i < logicalGPUCount; ++i) {
        if (NvAPI_GetPhysicalGPUsFromLogicalGPU(logicalGPUs[i], physicalGPUs, &physicalGPUCount) == NVAPI_OK) {
            iGpuNum = std::max(iGpuNum, static_cast<int>(physicalGPUCount));
        }
    }

    if (iGpuNum > 1) {
        Msg("* NVidia MGPU: %d-Way SLI detected.", iGpuNum);
    }

    return static_cast<u32>(iGpuNum);
}

u32 GetATIGpuNum() {
    int iGpuNum = AtiMultiGPUAdapters();
    if (iGpuNum <= 0) return 0;
    if (iGpuNum > 1) Msg("* ATI MGPU: %d-Way CrossFire detected.", iGpuNum);
    return static_cast<u32>(iGpuNum);
}

u32 GetGpuNum() {
    u32 res = GetNVGpuNum();
    res = std::max(res, GetATIGpuNum());
    res = std::max(res, 2u);
    res = std::min(res, static_cast<u32>(CHWCaps::MAX_GPUS));
    VERIFY(res > 0);
    Msg("* Starting rendering as %u-GPU.", res);
    return res;
}
#else
u32 GetGpuNum() { return 1; }
#endif

} // namespace

#if !defined(USE_DX10) && !defined(USE_DX11)
void CHWCaps::Update() {
    D3DCAPS9 caps;
    HW.pDevice->GetDeviceCaps(&caps);

    geometry_major = static_cast<u16>((caps.VertexShaderVersion & 0xf00) >> 8);
    geometry_minor = static_cast<u16>(caps.VertexShaderVersion & 0xf);
    geometry.bSoftware = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0;
    geometry.bPointSprites = FALSE;
    geometry.bNPatches = (caps.DevCaps & D3DDEVCAPS_NPATCHES) != 0;
    
    geometry.dwRegisters = std::clamp<u32>(caps.MaxVertexShaderConst, 0, 256);
    geometry.dwInstructions = 256;
    geometry.dwClipPlanes = std::min<u32>(caps.MaxUserClipPlanes, 15);
    geometry.bVTF = (geometry_major >= 3) && HW.support(D3DFMT_R32F, D3DRTYPE_TEXTURE, D3DUSAGE_QUERY_VERTEXTEXTURE);

    raster_major = static_cast<u16>((caps.PixelShaderVersion & 0xf00) >> 8);
    raster_minor = static_cast<u16>(caps.PixelShaderVersion & 0xf);
    raster.dwStages = caps.MaxSimultaneousTextures;
    raster.bNonPow2 = ((caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0) || ((caps.TextureCaps & D3DPTEXTURECAPS_POW2) == 0);
    raster.bCubemap = (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
    raster.dwMRT_count = caps.NumSimultaneousRTs;
    raster.b_MRT_mixdepth = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS) != 0;
    raster.dwInstructions = caps.PS20Caps.NumInstructionSlots;

    Msg("* GPU shading: vs(%x/%d.%d/%d), ps(%x/%d.%d/%d)", caps.VertexShaderVersion, geometry_major,
        geometry_minor, CAP_VERSION(geometry_major, geometry_minor), caps.PixelShaderVersion,
        raster_major, raster_minor, CAP_VERSION(raster_major, raster_minor));

    ID3DQuery* q_vc;
    D3DDEVINFO_VCACHE vc;
    HRESULT _hr = HW.pDevice->CreateQuery(D3DQUERYTYPE_VCACHE, &q_vc);
    if (FAILED(_hr)) {
        vc.OptMethod = 0;
        vc.CacheSize = 16;
        geometry.dwVertexCache = 16;
    } else {
        q_vc->Issue(D3DISSUE_END);
        q_vc->GetData(&vc, sizeof(vc), D3DGETDATA_FLUSH);
        _RELEASE(q_vc);
        geometry.dwVertexCache = (1 == vc.OptMethod) ? vc.CacheSize : 16;
    }
    Msg("* GPU vertex cache: %s, %u", (1 == vc.OptMethod) ? "recognized" : "unrecognized", static_cast<u32>(geometry.dwVertexCache));

    if (0 == raster_major) geometry_major = 0;
#ifdef _EDITOR
    geometry_major = 0;
#endif

    bTableFog = FALSE;

    bStencil = FALSE;
    IDirect3DSurface9* surfZS = nullptr;
    D3DSURFACE_DESC surfDESC;
    CHK_DX(HW.pDevice->GetDepthStencilSurface(&surfZS));
    R_ASSERT(surfZS);
    CHK_DX(surfZS->GetDesc(&surfDESC));
    _RELEASE(surfZS);

    if (surfDESC.Format == D3DFMT_D15S1 || surfDESC.Format == D3DFMT_D24S8 || surfDESC.Format == D3DFMT_D24X4S4) {
        bStencil = TRUE;
    }

    bScissor = (caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0;

    u32 dwStencilCaps = caps.StencilCaps;
    if ((!(dwStencilCaps & D3DSTENCILCAPS_INCR) && !(dwStencilCaps & D3DSTENCILCAPS_INCRSAT)) ||
        (!(dwStencilCaps & D3DSTENCILCAPS_DECR) && !(dwStencilCaps & D3DSTENCILCAPS_DECRSAT))) {
        soDec = soInc = D3DSTENCILOP_KEEP;
        dwMaxStencilValue = 0;
    } else {
        soInc = (dwStencilCaps & D3DSTENCILCAPS_INCRSAT) ? D3DSTENCILOP_INCRSAT : D3DSTENCILOP_INCR;
        soDec = (dwStencilCaps & D3DSTENCILCAPS_DECRSAT) ? D3DSTENCILOP_DECRSAT : D3DSTENCILOP_DECR;
        dwMaxStencilValue = (1 << 8) - 1;
    }

    iGPUNum = GetGpuNum();
}
#else
void CHWCaps::Update() {
    geometry_major = 4;
    geometry_minor = 0;
    geometry.bSoftware = FALSE;
    geometry.bPointSprites = FALSE;
    geometry.bNPatches = FALSE;
    geometry.dwRegisters = std::clamp<u32>(256, 0, 256);
    geometry.dwInstructions = 256;
    geometry.dwClipPlanes = std::min<u32>(6, 15);
    geometry.bVTF = TRUE;

    raster_major = 4;
    raster_minor = 0;
    raster.dwStages = 16;
    raster.bNonPow2 = TRUE;
    raster.bCubemap = TRUE;
    raster.dwMRT_count = 4;
    raster.b_MRT_mixdepth = TRUE;
    raster.dwInstructions = 256;

    Msg("* GPU shading: vs(%x/%d.%d/%d), ps(%x/%d.%d/%d)", 0, geometry_major, geometry_minor,
        CAP_VERSION(geometry_major, geometry_minor), 0, raster_major, raster_minor,
        CAP_VERSION(raster_major, raster_minor));

    geometry.dwVertexCache = 24;
    Msg("* GPU vertex cache: %s, %u", "unrecognized", static_cast<u32>(geometry.dwVertexCache));

    if (0 == raster_major) geometry_major = 0;

    bTableFog = FALSE;
    bStencil = TRUE;
    bScissor = TRUE;

    soInc = D3DSTENCILOP_INCRSAT;
    soDec = D3DSTENCILOP_DECRSAT;
    dwMaxStencilValue = (1 << 8) - 1;

    iGPUNum = GetGpuNum();
}
#endif