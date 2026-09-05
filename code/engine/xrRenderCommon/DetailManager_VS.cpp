#include "stdafx.h"
#include "detailmanager.h"
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include "../xrRenderDX10/dx10BufferUtils.h"
#include <algorithm> 

constexpr int quant = 16384;
constexpr int c_hdr = 10;
constexpr int c_size = 4;

static D3DVERTEXELEMENT9 dwDecl[] = {
    { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },  // pos
    { 0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, // uv
    D3DDECL_END()
};

#pragma pack(push, 1)
struct vertHW {
    float x, y, z;
    short u, v, t, mid;
};
#pragma pack(pop)

inline short QC(float v) {
    int t = iFloor(v * static_cast<float>(quant));
    t = std::clamp(t, -32768, 32767);
    return static_cast<short>(t & 0xffff);
}

void CDetailManager::hw_Load() {
    hw_Load_Geom();
    hw_Load_Shaders();
}

void CDetailManager::hw_Load_Geom() {
    hw_BatchSize = (static_cast<u32>(HW.Caps.geometry.dwRegisters) - c_hdr) / c_size;
    hw_BatchSize = std::clamp(hw_BatchSize, 0u, 64u);
    Msg("* [DETAILS] VertexConsts(%d), Batch(%d)", static_cast<u32>(HW.Caps.geometry.dwRegisters), hw_BatchSize);

    u32 dwVerts = 0;
    u32 dwIndices = 0;
    for (const auto* obj : objects) {
        const CDetail& D = *obj;
        dwVerts += D.number_vertices * hw_BatchSize;
        dwIndices += D.number_indices * hw_BatchSize;
    }
    const u32 vSize = sizeof(vertHW);
    Msg("* [DETAILS] %d v(%d), %d p", dwVerts, vSize, dwIndices / 3);

#if !defined(USE_DX10) && !defined(USE_DX11)
    const u32 dwUsage = D3DUSAGE_WRITEONLY;

    R_CHK(HW.pDevice->CreateVertexBuffer(dwVerts * vSize, dwUsage, 0, D3DPOOL_MANAGED, &hw_VB, 0));
    HW.stats_manager.increment_stats_vb(hw_VB);
    R_CHK(HW.pDevice->CreateIndexBuffer(dwIndices * 2, dwUsage, D3DFMT_INDEX16, D3DPOOL_MANAGED, &hw_IB, 0));
    HW.stats_manager.increment_stats_ib(hw_IB);
#endif

    Msg("* [DETAILS] Batch(%d), VB(%dK), IB(%dK)", hw_BatchSize, (dwVerts * vSize) / 1024, (dwIndices * 2) / 1024);

    // Fill VB
    {
        vertHW* pV;
#if defined(USE_DX10) || defined(USE_DX11)
        vertHW* pVOriginal = xr_alloc<vertHW>(dwVerts);
        pV = pVOriginal;
#else  
        R_CHK(hw_VB->Lock(0, 0, (void**)&pV, 0));
#endif 
        for (const auto* obj : objects) {
            const CDetail& D = *obj;
            for (u32 batch = 0; batch < hw_BatchSize; batch++) {
                const u32 mid = batch * c_size;
                for (u32 v = 0; v < D.number_vertices; v++) {
                    const Fvector& vP = D.vertices[v].P;
                    pV->x = vP.x;
                    pV->y = vP.y;
                    pV->z = vP.z;
                    pV->u = QC(D.vertices[v].u);
                    pV->v = QC(D.vertices[v].v);
                    pV->t = QC(vP.y / (D.bv_bb.max.y - D.bv_bb.min.y));
                    pV->mid = static_cast<short>(mid);
                    pV++;
                }
            }
        }
#if defined(USE_DX10) || defined(USE_DX11)
        R_CHK(dx10BufferUtils::CreateVertexBuffer(&hw_VB, pVOriginal, dwVerts * vSize));
        HW.stats_manager.increment_stats_vb(hw_VB);
        xr_free(pVOriginal);
#else 
        R_CHK(hw_VB->Unlock());
#endif 
    }

    // Fill IB
    {
        u16* pI;
#if defined(USE_DX10) || defined(USE_DX11)
        u16* pIOriginal = xr_alloc<u16>(dwIndices);
        pI = pIOriginal;
#else  
        R_CHK(hw_IB->Lock(0, 0, (void**)(&pI), 0));
#endif 
        for (const auto* obj : objects) {
            const CDetail& D = *obj;
            u16 offset = 0;
            for (u32 batch = 0; batch < hw_BatchSize; batch++) {
                for (u32 i = 0; i < static_cast<u32>(D.number_indices); i++)
                    *pI++ = static_cast<u16>(static_cast<u16>(D.indices[i]) + static_cast<u16>(offset));
                offset = static_cast<u16>(offset + static_cast<u16>(D.number_vertices));
            }
        }
#if defined(USE_DX10) || defined(USE_DX11)
        R_CHK(dx10BufferUtils::CreateIndexBuffer(&hw_IB, pIOriginal, dwIndices * 2));
        HW.stats_manager.increment_stats_ib(hw_IB);
        xr_free(pIOriginal);
#else 
        R_CHK(hw_IB->Unlock());
#endif 
    }

    hw_Geom.create(dwDecl, hw_VB, hw_IB);
}

void CDetailManager::hw_Unload() {
    hw_Geom.destroy();
    HW.stats_manager.decrement_stats_vb(hw_VB);
    HW.stats_manager.decrement_stats_ib(hw_IB);
    _RELEASE(hw_IB);
    _RELEASE(hw_VB);
}

#if !defined(USE_DX10) && !defined(USE_DX11)
void CDetailManager::hw_Load_Shaders() {
    ref_shader S;
    S.create("details\\set");
    R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
    R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
    hwc_consts = T0.get("consts");
    hwc_wave = T0.get("wave");
    hwc_wind = T0.get("dir2D");
    hwc_array = T0.get("array");
    hwc_s_consts = T1.get("consts");
    hwc_s_xform = T1.get("xform");
    hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render() {
    float fDelta = RDEVICE.fTimeGlobal - m_global_time_old;
    if ((fDelta < 0.f) || (fDelta > 1.f)) fDelta = 0.03f;
    m_global_time_old = RDEVICE.fTimeGlobal;

    m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
    m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
    m_time_pos += fDelta * swing_current.speed;

    const float tm_rot1 = m_time_rot_1;
    const float tm_rot2 = m_time_rot_2;

    Fvector4 dir1, dir2;
    dir1.set(std::sin(tm_rot1), 0.f, std::cos(tm_rot1), 0.f).normalize().mul(swing_current.amp1);
    dir2.set(std::sin(tm_rot2), 0.f, std::cos(tm_rot2), 0.f).normalize().mul(swing_current.amp2);

    RCache.set_Geometry(hw_Geom);

    const float scale = 1.f / static_cast<float>(quant);
    Fvector4 wave;
    
    // Wave0
    wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
    RCache.set_c(&*hwc_consts, scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);         
    RCache.set_c(&*hwc_wave, wave.div(PI_MUL_2)); 
    RCache.set_c(&*hwc_wind, dir1);               
    hw_Render_dump(&*hwc_array, 1, 0, c_hdr);

    // Wave1
    wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
    RCache.set_c(&*hwc_wave, wave.div(PI_MUL_2)); 
    RCache.set_c(&*hwc_wind, dir2);               
    hw_Render_dump(&*hwc_array, 2, 0, c_hdr);

    // Still
    RCache.set_c(&*hwc_s_consts, scale, scale, scale, 1.f);
    RCache.set_c(&*hwc_s_xform, RDEVICE.mFullTransform);
    hw_Render_dump(&*hwc_s_array, 0, 1, c_hdr);
}

void CDetailManager::hw_Render_dump(ref_constant x_array, u32 var_id, u32 lod_id, u32 c_offset) {
    RDEVICE.Statistic->RenderDUMP_DT_Count = 0;

    u32 vOffset = 0;
    u32 iOffset = 0;

    vis_list& list = m_visibles[var_id];

    Fvector c_sun, c_ambient, c_hemi;
#ifndef _EDITOR
    CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
    c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
    c_sun.mul(0.5f);
    c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
    c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);
#else
    c_sun.set(1.f, 1.f, 1.f);
    c_sun.mul(0.5f);
    c_ambient.set(1.f, 1.f, 1.f);
    c_hemi.set(1.f, 1.f, 1.f);
#endif

    VERIFY(objects.size() <= list.size());

    for (u32 O = 0; O < objects.size(); O++) {
        CDetail& Object = *objects[O];
        xr_vector<SlotItemVec*>& vis = list[O];
        
        if (!vis.empty()) {
            RCache.set_Element(Object.shader->E[lod_id]);
            RImplementation.apply_lmaterial();
            const u32 c_base = x_array->vs.index;
            Fvector4* c_storage = RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);

            u32 dwBatch = 0;
            const u32 verts_per_obj = Object.number_vertices;
            const u32 prims_per_obj = Object.number_indices / 3;

            for (auto* items : vis) {
                for (auto* item_ptr : *items) {
                    const SlotItem& Instance = *item_ptr;
                    const u32 base = dwBatch * 4;

                    const float scale = Instance.scale_calculated;
                    const Fmatrix& M = Instance.mRotY;
                    c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
                    c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
                    c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);

#if RENDER == R_R1
                    Fvector C;
                    C.set(c_ambient);
                    C.mad(c_hemi, Instance.c_hemi);
                    C.mad(c_sun, Instance.c_sun);
                    c_storage[base + 3].set(C.x, C.y, C.z, 1.f);
#else
                    const float h = Instance.c_hemi;
                    const float s = Instance.c_sun;
                    c_storage[base + 3].set(s, s, s, h);
#endif
                    dwBatch++;
                    if (dwBatch == hw_BatchSize) {
                        RDEVICE.Statistic->RenderDUMP_DT_Count += dwBatch;
                        const u32 dwCNT_verts = dwBatch * verts_per_obj;
                        const u32 dwCNT_prims = dwBatch * prims_per_obj;
                        
                        RCache.get_ConstantCache_Vertex().b_dirty = TRUE;
                        RCache.get_ConstantCache_Vertex().get_array_f().dirty(c_base, c_base + dwBatch * 4);
                        RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                        RCache.stat.r.s_details.add(dwCNT_verts);

                        dwBatch = 0;
                    }
                }
            }
            if (dwBatch) {
                RDEVICE.Statistic->RenderDUMP_DT_Count += dwBatch;
                const u32 dwCNT_verts = dwBatch * verts_per_obj;
                const u32 dwCNT_prims = dwBatch * prims_per_obj;
                
                RCache.get_ConstantCache_Vertex().b_dirty = TRUE;
                RCache.get_ConstantCache_Vertex().get_array_f().dirty(c_base, c_base + dwBatch * 4);
                RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                RCache.stat.r.s_details.add(dwCNT_verts);
            }
            vis.clear();
        }
        vOffset += hw_BatchSize * Object.number_vertices;
        iOffset += hw_BatchSize * Object.number_indices;
    }
}
#endif