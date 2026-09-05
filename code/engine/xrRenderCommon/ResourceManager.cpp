#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable : 4995)
#include <d3dx9.h>
#pragma warning(default : 4995)

#include "ResourceManager.h"
#include "tss.h"
#include "blenders\blender.h"
#include "blenders\blender_recorder.h"
#include <algorithm>

void fix_texture_name(LPSTR fn);

template <class T>
inline BOOL reclaim(xr_vector<T*>& vec, const T* ptr) {
    auto it = std::find(vec.begin(), vec.end(), ptr);
    if (it != vec.end()) {
        vec.erase(it);
        return TRUE;
    }
    return FALSE;
}

IBlender* CResourceManager::_GetBlender(LPCSTR Name) {
    R_ASSERT(Name && Name[0]);
    const auto it = m_blenders.find(Name);

#ifdef _EDITOR
    if (it == m_blenders.end()) return nullptr;
#else
#if defined(USE_DX10) || defined(USE_DX11)
    if (it == m_blenders.end()) {
        Msg("DX10: Shader '%s' not found in library.", Name);
        return nullptr;
    }
#endif
    if (it == m_blenders.end()) {
        Debug.fatal(DEBUG_INFO, "Shader '%s' not found in library.", Name);
        return nullptr;
    }
#endif
    return it->second;
}

IBlender* CResourceManager::_FindBlender(LPCSTR Name) {
    if (!Name || !Name[0]) return nullptr;
    const auto it = m_blenders.find(Name);
    return (it != m_blenders.end()) ? it->second : nullptr;
}

void CResourceManager::ED_UpdateBlender(LPCSTR Name, IBlender* data) {
    const auto it = m_blenders.find(Name);
    if (it != m_blenders.end()) {
        R_ASSERT(data->getDescription().CLS == it->second->getDescription().CLS);
        xr_delete(it->second);
        it->second = data;
    } else {
        m_blenders.insert(std::make_pair(xr_strdup(Name), data));
    }
}

void CResourceManager::_ParseList(sh_list& dest, LPCSTR names) {
    if (!names || !names[0]) names = "$null";

    std::memset(&dest, 0, sizeof(dest));
    const char* P = names;
    svector<char, 128> N;

    while (*P) {
        if (*P == ',') {
            N.push_back(0);
            strlwr(N.begin());
            fix_texture_name(N.begin());
            dest.push_back(N.begin());
            N.clear();
        } else {
            N.push_back(*P);
        }
        P++;
    }
    if (!N.empty()) {
        N.push_back(0);
        strlwr(N.begin());
        fix_texture_name(N.begin());
        dest.push_back(N.begin());
    }
}

ShaderElement* CResourceManager::_CreateElement(ShaderElement& S) {
    if (S.passes.empty()) return nullptr;

    for (auto* element : v_elements) {
        if (S.equal(*element)) return element;
    }

    ShaderElement* N = xr_new<ShaderElement>(S);
    N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_elements.push_back(N);
    return N;
}

void CResourceManager::_DeleteElement(const ShaderElement* S) {
    if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    if (reclaim(v_elements, S)) return;
    Msg("! ERROR: Failed to find compiled 'shader-element'");
}

Shader* CResourceManager::_cpp_Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices) {
    CBlender_Compile C;
    Shader S;

    C.BT = B;
    C.bEditor = FALSE;
    C.bDetail = FALSE;
#ifdef _EDITOR
    if (!C.BT) {
        ELog.Msg(mtError, "Can't find shader '%s'", s_shader);
        return nullptr;
    }
    C.bEditor = TRUE;
#endif

    _ParseList(C.L_textures, s_textures);
    _ParseList(C.L_constants, s_constants);
    _ParseList(C.L_matrices, s_matrices);

    auto CompileElement = [&](int element_id, bool apply_detail) {
        C.iElement = element_id;
        if (apply_detail) {
            C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
        } else {
            C.bDetail = FALSE;
        }
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[element_id] = _CreateElement(E);
    };

    CompileElement(0, true);
    CompileElement(1, true);
    CompileElement(2, false);
    CompileElement(3, false);
    
    C.iElement = 4;
    C.bDetail = TRUE; // HACK
    ShaderElement E4;
    C._cpp_Compile(&E4);
    S.E[4] = _CreateElement(E4);

    CompileElement(5, false);

    for (auto* shader : v_shaders) {
        if (S.equal(shader)) return shader;
    }

    Shader* N = xr_new<Shader>(S);
    N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_shaders.push_back(N);
    return N;
}

Shader* CResourceManager::_cpp_Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices) {
#if defined(USE_DX10) || defined(USE_DX11)
    IBlender* pBlender = _GetBlender(s_shader ? s_shader : "null");
    if (!pBlender) return nullptr;
    return _cpp_Create(pBlender, s_shader, s_textures, s_constants, s_matrices);
#else
    return _cpp_Create(_GetBlender(s_shader ? s_shader : "null"), s_shader, s_textures, s_constants, s_matrices);
#endif
}

Shader* CResourceManager::Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices) {
    return _cpp_Create(B, s_shader, s_textures, s_constants, s_matrices);
}

Shader* CResourceManager::Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices) {
#if defined(USE_DX10) || defined(USE_DX11)
    if (_lua_HasShader(s_shader))
        return _lua_Create(s_shader, s_textures);
    else {
        Shader* pShader = _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
        if (pShader) return pShader;
        
        if (_lua_HasShader("stub_default"))
            return _lua_Create("stub_default", s_textures);
            
        FATAL("Can't find stub_default.s");
        return nullptr;
    }
#else 
#ifndef _EDITOR
    if (_lua_HasShader(s_shader)) return _lua_Create(s_shader, s_textures);
#endif
    return _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
#endif 
}

void CResourceManager::Delete(const Shader* S) {
    if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    if (reclaim(v_shaders, S)) return;
    Msg("! ERROR: Failed to find complete shader");
}

void CResourceManager::DeferredUpload() {
    if (!RDEVICE.b_is_Ready) return;
    
    for (const auto& [name, tex] : m_textures) {
        tex->Load();
    }
}

#ifdef _EDITOR
void CResourceManager::ED_UpdateTextures(AStringVec* names) {
    if (names) {
        for (const auto& name : *names) {
            const auto it = m_textures.find(name.c_str());
            if (it != m_textures.end()) it->second->Unload();
        }
    } else {
        for (const auto& [name, tex] : m_textures) {
            tex->Unload();
        }
    }
}
#endif

void CResourceManager::_GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps) {
    m_base = c_base = m_lmaps = c_lmaps = 0;

    for (const auto& [name, tex] : m_textures) {
        u32 m = tex->flags.MemoryUsage;
        if (strstr(name, "lmap")) {
            c_lmaps++;
            m_lmaps += m;
        } else {
            c_base++;
            m_base += m;
        }
    }
}

void CResourceManager::_DumpMemoryUsage() {
    xr_multimap<u32, std::pair<u32, shared_str>> mtex;

    for (const auto& [name, tex] : m_textures) {
        // Копіюємо бітове поле у звичайну змінну, бо C++ забороняє брати посилання на бітові поля
        u32 mem_usage = tex->flags.MemoryUsage; 
        mtex.insert(std::make_pair(mem_usage, std::make_pair(tex->dwReference, tex->cName)));
    }

    for (const auto& [mem, info] : mtex) {
        Msg("* %4.1f : [%4d] %s", static_cast<float>(mem) / 1024.f, info.first, info.second.c_str());
    }
}

void CResourceManager::Evict() {
#if !defined(USE_DX10) && !defined(USE_DX11)
    CHK_DX(HW.pDevice->EvictManagedResources());
#endif
}