#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"

float r__dtex_range = 50.f;

class cl_dt_scaler : public R_constant_setup {
public:
    float scale;

    cl_dt_scaler(float s) : scale(s) {}
    void setup(R_constant* C) override { 
        RCache.set_c(C, scale, scale, scale, 1.f / r__dtex_range); 
    }
};

namespace {
constexpr LPCSTR texture_ltx_section = "texture";
constexpr LPCSTR texture_ltx_legacy_section = "texture_material";

void fix_texture_description_name(LPSTR fn) {
    LPSTR _ext = strext(fn);
    if (_ext &&
        (0 == stricmp(_ext, ".tga") || 0 == stricmp(_ext, ".thm") ||
         0 == stricmp(_ext, ".ltx") || 0 == stricmp(_ext, ".dds") ||
         0 == stricmp(_ext, ".bmp") || 0 == stricmp(_ext, ".ogm")))
        *_ext = 0;
}

LPCSTR find_texture_ltx_section(const CInifile& ini) {
    if (ini.section_exist(texture_ltx_section))
        return texture_ltx_section;
    if (ini.section_exist(texture_ltx_legacy_section))
        return texture_ltx_legacy_section;
    return nullptr;
}

bool read_texture_type(LPCSTR value, STextureParams::ETType& result) {
    if (0 == stricmp(value, "image"))
        result = STextureParams::ttImage;
    else if (0 == stricmp(value, "cube_map") || 0 == stricmp(value, "cubemap"))
        result = STextureParams::ttCubeMap;
    else if (0 == stricmp(value, "bump_map") || 0 == stricmp(value, "bump"))
        result = STextureParams::ttBumpMap;
    else if (0 == stricmp(value, "normal_map") || 0 == stricmp(value, "normal"))
        result = STextureParams::ttNormalMap;
    else if (0 == stricmp(value, "terrain"))
        result = STextureParams::ttTerrain;
    else
        return false;
    return true;
}

bool read_texture_material(LPCSTR value, STextureParams::ETMaterial& result) {
    if (0 == stricmp(value, "oren_nayar_blin") || 0 == stricmp(value, "oren_nayar_blinn"))
        result = STextureParams::tmOrenNayar_Blin;
    else if (0 == stricmp(value, "blin_phong") || 0 == stricmp(value, "blinn_phong"))
        result = STextureParams::tmBlin_Phong;
    else if (0 == stricmp(value, "phong_metal"))
        result = STextureParams::tmPhong_Metal;
    else if (0 == stricmp(value, "metal_oren_nayar"))
        result = STextureParams::tmMetal_OrenNayar;
    else if (0 == stricmp(value, "pbr"))
        result = STextureParams::tmPBR_Material;
    else
        return false;
    return true;
}

bool read_bump_mode(LPCSTR value, STextureParams::ETBumpMode& result) {
    if (0 == stricmp(value, "none"))
        result = STextureParams::tbmNone;
    else if (0 == stricmp(value, "use") || 0 == stricmp(value, "bump"))
        result = STextureParams::tbmUse;
    else if (0 == stricmp(value, "parallax") || 0 == stricmp(value, "use_parallax"))
        result = STextureParams::tbmUseParallax;
    else
        return false;
    return true;
}
} // namespace

void CTextureDescrMngr::LoadTHM(LPCSTR initial, map_TP& texture_params) {
    FS_FileSet flist;
    FS.file_list(flist, initial, FS_ListFiles, "*.thm");
    
#ifdef DEBUG
    Msg("count of .thm files=%d", flist.size());
#endif

    STextureParams tp;
    string_path fn;

    // C++11 Range-based for loop
    for (const auto& file : flist) {
        FS.update_path(fn, initial, file.name.c_str());
        
        IReader* F = FS.r_open(fn);
        if (!F) continue; // Захист від відсутніх файлів

        xr_strcpy(fn, file.name.c_str());
        fix_texture_description_name(fn);

        // МАКСИМАЛЬНА БЕЗПЕКА: Замість вильоту гри (R_ASSERT), просто пропускаємо битий файл
        if (!F->find_chunk(THM_CHUNK_TYPE)) {
            FS.r_close(F);
            continue;
        }

        F->r_u32();
        tp.Clear();
        tp.Load(*F);
        FS.r_close(F);

        texture_params[fn] = tp;
    }
}

void CTextureDescrMngr::LoadLTX(LPCSTR initial, map_TP& texture_params) {
    FS_FileSet flist;
    FS.file_list(flist, initial, FS_ListFiles, "*.ltx");

#ifdef DEBUG
    Msg("count of texture .ltx files=%d", flist.size());
#endif

    string_path full_path;
    string_path texture_name;

    for (const auto& file : flist) {
        FS.update_path(full_path, initial, file.name.c_str());
        CInifile ini(full_path, TRUE, TRUE, FALSE);
        LPCSTR section = find_texture_ltx_section(ini);
        if (!section)
            continue;

        xr_strcpy(texture_name, file.name.c_str());
        fix_texture_description_name(texture_name);
        STextureParams& tp = texture_params[texture_name];

        if (ini.line_exist(section, "type")) {
            LPCSTR value = ini.r_string(section, "type");
            if (!read_texture_type(value, tp.type))
                Msg("! Texture LTX [%s]: unknown type [%s]", texture_name, value);
        }

        if (ini.line_exist(section, "material")) {
            LPCSTR value = ini.r_string(section, "material");
            if (!read_texture_material(value, tp.material))
                Msg("! Texture LTX [%s]: unknown material [%s]", texture_name, value);
        }
        if (ini.line_exist(section, "material_weight"))
            tp.material_weight = clampr(ini.r_float(section, "material_weight"), 0.f, 1.f);

        if (ini.line_exist(section, "detail_texture"))
            tp.detail_name = ini.r_string(section, "detail_texture");
        if (ini.line_exist(section, "detail_scale"))
            tp.detail_scale = std::max(ini.r_float(section, "detail_scale"), EPS_S);
        if (ini.line_exist(section, "detail_diffuse"))
            tp.flags.set(STextureParams::flDiffuseDetail,
                         ini.r_bool(section, "detail_diffuse"));
        if (ini.line_exist(section, "detail_bump"))
            tp.flags.set(STextureParams::flBumpDetail,
                         ini.r_bool(section, "detail_bump"));

        if (ini.line_exist(section, "bump_mode")) {
            LPCSTR value = ini.r_string(section, "bump_mode");
            if (!read_bump_mode(value, tp.bump_mode))
                Msg("! Texture LTX [%s]: unknown bump mode [%s]", texture_name, value);
        }
        if (ini.line_exist(section, "bump_texture"))
            tp.bump_name = ini.r_string(section, "bump_texture");
    }
}

void CTextureDescrMngr::ApplyTextureParams(const map_TP& texture_params) {
    m_texture_details.clear();

    for (const auto& [texture_name, tp] : texture_params) {
        if (STextureParams::ttImage != tp.type && STextureParams::ttTerrain != tp.type &&
            STextureParams::ttNormalMap != tp.type)
            continue;

        texture_desc& desc = m_texture_details[texture_name];

        if (tp.detail_name.size() &&
            tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail)) {
            desc.bHasAssoc = true;
            desc.assoc.detail_name = tp.detail_name;
            desc.assoc.usage = 0;

            if (tp.flags.is(STextureParams::flDiffuseDetail))
                desc.assoc.usage |= (1 << 0);
            if (tp.flags.is(STextureParams::flBumpDetail))
                desc.assoc.usage |= (1 << 1);

            cl_dt_scaler*& dts = m_detail_scalers[texture_name];
            if (dts) {
                dts->scale = tp.detail_scale;
            } else {
                dts = xr_new<cl_dt_scaler>(tp.detail_scale);
            }
        }

        desc.bHasSpec = true;
        const bool isPBR = tp.material == STextureParams::tmPBR_Material;

        // The legacy renderers address a four-slice material LUT. PBR is a
        // texture-layout marker, not a fifth LUT slice, so give R2/R3 a
        // defined Blinn fallback instead of letting material 4 wrap around.
        desc.spec.m_material = isPBR
            ? float(STextureParams::tmBlin_Phong)
            : tp.material + tp.material_weight;
        desc.spec.m_use_steep_parallax = false;
        desc.spec.m_use_pbr = isPBR;

        if (tp.bump_mode == STextureParams::tbmUse) {
            desc.spec.m_bump_name = tp.bump_name;
        } else if (tp.bump_mode == STextureParams::tbmUseParallax) {
            desc.spec.m_bump_name = tp.bump_name;
            desc.spec.m_use_steep_parallax = true;
        }
    }
}

void CTextureDescrMngr::Load() {
#ifdef DEBUG
    CTimer TT;
    TT.Start();
#endif

    map_TP texture_params;

    // THM remains the compatibility baseline. Sidecar LTX files are applied
    // afterwards and therefore override only the keys explicitly present in
    // them. A texture can also be described entirely by LTX without a THM.
    LoadTHM("$game_textures$", texture_params);
    LoadTHM("$level$", texture_params);
    LoadLTX("$game_textures$", texture_params);
    LoadLTX("$level$", texture_params);
    ApplyTextureParams(texture_params);

#ifdef DEBUG
    Msg("load time=%d ms", TT.GetElapsed_ms());
#endif
}

void CTextureDescrMngr::UnLoad() {
    // Всі структури тепер живуть всередині map і знищуються автоматично!
    // Ми позбулися тисяч xr_delete, що радикально прискорює вивантаження рівня.
    m_texture_details.clear();
}

CTextureDescrMngr::~CTextureDescrMngr() {
    // C++17 Structured Bindings
    for (auto& [key, scaler] : m_detail_scalers) {
        xr_delete(scaler);
    }
    m_detail_scalers.clear();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasSpec) {
        return it->second.spec.m_bump_name;
    }
    return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasSpec) {
        return it->second.spec.m_use_steep_parallax;
    }
    return FALSE;
}

BOOL CTextureDescrMngr::UsePBRTextures(const shared_str& tex_name) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasSpec) {
        return it->second.spec.m_use_pbr;
    }
    return FALSE;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasSpec) {
        return it->second.spec.m_material;
    }
    return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasAssoc) {
        const u8 usage = it->second.assoc.usage;
        bDiffuse = !!(usage & (1 << 0));
        bBump = !!(usage & (1 << 1));
    }
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup*& CS) const {
    const auto it = m_texture_details.find(tex_name);
    if (it != m_texture_details.end() && it->second.bHasAssoc) {
        res = it->second.assoc.detail_name.c_str();
        const auto it2 = m_detail_scalers.find(tex_name);
        CS = (it2 != m_detail_scalers.end()) ? it2->second : nullptr;
        return TRUE;
    }
    return FALSE;
}
