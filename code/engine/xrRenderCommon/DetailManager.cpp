// DetailManager.cpp: implementation of the CDetailManager class.
#include "stdafx.h"
#include "DetailManager.h"
#include "cl_intersect.h"
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include <xmmintrin.h>

const float dbgOffset = 0.f;
const int dbgItems = 128;

//--------------------------------------------------- Decompression
static int magic4x4[4][4] = {
    { 0, 14, 3, 13 }, { 11, 5, 8, 6 }, { 12, 2, 15, 1 }, { 7, 9, 4, 10 }
};

void bwdithermap(int levels, int magic[16][16]) {
    const float N = 255.0f / (levels - 1);
    const float magicfact = (N - 1) / 16.f;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                for (int l = 0; l < 4; l++) {
                    magic[4 * k + i][4 * l + j] = static_cast<int>(
                        0.5f + magic4x4[i][j] * magicfact + (magic4x4[k][l] / 16.f) * magicfact
                    );
                }
            }
        }
    }
}
//--------------------------------------------------- Decompression

void CDetailManager::SSwingValue::lerp(const SSwingValue& A, const SSwingValue& B, float f) {
    const float fi = 1.f - f;
    amp1 = fi * A.amp1 + f * B.amp1;
    amp2 = fi * A.amp2 + f * B.amp2;
    rot1 = fi * A.rot1 + f * B.rot1;
    rot2 = fi * A.rot2 + f * B.rot2;
    speed = fi * A.speed + f * B.speed;
}
//---------------------------------------------------

CDetailManager::CDetailManager() {
    dtFS = nullptr;
    dtSlots = nullptr;
    soft_Geom = nullptr;
    hw_Geom = nullptr;
    hw_BatchSize = 0;
    hw_VB = nullptr;
    hw_IB = nullptr;
    m_time_rot_1 = 0;
    m_time_rot_2 = 0;
    m_time_pos = 0;
    m_global_time_old = 0;
}

CDetailManager::~CDetailManager() {}

#ifndef _EDITOR
void CDetailManager::Load() {
    if (!FS.exist("$level$", "level.details")) {
        dtFS = nullptr;
        return;
    }

    string_path fn;
    FS.update_path(fn, "$level$", "level.details");
    dtFS = FS.r_open(fn);

    dtFS->r_chunk_safe(0, &dtH, sizeof(dtH));
    R_ASSERT(dtH.version == DETAIL_VERSION);
    u32 m_count = dtH.object_count;

    IReader* m_fs = dtFS->open_chunk(1);
    for (u32 m_id = 0; m_id < m_count; m_id++) {
        CDetail* dt = xr_new<CDetail>();
        IReader* S = m_fs->open_chunk(m_id);
        dt->Load(S);
        objects.push_back(dt);
        S->close();
    }
    m_fs->close();

    IReader* m_slots = dtFS->open_chunk(2);
    dtSlots = (DetailSlot*)m_slots->pointer();
    m_slots->close();

    for (u32 i = 0; i < 3; ++i)
        m_visibles[i].resize(objects.size());
    cache_Initialize();

    bwdithermap(2, dither);

    if (UseVS())
        hw_Load();
    else
        soft_Load();

    swing_desc[0].amp1 = pSettings->r_float("details", "swing_normal_amp1");
    swing_desc[0].amp2 = pSettings->r_float("details", "swing_normal_amp2");
    swing_desc[0].rot1 = pSettings->r_float("details", "swing_normal_rot1");
    swing_desc[0].rot2 = pSettings->r_float("details", "swing_normal_rot2");
    swing_desc[0].speed = pSettings->r_float("details", "swing_normal_speed");
    
    swing_desc[1].amp1 = pSettings->r_float("details", "swing_fast_amp1");
    swing_desc[1].amp2 = pSettings->r_float("details", "swing_fast_amp2");
    swing_desc[1].rot1 = pSettings->r_float("details", "swing_fast_rot1");
    swing_desc[1].rot2 = pSettings->r_float("details", "swing_fast_rot2");
    swing_desc[1].speed = pSettings->r_float("details", "swing_fast_speed");
}
#endif

void CDetailManager::Unload() {
    if (UseVS()) hw_Unload();
    else soft_Unload();

    for (auto* obj : objects) {
        obj->Unload();
        xr_delete(obj);
    }
    objects.clear();
    
    for (int i = 0; i < 3; ++i) {
        m_visibles[i].clear();
    }
    
    if (dtFS) FS.r_close(dtFS);
}

extern ECORE_API float r_ssaDISCARD;

void CDetailManager::UpdateVisibleM() {
    Fvector EYE = RDEVICE.vCameraPosition_saved;

    CFrustum View;
    View.CreateFromMatrix(RDEVICE.mFullTransform_saved, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    // Кешування констант для зменшення навантаження у циклі
    const float fade_limit = dm_fade * dm_fade;
    const float fade_start = 1.f; // 1.f * 1.f
    const float fade_range = fade_limit - fade_start;
    const float r_ssaCHEAP = 16.f * r_ssaDISCARD;
    const u32 dwCC = dm_cache1_count * dm_cache1_count;

    RDEVICE.Statistic->RenderDUMP_DT_VIS.Begin();
    
    for (int _mz = 0; _mz < dm_cache1_line; _mz++) {
        for (int _mx = 0; _mx < dm_cache1_line; _mx++) {
            CacheSlot1& MS = cache_level1[_mz][_mx];
            if (MS.empty) continue;
            
            u32 mask = 0xff;
            u32 res = View.testSAABB(MS.vis.sphere.P, MS.vis.sphere.R, MS.vis.box.data(), mask);
            if (fcvNone == res) continue; 

            for (u32 _i = 0; _i < dwCC; _i++) {
                Slot& S = *(*MS.slots[_i]);

                if (S.empty) continue;

                if (fcvPartial == res) {
                    u32 _mask = mask;
                    if (fcvNone == View.testSAABB(S.vis.sphere.P, S.vis.sphere.R, S.vis.box.data(), _mask)) {
                        continue; 
                    }
                }
#ifndef _EDITOR
                if (!RImplementation.HOM.visible(S.vis)) continue; 
#endif

                if (RDEVICE.dwFrame > S.frame) {
                    float dist_sq = EYE.distance_to_sqr(S.vis.sphere.P);
                    if (dist_sq > fade_limit) continue;
                    
                    float alpha = (dist_sq < fade_start) ? 0.f : (dist_sq - fade_start) / fade_range;
                    float alpha_i = 1.f - alpha;
                    float dist_sq_rcp = 1.f / dist_sq;

                    S.frame = RDEVICE.dwFrame + Random.randI(15, 30);
                    
                    for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++) {
                        SlotPart& sp = S.G[sp_id];
                        if (sp.id == DetailSlot::ID_Empty) continue;

                        sp.r_items[0].clear();
                        sp.r_items[1].clear();
                        sp.r_items[2].clear();

                        const float R = objects[sp.id]->bv_sphere.R;
                        const float Rq_drcp = R * R * dist_sq_rcp; 

                        // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Range-based цикл замість подвійних вказівників
                        for (auto* item : sp.items) {
                            float scale = item->scale_calculated = item->scale * alpha_i;
                            float ssa = scale * scale * Rq_drcp;
                            
                            if (ssa < r_ssaDISCARD) continue;
                            
                            u32 vis_id = (ssa > r_ssaCHEAP) ? item->vis_ID : 0;
                            sp.r_items[vis_id].push_back(item);
                        }
                    }
                }
                
                for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++) {
                    SlotPart& sp = S.G[sp_id];
                    if (sp.id == DetailSlot::ID_Empty) continue;
                    
                    if (!sp.r_items[0].empty()) m_visibles[0][sp.id].push_back(&sp.r_items[0]);
                    if (!sp.r_items[1].empty()) m_visibles[1][sp.id].push_back(&sp.r_items[1]);
                    if (!sp.r_items[2].empty()) m_visibles[2][sp.id].push_back(&sp.r_items[2]);
                }
            }
        }
    }
    RDEVICE.Statistic->RenderDUMP_DT_VIS.End();
}

void CDetailManager::Render() {
#ifndef _EDITOR
    if (!dtFS || !psDeviceFlags.is(rsDetails)) return;
#endif

    MT_SYNC();

    RDEVICE.Statistic->RenderDUMP_DT_Render.Begin();

#ifndef _EDITOR
    float factor = g_pGamePersistent->Environment().wind_strength_factor;
#else
    float factor = 0.3f;
#endif
    swing_current.lerp(swing_desc[0], swing_desc[1], factor);

    RCache.set_CullMode(CULL_NONE);
    RCache.set_xform_world(Fidentity);
    
    if (UseVS()) hw_Render();
    else soft_Render();
    
    RCache.set_CullMode(CULL_CCW);
    RDEVICE.Statistic->RenderDUMP_DT_Render.End();
    
    m_frame_rendered.store(RDEVICE.dwFrame, std::memory_order_release);
}

void __stdcall CDetailManager::MT_CALC() {
#ifndef _EDITOR
    if (!RImplementation.Details || !dtFS || !psDeviceFlags.is(rsDetails)) return;
#endif

    std::lock_guard<std::recursive_mutex> lock(MT);
    
    if (m_frame_calc.load(std::memory_order_acquire) != RDEVICE.dwFrame) {
        if ((m_frame_rendered.load(std::memory_order_acquire) + 1) == RDEVICE.dwFrame) {
            Fvector EYE = RDEVICE.vCameraPosition_saved;

            int s_x = iFloor(EYE.x / dm_slot_size + 0.5f);
            int s_z = iFloor(EYE.z / dm_slot_size + 0.5f);

            RDEVICE.Statistic->RenderDUMP_DT_Cache.Begin();
            cache_Update(s_x, s_z, EYE, dm_max_decompress);
            RDEVICE.Statistic->RenderDUMP_DT_Cache.End();

            UpdateVisibleM();
            m_frame_calc.store(RDEVICE.dwFrame, std::memory_order_release);
        }
    }
}