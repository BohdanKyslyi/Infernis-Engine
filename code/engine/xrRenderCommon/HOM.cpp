// HOM.cpp: implementation of the CHOM class.
#include "stdafx.h"
#include "HOM.h"
#include "occRasterizer.h"
#include "xrEngine/GameFont.h"
#include "dxRenderDeviceRender.h"
#include <algorithm> 
#include <cmath>

float psOSSR = 0.001f;

void __stdcall CHOM::MT_RENDER() {
    std::lock_guard<std::recursive_mutex> lock(MT);
    bool b_main_menu_is_active = (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
    
    // Безпечне атомарне читання
    if (MT_frame_rendered.load(std::memory_order_acquire) != Device.dwFrame && !b_main_menu_is_active) {
        CFrustum ViewBase;
        ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
        Enable();
        Render(ViewBase);
    }
}

CHOM::CHOM() {
#ifdef DEBUG
    Device.seqRender.Add(this, REG_PRIORITY_LOW - 1000);
#endif
}

CHOM::~CHOM() {
#ifdef DEBUG
    Device.seqRender.Remove(this);
#endif
}

#pragma pack(push, 4)
struct HOM_poly {
    Fvector v1, v2, v3;
    u32 flags;
};
#pragma pack(pop)

inline float Area(const Fvector& v0, const Fvector& v1, const Fvector& v2) {
    const float e1 = v0.distance_to(v1);
    const float e2 = v0.distance_to(v2);
    const float e3 = v1.distance_to(v2);

    const float p = (e1 + e2 + e3) * 0.5f;
    return std::sqrt(p * (p - e1) * (p - e2) * (p - e3));
}

void CHOM::Load() {
    string_path fName;
    FS.update_path(fName, "$level$", "level.hom");
    if (!FS.exist(fName)) {
        Msg(" WARNING: Occlusion map '%s' not found.", fName);
        return;
    }
    Msg("* Loading HOM: %s", fName);

    IReader* fs = FS.r_open(fName);
    IReader* S = fs->open_chunk(1);

    CDB::Collector CL;
    while (!S->eof()) {
        HOM_poly P;
        S->r(&P, sizeof(P));
        CL.add_face_packed_D(P.v1, P.v2, P.v3, P.flags, 0.01f);
    }

    xr_vector<u32> adjacency;
    CL.calc_adjacency(adjacency);

    m_pTris = xr_alloc<occTri>(static_cast<u32>(CL.getTS()));
    for (u32 it = 0; it < CL.getTS(); ++it) {
        CDB::TRI& clT = CL.getT()[it];
        occTri& rT = m_pTris[it];
        Fvector& v0 = CL.getV()[clT.verts[0]];
        Fvector& v1 = CL.getV()[clT.verts[1]];
        Fvector& v2 = CL.getV()[clT.verts[2]];
        
        rT.adjacent[0] = (0xffffffff == adjacency[3 * it + 0]) ? reinterpret_cast<occTri*>(-1) : (m_pTris + adjacency[3 * it + 0]);
        rT.adjacent[1] = (0xffffffff == adjacency[3 * it + 1]) ? reinterpret_cast<occTri*>(-1) : (m_pTris + adjacency[3 * it + 1]);
        rT.adjacent[2] = (0xffffffff == adjacency[3 * it + 2]) ? reinterpret_cast<occTri*>(-1) : (m_pTris + adjacency[3 * it + 2]);
        
        rT.flags = clT.dummy;
        rT.area = Area(v0, v1, v2);
        
        if (rT.area < EPS_L) {
            Msg("! Invalid HOM triangle (%f,%f,%f)-(%f,%f,%f)-(%f,%f,%f)", VPUSH(v0), VPUSH(v1), VPUSH(v2));
        }
        rT.plane.build(v0, v1, v2);
        rT.skip = 0;
        rT.center.add(v0, v1).add(v2).div(3.f);
    }

    m_pModel = xr_new<CDB::MODEL>();
    m_pModel->build(CL.getV(), static_cast<int>(CL.getVS()), CL.getT(), static_cast<int>(CL.getTS()));
    bEnabled = TRUE;
    S->close();
    FS.r_close(fs);
}

void CHOM::Unload() {
    xr_delete(m_pModel);
    xr_free(m_pTris);
    bEnabled = FALSE;
}

void CHOM::Render_DB(CFrustum& base) {
    const float view_dim = occ_dim_0;
    const Fmatrix m_viewport = {
        view_dim / 2.f, 0.0f, 0.0f, 0.0f, 
        0.0f, -view_dim / 2.f, 0.0f, 0.0f,           
        0.0f, 0.0f, 1.0f, 0.0f, 
        view_dim / 2.f, view_dim / 2.f, 0.0f, 1.0f
    };
    const Fmatrix m_viewport_01 = {
        0.5f, 0.0f, 0.0f, 0.0f, 
        0.0f, -0.5f, 0.0f, 0.0f,      
        0.0f, 0.0f, 1.0f, 0.0f, 
        0.5f, 0.5f, 0.0f, 1.0f
    };
    m_xform.mul(m_viewport, Device.mFullTransform);
    m_xform_01.mul(m_viewport_01, Device.mFullTransform);

    xrc.frustum_options(0);
    xrc.frustum_query(m_pModel, base);
    if (0 == xrc.r_count()) return;

    CDB::RESULT* it = xrc.r_begin();
    CDB::RESULT* end = xrc.r_end();
    Fvector COP = Device.vCameraPosition;

    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: C++11 Лямбди замість важких функторів
    end = std::remove_if(it, end, [this](const CDB::RESULT& res) {
        return m_pTris[res.id].skip > Device.dwFrame;
    });
    
    std::sort(it, end, [this, &COP](const CDB::RESULT& a, const CDB::RESULT& b) {
        return COP.distance_to_sqr(m_pTris[a.id].center) < COP.distance_to_sqr(m_pTris[b.id].center);
    });

    CFrustum clip;
    clip.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_NEAR);
    sPoly src, dst;
    const u32 _frame = Device.dwFrame;
    
#ifdef DEBUG
    tris_in_frame = xrc.r_count();
    tris_in_frame_visible = 0;
#endif

    for (; it != end; ++it) {
        occTri& T = m_pTris[it->id];
        u32 next = _frame + ::Random.randI(3, 10);

        if (!(T.flags || (T.plane.classify(COP) > 0))) {
            T.skip = next;
            continue;
        }

        CDB::TRI& t = m_pModel->get_tris()[it->id];
        Fvector* v = m_pModel->get_verts();
        src.clear();
        dst.clear();
        src.push_back(v[t.verts[0]]);
        src.push_back(v[t.verts[1]]);
        src.push_back(v[t.verts[2]]);
        
        sPoly* P = clip.ClipPoly(src, dst);
        if (!P) {
            T.skip = next;
            continue;
        }

#ifdef DEBUG
        tris_in_frame_visible++;
#endif
        u32 pixels = 0;
        const int limit = static_cast<int>(P->size()) - 1;
        for (int i = 1; i < limit; ++i) {
            m_xform.transform(T.raster[0], (*P)[0]);
            m_xform.transform(T.raster[1], (*P)[i]);
            m_xform.transform(T.raster[2], (*P)[i + 1]);
            pixels += Raster.rasterize(&T);
        }
        
        if (0 == pixels) {
            T.skip = next;
            continue;
        }
    }
}

void CHOM::Render(CFrustum& base) {
    if (!bEnabled) return;

    Device.Statistic->RenderCALC_HOM.Begin();
    Raster.clear();
    Render_DB(base);
    Raster.propagade();
    
    // Атомарний запис
    MT_frame_rendered.store(Device.dwFrame, std::memory_order_release);
    
    Device.Statistic->RenderCALC_HOM.End();
}

inline BOOL xform_b0(Fvector2& min, Fvector2& max, float& minz, const Fmatrix& X, float _x, float _y, float _z) {
    float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
    if (z < EPS) return TRUE;
    float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
    min.x = max.x = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
    min.y = max.y = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
    minz = z * iw;
    return FALSE;
}

inline BOOL xform_b1(Fvector2& min, Fvector2& max, float& minz, const Fmatrix& X, float _x, float _y, float _z) {
    float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
    if (z < EPS) return TRUE;
    float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
    
    float t = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
    if (t < min.x) min.x = t;
    else if (t > max.x) max.x = t;
    
    t = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
    if (t < min.y) min.y = t;
    else if (t > max.y) max.y = t;
    
    t = z * iw;
    if (t < minz) minz = t;
    
    return FALSE;
}

static inline BOOL _visible(const Fbox& B, const Fmatrix& m_xform_01) {
    Fvector2 min, max;
    float z;
    if (xform_b0(min, max, z, m_xform_01, B.min.x, B.min.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.min.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.min.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.min.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.max.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.max.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.max.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.max.y, B.min.z)) return TRUE;
    return Raster.test(min.x, min.y, max.x, max.y, z);
}

BOOL CHOM::visible(const Fbox3& B) {
    if (!bEnabled) return TRUE;
    if (B.contains(Device.vCameraPosition)) return TRUE;
    return _visible(B, m_xform_01);
}

BOOL CHOM::visible(const Fbox2& B, float depth) {
    if (!bEnabled) return TRUE;
    return Raster.test(B.min.x, B.min.y, B.max.x, B.max.y, depth);
}

BOOL CHOM::visible(vis_data& vis) {
    if (Device.dwFrame < vis.hom_frame) return TRUE; 
    if (!bEnabled) return TRUE; 

#ifdef DEBUG
    Device.Statistic->RenderCALC_HOM.Begin();
#endif
    BOOL result = _visible(vis.box, m_xform_01);
    
    u32 delay = result ? ::Random.randI(10, 25) : 1;
    vis.hom_frame = Device.dwFrame + delay;
    vis.hom_tested = Device.dwFrame;
    
#ifdef DEBUG
    Device.Statistic->RenderCALC_HOM.End();
#endif

    return result;
}

BOOL CHOM::visible(const sPoly& P) {
    if (!bEnabled) return TRUE;

    Fvector2 min, max;
    float z;

    if (xform_b0(min, max, z, m_xform_01, P.front().x, P.front().y, P.front().z)) return TRUE;
    for (u32 it = 1; it < P.size(); ++it) {
        if (xform_b1(min, max, z, m_xform_01, P[it].x, P[it].y, P[it].z)) return TRUE;
    }
        
    return Raster.test(min.x, min.y, max.x, max.y, z);
}

void CHOM::Disable() { bEnabled = FALSE; }
void CHOM::Enable() { bEnabled = m_pModel ? TRUE : FALSE; }

#ifdef DEBUG
void CHOM::OnRender() {
    Raster.on_dbg_render();

    if (psDeviceFlags.is(rsOcclusionDraw)) {
        if (m_pModel) {
            using LVec = xr_vector<FVF::L>;
            static LVec poly;
            poly.resize(m_pModel->get_tris_count() * 3);
            static LVec line;
            line.resize(m_pModel->get_tris_count() * 6);
            
            for (int it = 0; it < m_pModel->get_tris_count(); ++it) {
                CDB::TRI* T = m_pModel->get_tris() + it;
                Fvector* verts = m_pModel->get_verts();
                
                poly[it * 3 + 0].set(*(verts + T->verts[0]), 0x80FFFFFF);
                poly[it * 3 + 1].set(*(verts + T->verts[1]), 0x80FFFFFF);
                poly[it * 3 + 2].set(*(verts + T->verts[2]), 0x80FFFFFF);
                
                line[it * 6 + 0].set(*(verts + T->verts[0]), 0xFFFFFFFF);
                line[it * 6 + 1].set(*(verts + T->verts[1]), 0xFFFFFFFF);
                line[it * 6 + 2].set(*(verts + T->verts[1]), 0xFFFFFFFF);
                line[it * 6 + 3].set(*(verts + T->verts[2]), 0xFFFFFFFF);
                line[it * 6 + 4].set(*(verts + T->verts[2]), 0xFFFFFFFF);
                line[it * 6 + 5].set(*(verts + T->verts[0]), 0xFFFFFFFF);
            }
            RCache.set_xform_world(Fidentity);
            
            // draw solid
            Device.SetNearer(TRUE);
            RCache.set_Shader(dxRenderDeviceRender::Instance().m_SelectionShader);
            RCache.dbg_Draw(D3DPT_TRIANGLELIST, poly.data(), poly.size() / 3);
            Device.SetNearer(FALSE);
            
            // draw wire
            if (bDebug) RImplementation.rmNear();
            else        Device.SetNearer(TRUE);
            
            RCache.set_Shader(dxRenderDeviceRender::Instance().m_SelectionShader);
            RCache.dbg_Draw(D3DPT_LINELIST, line.data(), line.size() / 2);
            
            if (bDebug) RImplementation.rmNormal();
            else        Device.SetNearer(FALSE);
        }
    }
}

void CHOM::stats() {
    if (m_pModel) {
        CGameFont& F = *Device.Statistic->Font();
        F.OutNext(" **** HOM-occ ****");
        F.OutNext("  visible:  %2d", tris_in_frame_visible);
        F.OutNext("  frustum:  %2d", tris_in_frame);
        F.OutNext("    total:  %2d", m_pModel->get_tris_count());
    }
}
#endif