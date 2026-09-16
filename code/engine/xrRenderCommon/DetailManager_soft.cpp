#include "stdafx.h"
#include "DetailManager.h"

constexpr u32 vs_size = 3000;

void CDetailManager::soft_Load() {
    R_ASSERT(RCache.Vertex.Buffer());
    R_ASSERT(RCache.Index.Buffer());
    
    // Vertex Stream
    soft_Geom.create(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, RCache.Vertex.Buffer(), RCache.Index.Buffer());
}

void CDetailManager::soft_Unload() { 
    soft_Geom.destroy(); 
}

void CDetailManager::soft_Render() {
    _IndexStream& _IS = RCache.Index;
    _VertexStream& _VS = RCache.Vertex;
    
    for (u32 O = 0; O < objects.size(); ++O) {
        CDetail& Object = *objects[O];
        const u32 vCount_Object = Object.number_vertices;
        const u32 iCount_Object = Object.number_indices;

        auto& _vis = m_visibles[0][O];
        for (auto* items : _vis) {
            const u32 o_total = items->size();
            const u32 vCount_Total = o_total * vCount_Object;
            
            // calculate lock count needed
            u32 lock_count = vCount_Total / vs_size;
            if (vCount_Total > (lock_count * vs_size)) lock_count++;

            // calculate objects per lock
            u32 o_per_lock = o_total / lock_count;
            if (o_total > (o_per_lock * lock_count)) o_per_lock++;

            // Fill VB (and flush it as necessary)
            RCache.set_Shader(Object.shader);

            Fmatrix mXform;
            for (u32 L_ID = 0; L_ID < lock_count; ++L_ID) {
                // Calculate params
                const u32 item_start = L_ID * o_per_lock;
                u32 item_end = item_start + o_per_lock;
                if (item_end > o_total) item_end = o_total;
                if (item_end <= item_start) break;
                
                const u32 item_range = item_end - item_start;

                // Calc Lock params
                const u32 vCount_Lock = item_range * vCount_Object;
                const u32 iCount_Lock = item_range * iCount_Object;

                // Lock buffers
                u32 vBase, iBase;
                u32 iOffset = 0;
                
                auto* vDest = static_cast<CDetail::fvfVertexOut*>(_VS.Lock(vCount_Lock, soft_Geom->vb_stride, vBase));
                u16* iDest = static_cast<u16*>(_IS.Lock(iCount_Lock, iBase));

                // Filling itself
                for (u32 item_idx = item_start; item_idx < item_end; ++item_idx) {
                    const SlotItem& Instance = *items->at(item_idx);
                    const float scale = Instance.scale_calculated;

                    // Build matrix
                    const Fmatrix& M = Instance.mRotY;
                    mXform._11 = M._11 * scale; mXform._12 = M._12 * scale; mXform._13 = M._13 * scale; mXform._14 = M._14;
                    mXform._21 = M._21 * scale; mXform._22 = M._22 * scale; mXform._23 = M._23 * scale; mXform._24 = M._24;
                    mXform._31 = M._31 * scale; mXform._32 = M._32 * scale; mXform._33 = M._33 * scale; mXform._34 = M._34;
                    mXform._41 = M._41;         mXform._42 = M._42;         mXform._43 = M._43;         mXform._44 = 1.f;

                    // Transfer vertices
                    {
                        constexpr u32 C = 0xffffffff;
                        const CDetail::fvfVertexIn* srcIt = Object.vertices;
                        const CDetail::fvfVertexIn* srcEnd = Object.vertices + Object.number_vertices;

                        for (; srcIt != srcEnd; ++srcIt, ++vDest) {
                            mXform.transform_tiny(vDest->P, srcIt->P);
                            vDest->C = C;
                            vDest->u = srcIt->u;
                            vDest->v = srcIt->v;
                        }
                    }

                    // Transfer indices (in 32bit lines)
                    VERIFY(iOffset < 65535);
                    {
                        const u32 item = (iOffset << 16) | iOffset;
                        const u32 count = Object.number_indices / 2;
                        const u32* sit = reinterpret_cast<const u32*>(Object.indices);
                        const u32* send = sit + count;
                        u32* dit = reinterpret_cast<u32*>(iDest);
                        
                        for (; sit != send; ++dit, ++sit) {
                            *dit = *sit + item;
                        }
                        
                        if (Object.number_indices & 1) {
                            iDest[Object.number_indices - 1] = static_cast<u16>(Object.indices[Object.number_indices - 1] + static_cast<u16>(iOffset));
                        }
                    }

                    // Increment counters
                    iDest += iCount_Object;
                    iOffset += vCount_Object;
                }
                
                _VS.Unlock(vCount_Lock, soft_Geom->vb_stride);
                _IS.Unlock(iCount_Lock);

                // Render
                const u32 dwNumPrimitives = iCount_Lock / 3;
                RCache.set_Geometry(soft_Geom);
                RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount_Lock, iBase, dwNumPrimitives);
            }
        }
        // Clean up
        _vis.clear();
    }
}