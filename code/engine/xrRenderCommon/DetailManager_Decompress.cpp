#include "stdafx.h"
#pragma hdrstop

#include "DetailManager.h"
#include "cl_intersect.h"
#include <algorithm> 
#include <array>

#ifdef _EDITOR
#include "scene.h"
#include "sceneobject.h"
#include "../utils/ETools/ETools.h"
#endif

//--------------------------------------------------- Decompression
inline float Interpolate(const float* base, u32 x, u32 y, u32 size) {
    const float f = static_cast<float>(size);
    const float fx = static_cast<float>(x) / f;
    const float ifx = 1.f - fx;
    const float fy = static_cast<float>(y) / f;
    const float ify = 1.f - fy;

    const float c01 = base[0] * ifx + base[1] * fx;
    const float c23 = base[2] * ifx + base[3] * fx;

    const float c02 = base[0] * ify + base[2] * fy;
    const float c13 = base[1] * ify + base[3] * fy;

    const float cx = ify * c01 + fy * c23;
    const float cy = ifx * c02 + fx * c13;
    return (cx + cy) * 0.5f;
}

inline bool InterpolateAndDither(const float* alpha255, u32 x, u32 y, u32 sx, u32 sy, u32 size, int dither[16][16]) {
    x = std::clamp(x, 0u, size - 1);
    y = std::clamp(y, 0u, size - 1);
    
    int c = iFloor(Interpolate(alpha255, x, y, size) + 0.5f);
    c = std::clamp(c, 0, 255);

    const u32 row = (y + sy) % 16;
    const u32 col = (x + sx) % 16;
    return c > dither[col][row];
}

#ifndef _EDITOR
#ifdef DEBUG
#include "dxDebugRender.h"
static void draw_obb(const Fmatrix& matrix, const u32& color) {
    Fvector aabb[8];
    matrix.transform_tiny(aabb[0], Fvector().set(-1, -1, -1)); 
    matrix.transform_tiny(aabb[1], Fvector().set(-1, +1, -1)); 
    matrix.transform_tiny(aabb[2], Fvector().set(+1, +1, -1)); 
    matrix.transform_tiny(aabb[3], Fvector().set(+1, -1, -1)); 
    matrix.transform_tiny(aabb[4], Fvector().set(-1, -1, +1)); 
    matrix.transform_tiny(aabb[5], Fvector().set(-1, +1, +1)); 
    matrix.transform_tiny(aabb[6], Fvector().set(+1, +1, +1)); 
    matrix.transform_tiny(aabb[7], Fvector().set(+1, -1, +1)); 

    u16 aabb_id[12 * 2] = {
        0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 1, 5, 2, 6, 3, 7, 0, 4
    };

    rdebug_render->add_lines(aabb, sizeof(aabb) / sizeof(Fvector), &aabb_id[0],
                             sizeof(aabb_id) / (2 * sizeof(u16)), color);
}

bool det_render_debug = false;
#endif
#endif

#include "xrEngine/gamemtllib.h"

void CDetailManager::cache_Decompress(Slot* S) {
    VERIFY(S);
    Slot& D = *S;
    D.type = stReady;
    
    if (D.empty) return;

    DetailSlot& DS = QueryDB(D.sx, D.sz);

    // Select polygons
    Fvector bC, bD;
    D.vis.box.get_CD(bC, bD);

#ifdef _EDITOR
    ETOOLS::box_options(CDB::OPT_FULL_TEST);
    SBoxPickInfoVec pinf;
    Scene->BoxPickObjects(D.vis.box, pinf, GetSnapList());
    u32 triCount = pinf.size();
#else
    xrc.box_options(CDB::OPT_FULL_TEST);
    xrc.box_query(g_pGameLevel->ObjectSpace.GetStaticModel(), bC, bD);
    const u32 raw_triCount = xrc.r_count();
    const CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
    const Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();

    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Пре-фільтрація трикутників.
    // Уникаємо виклику GMLib.GetMaterialByIdx для кожної травинки!
    xr_vector<std::array<Fvector, 3>> valid_tris;
    valid_tris.reserve(raw_triCount);

    for (u32 i = 0; i < raw_triCount; ++i) {
        const CDB::TRI& T = tris[xrc.r_begin()[i].id];
        const SGameMtl* mtl = GMLib.GetMaterialByIdx(T.material);
        if (!mtl->Flags.test(SGameMtl::flPassable)) {
            valid_tris.push_back({verts[T.verts[0]], verts[T.verts[1]], verts[T.verts[2]]});
        }
    }
    const u32 triCount = valid_tris.size();
#endif

    if (0 == triCount) return;

    // Build shading table
    float alpha255[dm_obj_in_slot][4];
    const float inv15 = 255.f / 15.f; // Оптимізація ділення
    for (int i = 0; i < dm_obj_in_slot; i++) {
        alpha255[i][0] = static_cast<float>(DS.palette[i].a0) * inv15;
        alpha255[i][1] = static_cast<float>(DS.palette[i].a1) * inv15;
        alpha255[i][2] = static_cast<float>(DS.palette[i].a2) * inv15;
        alpha255[i][3] = static_cast<float>(DS.palette[i].a3) * inv15;
    }

    // Prepare to selection
    const float density = ps_r__Detail_density;
    const float jitter = density / 1.7f;
    const u32 d_size = iCeil(dm_slot_size / density);
    svector<int, dm_obj_in_slot> selected;

    const u32 p_rnd = D.sx * D.sz; 
    CRandom r_selection(0x12071980 ^ p_rnd);
    CRandom r_jitter(0x12071980 ^ p_rnd);
    CRandom r_yaw(0x12071980 ^ p_rnd);
    CRandom r_scale(0x12071980 ^ p_rnd);

    Fbox Bounds;
    Bounds.invalidate();

    // Decompressing itself
    for (u32 z = 0; z <= d_size; z++) {
        for (u32 x = 0; x <= d_size; x++) {
            const u32 shift_x = r_jitter.randI(16);
            const u32 shift_z = r_jitter.randI(16);

            selected.clear();

            if (DS.id0 != DetailSlot::ID_Empty && InterpolateAndDither(alpha255[0], x, z, shift_x, shift_z, d_size, dither)) selected.push_back(0);
            if (DS.id1 != DetailSlot::ID_Empty && InterpolateAndDither(alpha255[1], x, z, shift_x, shift_z, d_size, dither)) selected.push_back(1);
            if (DS.id2 != DetailSlot::ID_Empty && InterpolateAndDither(alpha255[2], x, z, shift_x, shift_z, d_size, dither)) selected.push_back(2);
            if (DS.id3 != DetailSlot::ID_Empty && InterpolateAndDither(alpha255[3], x, z, shift_x, shift_z, d_size, dither)) selected.push_back(3);

            if (selected.empty()) continue;

            const u32 index = (selected.size() == 1) ? selected[0] : selected[r_selection.randI(selected.size())];

            CDetail* Dobj = objects[DS.r_id(index)];
            SlotItem* ItemP = poolSI.create();
            SlotItem& Item = *ItemP;

            // Position (XZ)
            const float rx = (static_cast<float>(x) / static_cast<float>(d_size)) * dm_slot_size + D.vis.box.min.x;
            const float rz = (static_cast<float>(z) / static_cast<float>(d_size)) * dm_slot_size + D.vis.box.min.z;
            
            Fvector Item_P;
            Item_P.set(rx + r_jitter.randFs(jitter), D.vis.box.max.y, rz + r_jitter.randFs(jitter));

            // Position (Y)
            float y = D.vis.box.min.y - 5.f;
            const Fvector dir{ 0.f, -1.f, 0.f };

            float r_u, r_v, r_range;
            for (u32 tid = 0; tid < triCount; tid++) {
#ifdef _EDITOR
                Fvector verts[3];
                SBoxPickInfo& I = pinf[tid];
                for (int k = 0; k < (int)I.inf.size(); k++) {
                    VERIFY(I.s_obj);
                    I.e_obj->GetFaceWorld(I.s_obj->_Transform(), I.e_mesh, I.inf[k].id, verts);
                    if (CDB::TestRayTri(Item_P, dir, verts, r_u, r_v, r_range, TRUE)) {
                        if (r_range >= 0.f) {
                            float y_test = Item_P.y - r_range;
                            if (y_test > y) y = y_test;
                        }
                    }
                }
#else
                // Оптимізований Raycast по відфільтрованих трикутниках
                if (CDB::TestRayTri(Item_P, dir, valid_tris[tid].data(), r_u, r_v, r_range, TRUE)) {
                    if (r_range >= 0.f) {
                        float y_test = Item_P.y - r_range;
                        if (y_test > y) y = y_test;
                    }
                }
#endif
            }
            if (y < D.vis.box.min.y) {
                poolSI.destroy(ItemP); // Не забуваємо повернути пам'ять, якщо трава під картою
                continue;
            }
            Item_P.y = y;

            // Angles and scale
            Item.scale = r_scale.randF(Dobj->m_fMinScale * 0.5f, Dobj->m_fMaxScale * 0.9f);

            Fmatrix mScale, mXform;
            Fbox ItemBB;

            Item.mRotY.rotateY(r_yaw.randF(0.f, PI_MUL_2));
            Item.mRotY.translate_over(Item_P);
            
            mScale.scale(Item.scale, Item.scale, Item.scale);
            mXform.mul_43(Item.mRotY, mScale);
            ItemBB.xform(Dobj->bv_bb, mXform);
            Bounds.merge(ItemBB);

#ifndef _EDITOR
#ifdef DEBUG
            if (det_render_debug)
                draw_obb(mXform, color_rgba(255, 0, 0, 255)); 
#endif
#endif

            // Color
#if RENDER == R_R1
            Item.c_rgb.x = DS.r_qclr(DS.c_r, 15);
            Item.c_rgb.y = DS.r_qclr(DS.c_g, 15);
            Item.c_rgb.z = DS.r_qclr(DS.c_b, 15);
#endif
            Item.c_hemi = DS.r_qclr(DS.c_hemi, 15);
            Item.c_sun = DS.r_qclr(DS.c_dir, 15);

            // Vis-sorting
            if (!UseVS() || Dobj->m_Flags.is(DO_NO_WAVING)) {
                Item.vis_ID = 0; // Still
            } else {
                Item.vis_ID = (::Random.randI(0, 3) == 0) ? 2 : 1; 
            }

            // Save it
            D.G[index].items.push_back(ItemP);
        }
    }

    // Update bounds to more tight and real ones
    D.vis.clear();
    D.vis.box.set(Bounds);
    D.vis.box.getsphere(D.vis.sphere.P, D.vis.sphere.R);
}