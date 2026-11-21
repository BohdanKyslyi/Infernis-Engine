// DetailManager.cpp: implementation of the CDetailManager class.
#include "stdafx.h"
#include "DetailManager.h"
#include "cl_intersect.h"
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include "xrEngine/ssfx_console_vars.h"
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
#if RENDER == R_R4
    m_density_laid = ps_r__Detail_density; // the density the queued slots will be unpacked with
#endif

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
extern ECORE_API float ps_r__Detail_radius;

void CDetailManager::UpdateVisibleM() {
    Fvector EYE = RDEVICE.vCameraPosition_saved;

    CFrustum View;
    View.CreateFromMatrix(RDEVICE.mFullTransform_saved, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    // Кешування констант для зменшення навантаження у циклі
    // Screen Space Shaders: grass draw distance ( r__detail_radius ).
    // The cache geometry is fixed at compile time ( dm_size in DetailManager.h ), so this can only
    // shorten the radius the cache already holds, never extend it - hence the clamp to dm_fade.
    // Slots past it take the "hidden" branch below, so their blades fade back in when the player
    // walks closer instead of popping into view. fade_range follows fade_limit, which means the
    // shrink curve stretches to whatever radius is set: at the full 152 m a blade stays tall far
    // out, at 48 m it shrinks as fast as stock Call of Pripyat did.
#if RENDER == R_R4
    // With SSS switched off ( ssfx_session_enable ) the game's own distance: Infernis built its
    // grass cache for 48 m ( dm_size 24, DetailManager.h:25 ) and faded a blade over all of it.
    float _detail_radius = ssfx_session_enable ? ps_r__Detail_radius : 2 * 24 - 0.5f;
    clamp(_detail_radius, 8.0f, dm_fade);
    const float fade_limit = _detail_radius * _detail_radius;
#else
    // The other renderers: the game's own line, Infernis DetailManager.cpp:141 ( dm_size is the
    // game's 24 there, DetailManager.h ).
    const float fade_limit = dm_fade * dm_fade;
#endif
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
#if RENDER == R_R4
                    // Screen Space Shaders. Monolith DetailManager.cpp:353-357: a slot that leaves
                    // fade range is marked, so its blades fade back in from nothing when it returns
                    // instead of popping into view at full opacity.
                    if (dist_sq > fade_limit) {
                        S.hidden = true;
                        continue;
                    }
#else
                    if (dist_sq > fade_limit) continue;
#endif

                    float alpha = (dist_sq < fade_start) ? 0.f : (dist_sq - fade_start) / fade_range;
                    float alpha_i = 1.f - alpha;
                    float dist_sq_rcp = 1.f / dist_sq;

                    S.frame = RDEVICE.dwFrame + Random.randI(15, 30);
                    
                    for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++) {
                        SlotPart& sp = S.G[sp_id];
                        if (sp.id == DetailSlot::ID_Empty) continue;

                        // The donor calls clear_not_free() here; in this engine xr_vector is
                        // std::vector, whose clear() already keeps the capacity, so clear() is the
                        // same operation under a different name.
                        sp.r_items[0].clear();
                        sp.r_items[1].clear();
                        sp.r_items[2].clear();

                        const float R = objects[sp.id]->bv_sphere.R;
                        const float Rq_drcp = R * R * dist_sq_rcp;

                        // Screen Space Shaders. The body of this loop is the donor's - Monolith
                        // DetailManager.cpp:376-403. It records the distance and the slot centre
                        // that the grass shader fades a blade against, drives the blade's own
                        // opacity, and honours r__no_scale_on_fade. The range-based form around it
                        // is this engine's and does exactly what the donor's pointer walk does.
                        for (auto* item : sp.items) {
                            SlotItem& Item = *item;
                            // Screen Space Shaders: grass height ( r__detail_height ).
                            // See the note in DetailManager_Decompress.cpp for why it is applied
                            // here and not where the donor applies it. The only thing left behind
                            // is the slot bounding box, which the decompressor still builds for
                            // height 1.0 - about a metre of slack on a 2x2 m patch of grass, far
                            // too little to change what the frustum or the occluder culls. ssa
                            // below is computed from this scale, so taller blades survive the
                            // distance cull exactly as they should.
#if RENDER == R_R4
                            // With SSS switched off ( ssfx_session_enable ) the game's own two
                            // lines, Infernis DetailManager.cpp:196-197: no grass height, and a
                            // blade always shrinks as it fades ( no r__no_scale_on_fade ).
                            float scale, ssa;
                            if (ssfx_session_enable) {
                                scale = psDeviceFlags2.test(rsNoScale)
                                            ? (Item.scale_calculated =
                                                   Item.scale * ps_r__Detail_height)
                                            : (Item.scale_calculated =
                                                   Item.scale * ps_r__Detail_height * alpha_i);
                                ssa = psDeviceFlags2.test(rsNoScale) ? scale
                                                                     : scale * scale * Rq_drcp;
                            } else {
                                scale = Item.scale_calculated = Item.scale * alpha_i;
                                ssa = scale * scale * Rq_drcp;
                            }
#else
                            // The other renderers: the game's own two lines, Infernis
                            // DetailManager.cpp:196-197.
                            float scale = Item.scale_calculated = Item.scale * alpha_i;
                            float ssa = scale * scale * Rq_drcp;
#endif
                            if (ssa < r_ssaDISCARD) {
                                Item.alpha_target = 0;
                                continue;
                            }
                            u32 vis_id = 0;
                            if (ssa > r_ssaCHEAP)
                                vis_id = Item.vis_ID;

                            sp.r_items[vis_id].push_back(item);

                            if (S.hidden) {
                                Item.alpha = 0;
                                S.hidden = false;
                            }
                            Item.alpha_target = 1;
                            Item.distance = dist_sq;
                            Item.position = S.vis.sphere.P;
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

// Screen Space Shaders. Monolith DetailManager.cpp:490-512. Called once at the end of the frame:
// it puts the fade distance back to "no fade" after a shadow pass has moved it, and, when grass
// shadows are on, empties the visibility lists that the shadow passes deliberately left filled so
// the same blades could be drawn again into each shadow map.
void CDetailManager::details_clear() {
    // Disable fade, next render will be scene
    fade_distance = 99999;

    if (ps_ssfx_grass_shadows.x <= 0)
        return;

    for (u32 x = 0; x < 3; x++) {
        vis_list& list = m_visibles[x];

        for (u32 O = 0; O < objects.size(); O++) {
            xr_vector<SlotItemVec*>& vis = list[O];
            if (!vis.empty()) {
                // Donor: clear_not_free(). Here xr_vector is std::vector and clear() keeps the
                // capacity, which is what that name means.
                vis.clear();
            }
        }
    }
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
#if RENDER == R_R4
            // Grass density at once, as the grass height ( user 2026-11-04; beyond the donor, which
            // takes r__detail_density at the next level load only, its DetailManager.cpp:100 ):
            // with SSS another slider value lays the cached grass out again, the way Load() does,
            // under the lock cache_Update runs under. Without SSS it waits for the next level load.
            if (ssfx_session_enable && m_density_laid != ps_r__Detail_density) {
                m_density_laid = ps_r__Detail_density;
                cache_Initialize();
            }
#endif
            cache_Update(s_x, s_z, EYE, dm_max_decompress);
            RDEVICE.Statistic->RenderDUMP_DT_Cache.End();

            UpdateVisibleM();
            m_frame_calc.store(RDEVICE.dwFrame, std::memory_order_release);
        }
    }
}