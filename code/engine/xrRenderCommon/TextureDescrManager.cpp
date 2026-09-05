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

void fix_texture_thm_name(LPSTR fn) {
    LPSTR _ext = strext(fn);
    if (_ext &&
        (0 == stricmp(_ext, ".tga") || 0 == stricmp(_ext, ".thm") || 0 == stricmp(_ext, ".dds") ||
         0 == stricmp(_ext, ".bmp") || 0 == stricmp(_ext, ".ogm")))
        *_ext = 0;
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial) {
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
        fix_texture_thm_name(fn);

        // МАКСИМАЛЬНА БЕЗПЕКА: Замість вильоту гри (R_ASSERT), просто пропускаємо битий файл
        if (!F->find_chunk(THM_CHUNK_TYPE)) {
            FS.r_close(F);
            continue;
        }

        F->r_u32();
        tp.Clear();
        tp.Load(*F);
        FS.r_close(F);

        if (STextureParams::ttImage == tp.type || STextureParams::ttTerrain == tp.type || STextureParams::ttNormalMap == tp.type) {
            texture_desc& desc = m_texture_details[fn];

            if (tp.detail_name.size() && tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail)) {
                desc.bHasAssoc = true;
                desc.assoc.detail_name = tp.detail_name;
                desc.assoc.usage = 0;

                if (tp.flags.is(STextureParams::flDiffuseDetail)) desc.assoc.usage |= (1 << 0);
                if (tp.flags.is(STextureParams::flBumpDetail))    desc.assoc.usage |= (1 << 1);

                cl_dt_scaler*& dts = m_detail_scalers[fn];
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
}

void CTextureDescrMngr::Load() {
#ifdef DEBUG
    CTimer TT;
    TT.Start();
#endif

    LoadTHM("$game_textures$");
    LoadTHM("$level$");

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
