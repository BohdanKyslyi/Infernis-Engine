// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable : 4995)
#include <d3dx9.h>
#pragma warning(default : 4995)

#ifndef _EDITOR
#include "dxRenderDeviceRender.h"
#endif

void fix_texture_name(LPSTR fn) {
    LPSTR _ext = strext(fn);
    if (_ext && (0 == stricmp(_ext, ".tga") || 0 == stricmp(_ext, ".dds") ||
                 0 == stricmp(_ext, ".bmp") || 0 == stricmp(_ext, ".ogm")))
        *_ext = 0;
}

ENGINE_API int g_current_renderer;
#ifndef _EDITOR
ENGINE_API bool is_enough_address_space_available();
#else
bool is_enough_address_space_available() { return true; }
#endif

int get_texture_load_lod(LPCSTR fn) {
    CInifile::Sect& sect = pSettings->r_section("reduce_lod_texture_list");
    static bool enough_address_space_available = is_enough_address_space_available();

    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: C++17 Structured Bindings
    for (const auto& [key, value] : sect.Data) {
        if (strstr(fn, key.c_str())) {
            if (psTextureLOD < 1) {
                return (enough_address_space_available || (g_current_renderer < 2)) ? 0 : 1;
            } else if (psTextureLOD < 3) {
                return 1;
            } else {
                return 2;
            }
        }
    }

    if (psTextureLOD < 2) return 0;
    else if (psTextureLOD < 4) return 1;
    else return 2;
}

u32 calc_texture_size(int lod, u32 mip_cnt, u32 orig_size) {
    if (1 == mip_cnt) return orig_size;

    int _lod = lod;
    float res = static_cast<float>(orig_size);

    while (_lod > 0) {
        --_lod;
        res -= res / 1.333f;
    }
    return iFloor(res);
}

constexpr float _BUMPHEIGH = 8.f;

namespace {
constexpr u32 XR_DDS_MAGIC = MAKEFOURCC('D', 'D', 'S', ' ');
constexpr u32 XR_DDS_FOURCC = 0x00000004;
constexpr u32 XR_DDS_FOURCC_DX10 = MAKEFOURCC('D', 'X', '1', '0');
constexpr u32 XR_DDS_RESOURCE_DIMENSION_TEXTURE2D = 3;
constexpr u32 XR_DDS_RESOURCE_MISC_TEXTURECUBE = 0x00000004;
constexpr u32 XR_DDS_CAPS2_CUBEMAP_ALL_FACES = 0x0000FE00;

// Values from DXGI_FORMAT. They are kept local so the D3D9 renderers do not
// need a Direct3D 10/11 header dependency merely to inspect a DDS header.
constexpr u32 XR_DDS_DXGI_FORMAT_BC1_UNORM = 71;
constexpr u32 XR_DDS_DXGI_FORMAT_BC1_UNORM_SRGB = 72;
constexpr u32 XR_DDS_DXGI_FORMAT_BC2_UNORM = 74;
constexpr u32 XR_DDS_DXGI_FORMAT_BC2_UNORM_SRGB = 75;
constexpr u32 XR_DDS_DXGI_FORMAT_BC3_UNORM = 77;
constexpr u32 XR_DDS_DXGI_FORMAT_BC3_UNORM_SRGB = 78;

#pragma pack(push, 1)
struct SDDS_PIXELFORMAT {
    u32 size;
    u32 flags;
    u32 fourCC;
    u32 rgbBitCount;
    u32 rBitMask;
    u32 gBitMask;
    u32 bBitMask;
    u32 aBitMask;
};

struct SDDS_HEADER {
    u32 size;
    u32 flags;
    u32 height;
    u32 width;
    u32 pitchOrLinearSize;
    u32 depth;
    u32 mipMapCount;
    u32 reserved1[11];
    SDDS_PIXELFORMAT pixelFormat;
    u32 caps;
    u32 caps2;
    u32 caps3;
    u32 caps4;
    u32 reserved2;
};

struct SDDS_HEADER_DXT10 {
    u32 dxgiFormat;
    u32 resourceDimension;
    u32 miscFlag;
    u32 arraySize;
    u32 miscFlags2;
};
#pragma pack(pop)

static_assert(sizeof(SDDS_PIXELFORMAT) == 32, "Unexpected DDS pixel format size");
static_assert(sizeof(SDDS_HEADER) == 124, "Unexpected DDS header size");
static_assert(sizeof(SDDS_HEADER_DXT10) == 20, "Unexpected DX10 DDS header size");

bool prepare_dds_for_d3d9(LPCVOID sourceData, u32 sourceSize, LPCSTR textureName,
                          xr_vector<u8>& convertedData, LPCVOID& resultData, u32& resultSize) {
    resultData = sourceData;
    resultSize = sourceSize;
    convertedData.clear();

    constexpr u32 legacyHeaderSize = sizeof(u32) + sizeof(SDDS_HEADER);
    constexpr u32 dx10HeaderSize = legacyHeaderSize + sizeof(SDDS_HEADER_DXT10);

    if (!sourceData || sourceSize < legacyHeaderSize)
        return true;

    const u8* sourceBytes = static_cast<const u8*>(sourceData);
    u32 magic = 0;
    SDDS_HEADER header{};
    std::memcpy(&magic, sourceBytes, sizeof(magic));
    std::memcpy(&header, sourceBytes + sizeof(magic), sizeof(header));

    if (magic != XR_DDS_MAGIC || header.pixelFormat.fourCC != XR_DDS_FOURCC_DX10)
        return true;

    if (sourceSize < dx10HeaderSize || header.size != sizeof(SDDS_HEADER) ||
        header.pixelFormat.size != sizeof(SDDS_PIXELFORMAT) ||
        !(header.pixelFormat.flags & XR_DDS_FOURCC)) {
        Msg("! D3D9 DDS bridge: malformed DX10 header in '%s'", textureName);
        return false;
    }

    SDDS_HEADER_DXT10 header10{};
    std::memcpy(&header10, sourceBytes + legacyHeaderSize, sizeof(header10));

    if (header10.resourceDimension != XR_DDS_RESOURCE_DIMENSION_TEXTURE2D ||
        header10.arraySize != 1 ||
        (header10.miscFlag & ~XR_DDS_RESOURCE_MISC_TEXTURECUBE)) {
        Msg("! D3D9 DDS bridge: unsupported DX10 resource in '%s' "
            "(dimension=%u, array=%u, misc=0x%08x)",
            textureName, header10.resourceDimension, header10.arraySize, header10.miscFlag);
        return false;
    }

    u32 legacyFourCC = 0;
    switch (header10.dxgiFormat) {
    case XR_DDS_DXGI_FORMAT_BC1_UNORM:
    case XR_DDS_DXGI_FORMAT_BC1_UNORM_SRGB:
        legacyFourCC = MAKEFOURCC('D', 'X', 'T', '1');
        break;
    case XR_DDS_DXGI_FORMAT_BC2_UNORM:
    case XR_DDS_DXGI_FORMAT_BC2_UNORM_SRGB:
        legacyFourCC = MAKEFOURCC('D', 'X', 'T', '3');
        break;
    case XR_DDS_DXGI_FORMAT_BC3_UNORM:
    case XR_DDS_DXGI_FORMAT_BC3_UNORM_SRGB:
        legacyFourCC = MAKEFOURCC('D', 'X', 'T', '5');
        break;
    default:
        Msg("! D3D9 DDS bridge: DXGI format %u in '%s' has no lossless D3D9 mapping",
            header10.dxgiFormat, textureName);
        return false;
    }

    header.pixelFormat.fourCC = legacyFourCC;
    if (header10.miscFlag & XR_DDS_RESOURCE_MISC_TEXTURECUBE)
        header.caps2 |= XR_DDS_CAPS2_CUBEMAP_ALL_FACES;

    convertedData.resize(sourceSize - sizeof(SDDS_HEADER_DXT10));
    u8* resultBytes = &convertedData[0];
    std::memcpy(resultBytes, &magic, sizeof(magic));
    std::memcpy(resultBytes + sizeof(magic), &header, sizeof(header));
    std::memcpy(resultBytes + legacyHeaderSize, sourceBytes + dx10HeaderSize,
                sourceSize - dx10HeaderSize);

    resultData = resultBytes;
    resultSize = static_cast<u32>(convertedData.size());

#ifdef DEBUG
    Msg("* D3D9 DDS bridge: adapted DXGI format %u in '%s' without recompression",
        header10.dxgiFormat, textureName);
#endif
    return true;
}
} // namespace

//////////////////////////////////////////////////////////////////////
// Utility pack
//////////////////////////////////////////////////////////////////////
inline u32 GetPowerOf2Plus1(u32 v) {
    u32 cnt = 0;
    while (v) {
        v >>= 1;
        cnt++;
    };
    return cnt;
}

inline void Reduce(int& w, int& h, int& l, int& skip) {
    while ((l > 1) && skip) {
        w /= 2;
        h /= 2;
        l -= 1;
        skip--;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
}

void TW_Save(ID3DTexture2D* T, LPCSTR name, LPCSTR prefix, LPCSTR postfix) {
    string256 fn;
    strconcat(sizeof(fn), fn, name, "_", prefix, "-", postfix);
    for (int it = 0; it < int(xr_strlen(fn)); it++) {
        if ('\\' == fn[it]) fn[it] = '_';
    }
    string256 fn2;
    strconcat(sizeof(fn2), fn2, "debug\\", fn, ".dds");
    Log("* debug texture save: ", fn2);
    R_CHK(D3DXSaveTextureToFile(fn2, D3DXIFF_DDS, T, 0));
}

ID3DTexture2D* TW_LoadTextureFromTexture(ID3DTexture2D* t_from, D3DFORMAT& t_dest_fmt,
                                         int levels_2_skip, u32& w, u32& h) {
    ID3DTexture2D* t_dest = nullptr;
    D3DSURFACE_DESC t_from_desc0;
    R_CHK(t_from->GetLevelDesc(0, &t_from_desc0));
    
    int levels_exist = t_from->GetLevelCount();
    int top_width = t_from_desc0.Width;
    int top_height = t_from_desc0.Height;
    Reduce(top_width, top_height, levels_exist, levels_2_skip);

    if (D3DX_DEFAULT == t_dest_fmt)
        t_dest_fmt = t_from_desc0.Format;
        
    R_CHK(D3DXCreateTexture(HW.pDevice, top_width, top_height, levels_exist, 0, t_dest_fmt,
                            D3DPOOL_MANAGED, &t_dest));

    ID3DTexture2D* T_src = t_from;
    ID3DTexture2D* T_dst = t_dest;

    int L_src = T_src->GetLevelCount() - 1;
    int L_dst = T_dst->GetLevelCount() - 1;
    for (; L_dst >= 0; L_src--, L_dst--) {
        IDirect3DSurface9 *S_src = nullptr, *S_dst = nullptr;
        R_CHK(T_src->GetSurfaceLevel(L_src, &S_src));
        R_CHK(T_dst->GetSurfaceLevel(L_dst, &S_dst));

        R_CHK(D3DXLoadSurfaceFromSurface(S_dst, nullptr, nullptr, S_src, nullptr, nullptr, D3DX_FILTER_NONE, 0));

        _RELEASE(S_src);
        _RELEASE(S_dst);
    }

    w = top_width;
    h = top_height;
    return t_dest;
}

template <class _It>
inline void TW_Iterate_1OP(ID3DTexture2D* t_dst, ID3DTexture2D* t_src, const _It pred) {
    u32 mips = t_dst->GetLevelCount();
    R_ASSERT(mips == t_src->GetLevelCount());
    
    for (u32 i = 0; i < mips; ++i) {
        D3DLOCKED_RECT Rsrc, Rdst;
        D3DSURFACE_DESC desc, descS;

        t_dst->GetLevelDesc(i, &desc);
        t_src->GetLevelDesc(i, &descS);
        VERIFY(desc.Format == descS.Format);
        VERIFY(desc.Format == D3DFMT_A8R8G8B8);
        
        t_src->LockRect(i, &Rsrc, nullptr, 0);
        t_dst->LockRect(i, &Rdst, nullptr, 0);
        
        for (u32 y = 0; y < desc.Height; ++y) {
            u32* pSrcRow = reinterpret_cast<u32*>(static_cast<u8*>(Rsrc.pBits) + (y * Rsrc.Pitch));
            u32* pDstRow = reinterpret_cast<u32*>(static_cast<u8*>(Rdst.pBits) + (y * Rdst.Pitch));
            
            for (u32 x = 0; x < desc.Width; ++x) {
                pDstRow[x] = pred(pDstRow[x], pSrcRow[x]);
            }
        }
        t_dst->UnlockRect(i);
        t_src->UnlockRect(i);
    }
}

template <class _It>
inline void TW_Iterate_2OP(ID3DTexture2D* t_dst, ID3DTexture2D* t_src0, ID3DTexture2D* t_src1, const _It pred) {
    u32 mips = t_dst->GetLevelCount();
    R_ASSERT(mips == t_src0->GetLevelCount());
    R_ASSERT(mips == t_src1->GetLevelCount());
    
    for (u32 i = 0; i < mips; ++i) {
        D3DLOCKED_RECT Rsrc0, Rsrc1, Rdst;
        D3DSURFACE_DESC desc, descS0, descS1;

        t_dst->GetLevelDesc(i, &desc);
        t_src0->GetLevelDesc(i, &descS0);
        t_src1->GetLevelDesc(i, &descS1);
        VERIFY(desc.Format == descS0.Format);
        VERIFY(desc.Format == descS1.Format);
        VERIFY(desc.Format == D3DFMT_A8R8G8B8);
        
        t_src0->LockRect(i, &Rsrc0, nullptr, 0);
        t_src1->LockRect(i, &Rsrc1, nullptr, 0);
        t_dst->LockRect(i, &Rdst, nullptr, 0);
        
        for (u32 y = 0; y < desc.Height; ++y) {
            u32* pSrc0Row = reinterpret_cast<u32*>(static_cast<u8*>(Rsrc0.pBits) + (y * Rsrc0.Pitch));
            u32* pSrc1Row = reinterpret_cast<u32*>(static_cast<u8*>(Rsrc1.pBits) + (y * Rsrc1.Pitch));
            u32* pDstRow  = reinterpret_cast<u32*>(static_cast<u8*>(Rdst.pBits)  + (y * Rdst.Pitch));
            
            for (u32 x = 0; x < desc.Width; ++x) {
                pDstRow[x] = pred(pDstRow[x], pSrc0Row[x], pSrc1Row[x]);
            }
        }
        t_dst->UnlockRect(i);
        t_src0->UnlockRect(i);
        t_src1->UnlockRect(i);
    }
}

inline u32 it_gloss_rev(u32 d, u32 s) {
    return color_rgba(color_get_A(s), color_get_B(d), color_get_G(d), color_get_R(d));
}

inline u32 it_gloss_rev_base(u32 d, u32 s) {
    u32 occ = color_get_A(d) / 3;
    u32 def = 8;
    u32 gloss = (occ * 1 + def * 3) / 4;
    return color_rgba(gloss, color_get_B(d), color_get_G(d), color_get_R(d));
}

inline u32 it_difference(u32 d, u32 orig, u32 ucomp) {
    return color_rgba(128 + (static_cast<int>(color_get_R(orig)) - static_cast<int>(color_get_R(ucomp))) * 2,
                      128 + (static_cast<int>(color_get_G(orig)) - static_cast<int>(color_get_G(ucomp))) * 2,
                      128 + (static_cast<int>(color_get_B(orig)) - static_cast<int>(color_get_B(ucomp))) * 2,
                      128 + (static_cast<int>(color_get_A(orig)) - static_cast<int>(color_get_A(ucomp))) * 2);
}

inline u32 it_height_rev(u32 d, u32 s) {
    return color_rgba(color_get_A(d), color_get_B(d), color_get_G(d), color_get_R(s)); 
}

inline u32 it_height_rev_base(u32 d, u32 s) {
    return color_rgba(color_get_A(d), color_get_B(d), color_get_G(d),
                      (color_get_R(s) + color_get_G(s) + color_get_B(s)) / 3); 
}

ID3DBaseTexture* CRender::texture_load(LPCSTR fRName, u32& ret_msize) {
    ID3DTexture2D* pTexture2D = nullptr;
    IDirect3DCubeTexture9* pTextureCUBE = nullptr;
    string_path fn;
    u32 dwWidth, dwHeight;
    u32 img_size = 0;
    int img_loaded_lod = 0;
    D3DFORMAT fmt;
    u32 mip_cnt = static_cast<u32>(-1);
    D3DXIMAGE_INFO IMG{};
    xr_vector<u8> d3d9DDSData;
    LPCVOID ddsData = nullptr;
    u32 ddsSize = 0;

    R_ASSERT(fRName);
    R_ASSERT(fRName[0]);

    string_path fname;
    xr_strcpy(fname, fRName);
    fix_texture_name(fname);
    IReader* S = nullptr;
    
    if (!FS.exist(fn, "$game_textures$", fname, ".dds") && strstr(fname, "_bump"))
        goto _BUMP_from_base;
    if (FS.exist(fn, "$level$", fname, ".dds"))
        goto _DDS;
    if (FS.exist(fn, "$game_saves$", fname, ".dds"))
        goto _DDS;
    if (FS.exist(fn, "$game_textures$", fname, ".dds"))
        goto _DDS;

#ifdef _EDITOR
    ELog.Msg(mtError, "Can't find texture '%s'", fname);
    return nullptr;
#else
    Msg("! Can't find texture '%s'", fname);
    R_ASSERT(FS.exist(fn, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"));
    goto _DDS;
#endif

_DDS: {
    S = FS.r_open(fn);
#ifdef DEBUG
    Msg("* Loaded: %s[%d]b", fn, S->length());
#endif
    R_ASSERT(S);

    if (!prepare_dds_for_d3d9(S->pointer(), S->length(), fn, d3d9DDSData, ddsData, ddsSize)) {
        FS.r_close(S);
        string_path temp;
        R_ASSERT(FS.exist(temp, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"));
        R_ASSERT(xr_strcmp(temp, fn));
        xr_strcpy(fn, temp);
        goto _DDS;
    }

    img_size = ddsSize;
    {
        HRESULT const result = D3DXGetImageInfoFromFileInMemory(ddsData, ddsSize, &IMG);
        if (FAILED(result)) {
            LogMsg("! Can't get image info for texture '{}'", fn);
            FS.r_close(S);
            string_path temp;
            R_ASSERT(FS.exist(temp, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"));
            R_ASSERT(xr_strcmp(temp, fn));
            xr_strcpy(fn, temp);
            goto _DDS;
        }
    }

    if (IMG.ResourceType == D3DRTYPE_CUBETEXTURE)
        goto _DDS_CUBE;
    else
        goto _DDS_2D;

_DDS_CUBE: {
    HRESULT const result = D3DXCreateCubeTextureFromFileInMemoryEx(
        HW.pDevice, ddsData, ddsSize, D3DX_DEFAULT, IMG.MipLevels, 0, IMG.Format,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, &IMG, nullptr, &pTextureCUBE);
    FS.r_close(S);

    if (FAILED(result)) {
        Msg("! Can't load texture '%s'", fn);
        string_path temp;
        R_ASSERT(FS.exist(temp, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"));
        R_ASSERT(xr_strcmp(temp, fn));
        xr_strcpy(fn, temp);
        goto _DDS;
    }

    dwWidth = IMG.Width;
    dwHeight = IMG.Height;
    fmt = IMG.Format;
    ret_msize = calc_texture_size(img_loaded_lod, mip_cnt, img_size);
    mip_cnt = pTextureCUBE->GetLevelCount();
    return pTextureCUBE;
}

_DDS_2D: {
    strlwr(fn);
    ID3DTexture2D* T_sysmem;
    HRESULT const result = D3DXCreateTextureFromFileInMemoryEx(
        HW.pDevice, ddsData, ddsSize, D3DX_DEFAULT, D3DX_DEFAULT, IMG.MipLevels, 0,
        IMG.Format, D3DPOOL_SYSTEMMEM, D3DX_DEFAULT, D3DX_DEFAULT, 0, &IMG, nullptr, &T_sysmem);
    FS.r_close(S);

    if (FAILED(result)) {
        Msg("! Can't load texture '%s'", fn);
        string_path temp;
        R_ASSERT(FS.exist(temp, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"));
        strlwr(temp);
        R_ASSERT(xr_strcmp(temp, fn));
        xr_strcpy(fn, temp);
        goto _DDS;
    }

    img_loaded_lod = get_texture_load_lod(fn);
    pTexture2D = TW_LoadTextureFromTexture(T_sysmem, IMG.Format, img_loaded_lod, dwWidth, dwHeight);
    mip_cnt = pTexture2D->GetLevelCount();
    _RELEASE(T_sysmem);

    fmt = IMG.Format;
    ret_msize = calc_texture_size(img_loaded_lod, mip_cnt, img_size);
    return pTexture2D;
}
}

_BUMP_from_base: {
    Msg("! auto-generated bump map: %s", fname);
#ifndef _EDITOR
    if (strstr(fname, "_bump#")) {
        R_ASSERT2(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump#", ".dds"), "ed_dummy_bump#");
        S = FS.r_open(fn);
        R_ASSERT2(S, fn);
        ddsData = S->pointer();
        ddsSize = S->length();
        img_size = ddsSize;
        R_CHK2(D3DXGetImageInfoFromFileInMemory(ddsData, ddsSize, &IMG), fn);
        goto _DDS_2D;
    }
    if (strstr(fname, "_bump")) {
        R_ASSERT2(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump", ".dds"), "ed_dummy_bump");
        S = FS.r_open(fn);
        R_ASSERT2(S, fn);
        ddsData = S->pointer();
        ddsSize = S->length();
        img_size = ddsSize;
        R_CHK2(D3DXGetImageInfoFromFileInMemory(ddsData, ddsSize, &IMG), fn);
        goto _DDS_2D;
    }
#endif

    *strstr(fname, "_bump") = 0;
    R_ASSERT2(FS.exist(fn, "$game_textures$", fname, ".dds"), fname);

    S = FS.r_open(fn);
    R_ASSERT(S);

    if (!prepare_dds_for_d3d9(S->pointer(), S->length(), fn, d3d9DDSData, ddsData, ddsSize)) {
        FS.r_close(S);
        return nullptr;
    }

    img_size = ddsSize;
    ID3DTexture2D* T_base = nullptr;
    
    R_CHK2(D3DXCreateTextureFromFileInMemoryEx(
               HW.pDevice, ddsData, ddsSize, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0,
               D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, D3DX_DEFAULT, D3DX_DEFAULT, 0, &IMG, nullptr, &T_base),
           fn);
    FS.r_close(S);

    ID3DTexture2D* T_normal_1 = nullptr;
    R_CHK(D3DXCreateTexture(HW.pDevice, IMG.Width, IMG.Height, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8,
                            D3DPOOL_SYSTEMMEM, &T_normal_1));
    R_CHK(D3DXComputeNormalMap(T_normal_1, T_base, nullptr, D3DX_NORMALMAP_COMPUTE_OCCLUSION,
                               D3DX_CHANNEL_LUMINANCE, _BUMPHEIGH));

    TW_Iterate_1OP(T_normal_1, T_base, it_gloss_rev_base);

    fmt = D3DFMT_DXT5;
    img_loaded_lod = get_texture_load_lod(fn);
    ID3DTexture2D* T_normal_1C =
        TW_LoadTextureFromTexture(T_normal_1, fmt, img_loaded_lod, dwWidth, dwHeight);
    mip_cnt = T_normal_1C->GetLevelCount();

#if RENDER == R_R2
    fmt = D3DFMT_A8R8G8B8;
    ID3DTexture2D* T_normal_1U = TW_LoadTextureFromTexture(T_normal_1C, fmt, 0, dwWidth, dwHeight);

    ID3DTexture2D* T_normal_1D = nullptr;
    R_CHK(D3DXCreateTexture(HW.pDevice, dwWidth, dwHeight, T_normal_1U->GetLevelCount(), 0,
                            D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &T_normal_1D));
    TW_Iterate_2OP(T_normal_1D, T_normal_1, T_normal_1U, it_difference);

    TW_Iterate_1OP(T_normal_1D, T_base, it_height_rev_base);

    fmt = D3DFMT_DXT5;
    ID3DTexture2D* T_normal_2C = TW_LoadTextureFromTexture(T_normal_1D, fmt, 0, dwWidth, dwHeight);
    
    _RELEASE(T_normal_1U);
    _RELEASE(T_normal_1D);

    string256 fnameB;
    strconcat(sizeof(fnameB), fnameB, "$user$", fname, "_bumpX");
    ref_texture t_temp = dxRenderDeviceRender::Instance().Resources->_CreateTexture(fnameB);
    t_temp->surface_set(T_normal_2C);
    
    _RELEASE(T_normal_2C); 
#endif

    _RELEASE(T_base);
    _RELEASE(T_normal_1);
    ret_msize = calc_texture_size(img_loaded_lod, mip_cnt, img_size);
    return T_normal_1C;
}
}
