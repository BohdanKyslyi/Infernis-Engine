#ifndef ResourceManagerH
#define ResourceManagerH
#pragma once

#include "shader.h"
#include "tss_def.h"
#include "TextureDescrManager.h"

struct lua_State;
class dx10ConstantBuffer;

class ECORE_API CResourceManager {
private:
    struct str_pred {
        inline bool operator()(LPCSTR x, LPCSTR y) const noexcept { return xr_strcmp(x, y) < 0; }
    };
    
    struct texture_detail {
        const char* T;
        R_constant_setup* cs;
    };

public:
    using map_Blender = xr_map<const char*, IBlender*, str_pred>;
    using map_Texture = xr_map<const char*, CTexture*, str_pred>;
    using map_Matrix = xr_map<const char*, CMatrix*, str_pred>;
    using map_Constant = xr_map<const char*, CConstant*, str_pred>;
    using map_RT = xr_map<const char*, CRT*, str_pred>;
    using map_VS = xr_map<const char*, SVS*, str_pred>;

#if defined(USE_DX10) || defined(USE_DX11)
    using map_GS = xr_map<const char*, SGS*, str_pred>;
#endif 

#ifdef USE_DX11
    using map_HS = xr_map<const char*, SHS*, str_pred>;
    using map_DS = xr_map<const char*, SDS*, str_pred>;
    using map_CS = xr_map<const char*, SCS*, str_pred>;
#endif

    using map_PS = xr_map<const char*, SPS*, str_pred>;
    using map_TD = xr_map<const char*, texture_detail, str_pred>;

private:
    map_Blender m_blenders;
    map_Texture m_textures;
    map_Matrix m_matrices;
    map_Constant m_constants;
    map_RT m_rtargets;
    map_VS m_vs;
    map_PS m_ps;
#if defined(USE_DX10) || defined(USE_DX11)
    map_GS m_gs;
#endif 
    map_TD m_td;

    xr_vector<SState*> v_states;
    xr_vector<SDeclaration*> v_declarations;
    xr_vector<SGeometry*> v_geoms;
    xr_vector<R_constant_table*> v_constant_tables;

#if defined(USE_DX10) || defined(USE_DX11)
    xr_vector<dx10ConstantBuffer*> v_constant_buffer;
    xr_vector<SInputSignature*> v_input_signature;
#endif 

    xr_vector<STextureList*> lst_textures;
    xr_vector<SMatrixList*> lst_matrices;
    xr_vector<SConstantList*> lst_constants;

    xr_vector<SPass*> v_passes;
    xr_vector<ShaderElement*> v_elements;
    xr_vector<Shader*> v_shaders;

    xr_vector<ref_texture> m_necessary;

public:
    CTextureDescrMngr m_textures_description;
    xr_vector<std::pair<shared_str, R_constant_setup*>> v_constant_setup;
    lua_State* LSVM{ nullptr };
    BOOL bDeferredLoad{ TRUE };

private:
    void LS_Load();
    void LS_Unload();

public:
    void _ParseList(sh_list& dest, LPCSTR names);
    [[nodiscard]] IBlender* _GetBlender(LPCSTR Name);
    [[nodiscard]] IBlender* _FindBlender(LPCSTR Name);
    void _GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps);
    void _DumpMemoryUsage();

    inline map_Blender& _GetBlenders() { return m_blenders; }

    void DBG_VerifyGeoms();
    void DBG_VerifyTextures();

    void ED_UpdateBlender(LPCSTR Name, IBlender* data);
    void ED_UpdateMatrix(LPCSTR Name, CMatrix* data);
    void ED_UpdateConstant(LPCSTR Name, CConstant* data);
#ifdef _EDITOR
    void ED_UpdateTextures(AStringVec* names);
#endif

    [[nodiscard]] CTexture* _CreateTexture(LPCSTR Name);
    void _DeleteTexture(const CTexture* T);

    [[nodiscard]] CMatrix* _CreateMatrix(LPCSTR Name);
    void _DeleteMatrix(const CMatrix* M);

    [[nodiscard]] CConstant* _CreateConstant(LPCSTR Name);
    void _DeleteConstant(const CConstant* C);

    [[nodiscard]] R_constant_table* _CreateConstantTable(R_constant_table& C);
    void _DeleteConstantTable(const R_constant_table* C);

#if defined(USE_DX10) || defined(USE_DX11)
    [[nodiscard]] dx10ConstantBuffer* _CreateConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable);
    void _DeleteConstantBuffer(const dx10ConstantBuffer* pBuffer);

    [[nodiscard]] SInputSignature* _CreateInputSignature(ID3DBlob* pBlob);
    void _DeleteInputSignature(const SInputSignature* pSignature);
#endif 

#ifdef USE_DX11
    [[nodiscard]] CRT* _CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1, bool useUAV = false);
#else
    [[nodiscard]] CRT* _CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1);
#endif
    void _DeleteRT(const CRT* RT);

#if defined(USE_DX10) || defined(USE_DX11)
    [[nodiscard]] SGS* _CreateGS(LPCSTR Name);
    void _DeleteGS(const SGS* GS);
#endif 

#ifdef USE_DX11
    [[nodiscard]] SHS* _CreateHS(LPCSTR Name);
    void _DeleteHS(const SHS* HS);

    [[nodiscard]] SDS* _CreateDS(LPCSTR Name);
    void _DeleteDS(const SDS* DS);

    [[nodiscard]] SCS* _CreateCS(LPCSTR Name);
    void _DeleteCS(const SCS* CS);
#endif 

    [[nodiscard]] SPS* _CreatePS(LPCSTR Name);
    void _DeletePS(const SPS* PS);

    [[nodiscard]] SVS* _CreateVS(LPCSTR Name);
    void _DeleteVS(const SVS* VS);

    [[nodiscard]] SPass* _CreatePass(const SPass& proto);
    void _DeletePass(const SPass* P);

    [[nodiscard]] SState* _CreateState(SimulatorStates& Code);
    void _DeleteState(const SState* SB);

    [[nodiscard]] SDeclaration* _CreateDecl(D3DVERTEXELEMENT9* dcl);
    void _DeleteDecl(const SDeclaration* dcl);

    [[nodiscard]] STextureList* _CreateTextureList(STextureList& L);
    void _DeleteTextureList(const STextureList* L);

    [[nodiscard]] SMatrixList* _CreateMatrixList(SMatrixList& L);
    void _DeleteMatrixList(const SMatrixList* L);

    [[nodiscard]] SConstantList* _CreateConstantList(SConstantList& L);
    void _DeleteConstantList(const SConstantList* L);

    [[nodiscard]] ShaderElement* _CreateElement(ShaderElement& L);
    void _DeleteElement(const ShaderElement* L);

    [[nodiscard]] Shader* _cpp_Create(LPCSTR s_shader, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
    [[nodiscard]] Shader* _cpp_Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
    [[nodiscard]] Shader* _lua_Create(LPCSTR s_shader, LPCSTR s_textures);
    BOOL _lua_HasShader(LPCSTR s_shader);

    CResourceManager() = default;
    ~CResourceManager();

    void OnDeviceCreate(IReader* F);
    void OnDeviceCreate(LPCSTR name);
    void OnDeviceDestroy(BOOL bKeepTextures);

    void reset_begin();
    void reset_end();

    [[nodiscard]] Shader* Create(LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
    [[nodiscard]] Shader* Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
    
    void Delete(const Shader* S);
    void RegisterConstantSetup(LPCSTR name, R_constant_setup* s) {
        v_constant_setup.push_back(std::make_pair(shared_str(name), s));
    }

    [[nodiscard]] SGeometry* CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
    [[nodiscard]] SGeometry* CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
    void DeleteGeom(const SGeometry* VS);
    
    void DeferredLoad(BOOL E) { bDeferredLoad = E; }
    void DeferredUpload();
    
    void Evict();
    void StoreNecessaryTextures();
    void DestroyNecessaryTextures();
    void Dump(bool bBrief);

private:
#ifdef USE_DX11
    map_DS m_ds;
    map_HS m_hs;
    map_CS m_cs;

    template <typename T> T& GetShaderMap();
    template <typename T> T* CreateShader(const char* name);
    template <typename T> void DestroyShader(const T* sh);
#endif 
};

#endif // ResourceManagerH