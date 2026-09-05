#ifndef _TextureDescrManager_included_
#define _TextureDescrManager_included_
#pragma once

#include "ETextureParams.h"

class cl_dt_scaler;

class CTextureDescrMngr {
    struct texture_assoc {
        shared_str detail_name;
        u8 usage{ 0 };
    };

    struct texture_spec {
        shared_str m_bump_name;
        float m_material{ 1.0f };
        bool m_use_steep_parallax{ false };
        bool m_use_pbr{ false };
    };

    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Inline-зберігання даних замість вказівників.
    // Це економить тисячі викликів xr_new / xr_delete при завантаженні рівнів!
    struct texture_desc {
        texture_assoc assoc;
        texture_spec spec;
        bool bHasAssoc{ false };
        bool bHasSpec{ false };
    };

    using map_TD = xr_map<shared_str, texture_desc>;
    using map_CS = xr_map<shared_str, cl_dt_scaler*>;

    map_TD m_texture_details;
    map_CS m_detail_scalers;

    void LoadTHM(LPCSTR initial);

public:
    CTextureDescrMngr() = default;
    ~CTextureDescrMngr();
    
    void Load();
    void UnLoad();

public:
    [[nodiscard]] shared_str GetBumpName(const shared_str& tex_name) const;
    [[nodiscard]] float GetMaterial(const shared_str& tex_name) const;
    [[nodiscard]] BOOL UseSteepParallax(const shared_str& tex_name) const;
    [[nodiscard]] BOOL UsePBRTextures(const shared_str& tex_name) const;
    
    void GetTextureUsage(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const;
    BOOL GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup*& CS) const;
};

#endif // _TextureDescrManager_included_
