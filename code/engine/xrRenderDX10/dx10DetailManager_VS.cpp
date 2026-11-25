#include "stdafx.h"
#include "xrRenderCommon/DetailManager.h"

#include "xrEngine/igame_persistent.h"
#include "xrEngine/ssfx_console_vars.h"
#include "xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

#if RENDER == R_R4
// Edited by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" - the task system and the
// parts of "cpu_frame_stats 1"
// Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" - система задач і
// частини "cpu_frame_stats 1"
#include "xrCore/xrTasks.h"
#include "xrEngine/frame_parts.h"
#endif

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

// SSFX motion vectors: previous frame swing state
static u32 prev_frame = u32(-1);
static float prev_time = 0;
static Fvector4 prev_dir1 = { 0, 0, 0, 0 }, prev_dir2 = { 0, 0, 0, 0 };

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

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

// Screen Space Shaders. Monolith dx10DetailManager_VS.cpp:39-52, unchanged. Walks a blade's
// opacity towards its target at one unit per second, which is what makes grass fade in and out
// instead of appearing and vanishing.
float GoToValue(float& current, float go_to) {
    float diff = abs(current - go_to);

    float r_value = Device.fTimeDelta;

    if (diff - r_value <= 0) {
        current = go_to;
        return 0;
    }

    return current < go_to ? r_value : -r_value;
}
#if RENDER == R_R4

// Written by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" ( "mt_grass", on by default,
// xrEngine\xr_ioc_cmd.cpp; the pieces: DetailManager.h ). hw_Render calls mt_prepare once, before its
// three draws: the blades of every list of the three variants are worked out at once on the task
// system's workers and on this thread ( xrCore\xrTasks.h ), into pieces of up to 32 lists. A piece runs
// exactly the code the old walk of hw_Render_dump runs for each blade ( below, unchanged ), list by
// list in the same order, the opacity step and the "break" at a faded blade included. The first pass of
// each grass type then copies the finished values into the constant buffer in the same batches and
// draws them. The old way, as before: "mt_grass 0", "cpu_tasks 0" or a single CPU, fewer than 64 lists
// in the call, a frame whose lists were not emptied ( m_mt_lists_stale ), and every pass after the
// first.
// Створено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" ( "mt_grass", увімкнено
// за замовчуванням, xrEngine\xr_ioc_cmd.cpp; шматки: DetailManager.h ). hw_Render викликає mt_prepare
// один раз, перед своїми трьома малюваннями: травинки всіх списків трьох варіантів рахуються разом на
// робочих потоках системи задач і в цьому потоці ( xrCore\xrTasks.h ) у шматки до 32 списків. Шматок
// виконує рівно той код, який старий обхід hw_Render_dump виконує для кожної травинки ( нижче, без
// змін ), список за списком у тому самому порядку, разом із кроком непрозорості й "break" на згаслій
// травинці. Перший прохід кожного типу трави потім копіює готові значення в буфер констант тими самими
// пачками й малює їх. По-старому, як раніше: "mt_grass 0", "cpu_tasks 0" або один процесор, менше ніж
// 64 списки у виклику, кадр, чиї списки не спорожнили ( m_mt_lists_stale ), і кожен прохід після
// першого.
extern ENGINE_API int ps_mt_grass; // xrEngine\xr_ioc_cmd.cpp

namespace {
constexpr u32 mt_piece_lists = 32; // lists in one piece
constexpr u32 mt_lists_min = 64;   // fewer lists in a call: the old way ( handing out would cost more )
} // namespace

// the pieces of this hw_Render call, worked out on all cores; m_mt_ready = they are there
void CDetailManager::mt_prepare() {
    m_mt_ready = false;
    if (!ps_mt_grass || m_mt_lists_stale || !hw_BatchSize || xrTasks::Slots() < 2)
        return;
    const u32 objects_count = u32(objects.size());
    u32 pieces_count = 0, lists = 0;
    for (u32 v = 0; v < 3; ++v) {
        // the shader element hw_Render draws the variant with: 0 for the waves, 1 for the still grass
        const u32 lod_id = v ? 0 : 1;
        for (u32 O = 0; O < objects_count; ++O) {
            u32 n = u32(m_visibles[v][O].size());
            if (n && !objects[O]->shader->E[lod_id]->passes.size())
                n = 0; // never drawn, so its blades stay as they are, as before
            m_mt_first[v][O] = pieces_count;
            m_mt_count[v][O] = (n + mt_piece_lists - 1) / mt_piece_lists;
            pieces_count += m_mt_count[v][O];
            lists += n;
        }
    }
    if (lists < mt_lists_min)
        return;
    if (m_mt_pieces.size() < pieces_count)
        m_mt_pieces.resize(pieces_count);
    mt_piece* pieces = m_mt_pieces.data();
    for (u32 v = 0; v < 3; ++v)
        for (u32 O = 0; O < objects_count; ++O) {
            mt_piece* piece = pieces + m_mt_first[v][O];
            const u32 n = m_mt_count[v][O] ? u32(m_visibles[v][O].size()) : 0;
            for (u32 begin = 0; begin < n; begin += mt_piece_lists, ++piece) {
                piece->var_id = v;
                piece->object = O;
                piece->list_begin = begin;
                piece->list_end = std::min(n, begin + mt_piece_lists);
                piece->blades = 0;
            }
        }
    u32 grain = pieces_count / (xrTasks::Slots() * 4);
    if (grain < 1)
        grain = 1;
    xrTasks::ParallelFor(pieces_count, grain, [this, pieces](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i)
            mt_prepare_piece(pieces[i]);
    });
    m_mt_ready = true;
}

// one piece: for each blade the old walk's code ( hw_Render_dump below ), the values into the piece
void CDetailManager::mt_prepare_piece(mt_piece& piece) {
    xr_vector<SlotItemVec*>& vis = m_visibles[piece.var_id][piece.object];
    u32 most = 0; // every blade of its lists: the most the piece can take
    for (u32 l = piece.list_begin; l < piece.list_end; ++l)
        most += u32(vis[l]->size());
    if (piece.arr.size() < most * 4)
        piece.arr.resize(most * 4);
    if (piece.ex.size() < most)
        piece.ex.resize(most);
    Fvector4* c_storage = piece.arr.data();
    Fvector4* c_ExData = piece.ex.data();
    u32 dwBatch = 0; // the blades taken ( the names are the old walk's )
    for (u32 l = piece.list_begin; l < piece.list_end; ++l) {
        SlotItemVec* items = vis[l];
        auto _iI = items->begin();
        auto _iE = items->end();
        for (; _iI != _iE && dwBatch < most; _iI++) {
            SlotItem& Instance = **_iI;
            u32 base = dwBatch * 4;

            float scale = Instance.scale_calculated;
            if (ssfx_session_enable) {
                Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);
                if (fade_distance <= -1)
                    scale *= 1.0f - Instance.position.distance_to_xz_sqr(light_position) * 0.005f;
                else if (Instance.distance > fade_distance)
                    scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;
                if (scale <= 0 || Instance.alpha <= 0)
                    break;
            }

            Fmatrix& M = Instance.mRotY;
            c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
            c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
            c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
            float h = Instance.c_hemi;
            float s = Instance.c_sun;
            c_storage[base + 3].set(s, s, s, h);
            c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y, Instance.normal.z,
                                  Instance.alpha);
            dwBatch++;
        }
    }
    piece.blades = dwBatch;
}
#endif

void CDetailManager::hw_Load_Shaders() {
    // Create shader to access constant storage
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
    // Render-prepare
    //	Update timer
    //	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks
    //more choppy!
    float fDelta = Device.fTimeGlobal - m_global_time_old;
    if ((fDelta < 0) || (fDelta > 1))
        fDelta = 0.03;
    m_global_time_old = Device.fTimeGlobal;

    m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
    m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
    m_time_pos += fDelta * swing_current.speed;

    // float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
    // float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
    float tm_rot1 = m_time_rot_1;
    float tm_rot2 = m_time_rot_2;

    Fvector4 dir1, dir2;
    dir1.set(std::sin(tm_rot1), 0, std::cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
    dir2.set(std::sin(tm_rot2), 0, std::cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

    // Setup geometry and DMA
    RCache.set_Geometry(hw_Geom);

#if RENDER == R_R4
    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" - the blades of the three
    // draws below are first worked out at once on all cores ( mt_prepare above ); "cpu_frame_stats 1"
    // gets that time, the drawing's time, the blades drawn and the draw calls ( xrEngine\device.cpp )
    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" - травинки трьох
    // малювань нижче спершу рахуються разом на всіх ядрах ( mt_prepare вище ); "cpu_frame_stats 1"
    // отримує цей час, час малювання, намальовані травинки й виклики малювання ( xrEngine\device.cpp )
    const u64 mt_start = frame_part_begin();
    m_mt_blades = 0;
    m_mt_draws = 0;
    mt_prepare();
    const u64 mt_prepared = mt_start ? frame_part_now() : 0;
#endif

    // Wave0
    float scale = 1.f / float(quant);
    Fvector4 wave, prev_wave;
    Fvector4 consts;
    consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
    // wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,
    // Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
    prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
    // RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);
    // // consts  RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir1);																					//
    // wind-dir  hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0);

    // Wave1
    // wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,
    // Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
    prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
    // RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir2);																					//
    // wind-dir  hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0);

    // Still
    consts.set(scale, scale, scale, 1.f);
    // RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,
    // 1.f);  RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
    // hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
    hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 0, 1);

#if RENDER == R_R4
    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - the worked-out blades are used up; this call's
    // parts for "cpu_frame_stats 1"
    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - пораховані травинки використано; частини
    // цього виклику для "cpu_frame_stats 1"
    m_mt_ready = false;
    if (mt_start) {
        frame_part_add_n(FP_GRASS_PREP, mt_prepared - mt_start, m_mt_blades);
        frame_part_add_n(FP_GRASS_DRAW, frame_part_now() - mt_prepared, m_mt_draws);
    }
#endif

    if (prev_frame != Device.dwFrame) {
        prev_frame = Device.dwFrame;

        // Prev frame swing time and dir
        prev_time = m_time_pos;
        prev_dir1 = dir1;
        prev_dir2 = dir2;
    }
}

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave,
                                    const Fvector4& wind, const Fvector4& prev_wave,
                                    const Fvector4& prev_wind, u32 var_id, u32 lod_id) {
    static shared_str strConsts("consts");
    static shared_str strWave("wave");
    static shared_str strDir2D("dir2D");
    static shared_str strArray("array");
    static shared_str strXForm("xform");
    static shared_str strWavePrev("wave_prev");
    static shared_str strDir2DPrev("dir2D_prev");

    // Screen Space Shaders: per-blade data ( terrain normal + opacity ) and the align switch the
    // grass vertex shader reads. Ported from Monolith dx10DetailManager_VS.cpp.
    static shared_str strExData("exdata");
    static shared_str strGrassAlign("grass_align");

    // Screen Space Shaders: interactive grass. deffer_grass.vs:72-130 loops over these three
    // constants and bends every blade away from whatever they describe. They MUST be written
    // whenever SSFX_INT_GRASS is defined - the define comes from ps_ssfx_grass_interactive.y
    // ( r4.cpp:1656 ), and if the constants are left unwritten the shader reads whatever the
    // constant buffer happened to hold from an earlier draw and throws the grass across the
    // screen in long streaks. Ported from Monolith dx10DetailManager_VS.cpp:151-248.
    static shared_str strPrevPos("benders_prevpos");
    static shared_str strPos("benders_pos");
    static shared_str strGrassSetup("benders_setup");

    // Grass benders data
    IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
    Fvector4 player_pos = { 0, 0, 0, 0 };
    int BendersQty = std::min(16, int(ps_ssfx_grass_interactive.y) + 1);

    // Add Player?
    if (ps_ssfx_grass_interactive.x > 0)
        player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z,
                       -1);

    Device.Statistic->RenderDUMP_DT_Count = 0;

    // Matrices and offsets
    u32 vOffset = 0;
    u32 iOffset = 0;

    vis_list& list = m_visibles[var_id];

    CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
    Fvector c_sun, c_ambient, c_hemi;
    c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
    c_sun.mul(.5f);
    c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
    c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

    // Iterate
    for (u32 O = 0; O < objects.size(); O++) {
        CDetail& Object = *objects[O];
        xr_vector<SlotItemVec*>& vis = list[O];
        if (!vis.empty()) {
            for (u32 iPass = 0; iPass < Object.shader->E[lod_id]->passes.size(); ++iPass) {
                // Setup matrices + colors (and flush it as necessary)
                // RCache.set_Element				(Object.shader->E[lod_id]);
                RCache.set_Element(Object.shader->E[lod_id], iPass);
                RImplementation.apply_lmaterial();

                //	This could be cached in the corresponding consatant buffer
                //	as it is done for DX9
                RCache.set_c(strConsts, consts);
                RCache.set_c(strWave, wave);
                RCache.set_c(strDir2D, wind);
                RCache.set_c(strXForm, Device.mFullTransform);
                RCache.set_c(strWavePrev, prev_wave);
                RCache.set_c(strDir2DPrev, prev_wind);
                RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);

                if (ps_ssfx_grass_interactive.y > 0) {
                    RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

                    Fvector4* c_grass;
                    {
                        void* GrassData;
                        RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2,
                                                  &GrassData, 0, 0);
                        c_grass = (Fvector4*)GrassData;
                    }
                    VERIFY(c_grass);

                    if (c_grass) {
                        c_grass[0].set(player_pos);
                        c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

                        for (int Bend = 1; Bend < BendersQty; Bend++) {
                            c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y,
                                              GData.pos[Bend].z, GData.radius_curr[Bend]);
                            c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y,
                                                   GData.dir[Bend].z, GData.str[Bend]);
                        }
                    }

                    Fvector4* c_prev_grass;
                    {
                        void* prev_GrassData;
                        RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2,
                                                  &prev_GrassData, 0, 0);
                        c_prev_grass = (Fvector4*)prev_GrassData;
                    }
                    VERIFY(c_prev_grass);

                    if (c_prev_grass) {
                        for (int Bend = 0; Bend < BendersQty; Bend++) {
                            c_prev_grass[Bend].set(GData.prev_pos[Bend]);
                            c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
                        }
                    }
                }

                // ref_constant constArray = RCache.get_c(strArray);
                // VERIFY(constArray);

                // u32			c_base				= x_array->vs.index;
                // Fvector4*	c_storage			=
                // RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
                // Screen Space Shaders: one float4 per blade in the batch.
                Fvector4* c_ExData = 0;
                {
                    void* pExtraData;
                    RCache.get_ConstantDirect(strExData, hw_BatchSize * sizeof(Fvector4),
                                              &pExtraData, 0, 0);
                    c_ExData = (Fvector4*)pExtraData;
                }
                VERIFY(c_ExData);

                Fvector4* c_storage = 0;
                //	Map constants to memory directly
                {
                    void* pVData;
                    RCache.get_ConstantDirect(strArray, hw_BatchSize * sizeof(Fvector4) * 4,
                                              &pVData, 0, 0);
                    c_storage = (Fvector4*)pVData;
                }
                VERIFY(c_storage);

                u32 dwBatch = 0;

                xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
                xr_vector<SlotItemVec*>::iterator _vE = vis.end();
#if RENDER == R_R4
                // Written by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" - the first
                // pass takes the blades mt_prepare worked out on all cores: copied into the constants
                // in runs and drawn in the same batches as by the walk below, which then has nothing
                // left to do.
                // Створено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" - перший
                // прохід бере травинки, пораховані mt_prepare на всіх ядрах: вони копіюються в
                // константи шматками й малюються тими самими пачками, що й в обході нижче, якому потім
                // нічого не лишається.
                if (m_mt_ready && iPass == 0) {
                    const u32 first = m_mt_first[var_id][O];
                    const u32 last = first + m_mt_count[var_id][O];
                    for (u32 p = first; p < last; ++p) {
                        const mt_piece& piece = m_mt_pieces[p];
                        for (u32 done = 0; done < piece.blades;) {
                            const u32 run = std::min(piece.blades - done, hw_BatchSize - dwBatch);
                            memcpy(c_storage + dwBatch * 4, piece.arr.data() + done * 4,
                                   run * 4 * sizeof(Fvector4));
                            if (c_ExData)
                                memcpy(c_ExData + dwBatch, piece.ex.data() + done,
                                       run * sizeof(Fvector4));
                            dwBatch += run;
                            done += run;
                            if (dwBatch == hw_BatchSize) {
                                // flush ( the walk's own lines below )
                                Device.Statistic->RenderDUMP_DT_Count += dwBatch;
                                u32 dwCNT_verts = dwBatch * Object.number_vertices;
                                u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                                RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset,
                                              dwCNT_prims);
                                RCache.stat.r.s_details.add(dwCNT_verts);
                                m_mt_blades += dwBatch;
                                m_mt_draws++;

                                // restart
                                dwBatch = 0;

                                //	Remap constants to memory directly ( as the walk below does )
                                {
                                    void* pVData;
                                    RCache.get_ConstantDirect(
                                        strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                                    c_storage = (Fvector4*)pVData;
                                }
                                VERIFY(c_storage);
                            }
                        }
                    }
                    _vI = _vE;
                }
#endif
                for (; _vI != _vE; _vI++) {
                    SlotItemVec* items = *_vI;
                    auto _iI = items->begin();
                    auto _iE = items->end();
                    for (; _iI != _iE; _iI++) {
                        SlotItem& Instance = **_iI;
                        u32 base = dwBatch * 4;

#if RENDER == R_R4
                        float scale = Instance.scale_calculated;

                        // With SSS switched off ( ssfx_session_enable ) none of the block below:
                        // the game's own blade, Infernis dx10DetailManager_VS.cpp:174 - no fade-in
                        // and no fade towards a lamp or the edge of a shadow map.
                        if (ssfx_session_enable) {
                            // Screen Space Shaders. Monolith dx10DetailManager_VS.cpp:288-300.
                            Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

                            // Sort of fade using the scale
                            // fade_distance == -1 use light_position to define "fade", anything
                            // else uses fade_distance
                            if (fade_distance <= -1)
                                scale *= 1.0f -
                                         Instance.position.distance_to_xz_sqr(light_position) *
                                             0.005f;
                            else if (Instance.distance > fade_distance)
                                scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;

                            if (scale <= 0 || Instance.alpha <= 0)
                                break;
                        }

                        // Build matrix ( 3x4 matrix, last row - color )
                        // float scale = Instance.scale_calculated;
#else
                        // Build matrix ( 3x4 matrix, last row - color )
                        float scale = Instance.scale_calculated;
#endif
                        Fmatrix& M = Instance.mRotY;
                        c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
                        c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
                        c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
                        // RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,
                        // M._31*scale,	M._41	);  RCache.set_ca(&*constArray, base+1, M._12*scale,
                        // M._22*scale,	M._32*scale,	M._42	);  RCache.set_ca(&*constArray,
                        // base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

                        // Build color
                        // R2 only needs hemisphere
                        float h = Instance.c_hemi;
                        float s = Instance.c_sun;
                        c_storage[base + 3].set(s, s, s, h);

                        // Screen Space Shaders: terrain normal + opacity for this blade.
                        if (c_ExData)
                            c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y,
                                                  Instance.normal.z, Instance.alpha);
                        // RCache.set_ca(&*constArray, base+3, s,				s,
                        // s,				h		);
                        dwBatch++;
                        if (dwBatch == hw_BatchSize) {
                            // flush
                            Device.Statistic->RenderDUMP_DT_Count += dwBatch;
                            u32 dwCNT_verts = dwBatch * Object.number_vertices;
                            u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                            // RCache.get_ConstantCache_Vertex().b_dirty				=
                            // TRUE;  RCache.get_ConstantCache_Vertex().get_array_f().dirty
                            // (c_base,c_base+dwBatch*4);
                            RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset,
                                          dwCNT_prims);
                            RCache.stat.r.s_details.add(dwCNT_verts);
#if RENDER == R_R4
                            // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - "cpu_frame_stats 1":
                            // the blades and the draw calls
                            // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - "cpu_frame_stats 1":
                            // травинки й виклики малювання
                            m_mt_blades += dwBatch;
                            m_mt_draws++;
#endif

                            // restart
                            dwBatch = 0;

                            //	Remap constants to memory directly (just in case anything goes
                            //wrong)
                            {
                                void* pVData;
                                RCache.get_ConstantDirect(
                                    strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                                c_storage = (Fvector4*)pVData;
                            }
                            VERIFY(c_storage);
                        }
                    }
                }
                // flush if nessecary
                if (dwBatch) {
                    Device.Statistic->RenderDUMP_DT_Count += dwBatch;
                    u32 dwCNT_verts = dwBatch * Object.number_vertices;
                    u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                    // RCache.get_ConstantCache_Vertex().b_dirty				=
                    // TRUE;  RCache.get_ConstantCache_Vertex().get_array_f().dirty
                    // (c_base,c_base+dwBatch*4);
                    RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset,
                                  dwCNT_prims);
                    RCache.stat.r.s_details.add(dwCNT_verts);
#if RENDER == R_R4
                    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - "cpu_frame_stats 1": the
                    // blades and the draw calls
                    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - "cpu_frame_stats 1":
                    // травинки й виклики малювання
                    m_mt_blades += dwBatch;
                    m_mt_draws++;
#endif
                }
            }
            // Clean up
            // KD: we must not clear vis on r2 since we want details shadows
            //
            // Screen Space Shaders. The condition is the donor's - Monolith
            // dx10DetailManager_VS.cpp:361-372. With grass shadows on, the list is deliberately
            // kept so the shadow passes can draw the same blades again, and details_clear()
            // empties it at the end of the frame.
            //
            // The outer scopeLensPass test is this engine's 3D scope, which is untouchable: the
            // lens and the main view render the same visibility result inside one frame, so the
            // list must survive until the main pass has consumed it.
            if (!Device.scopeLensPass) {
#if RENDER == R_R4
                // With SSS switched off ( ssfx_session_enable ) the game's own clean-up, Infernis
                // dx10DetailManager_VS.cpp:233: the list is emptied after every draw. The 3D scope
                // test above stays - it belongs to the scope, not to SSS.
                if (!ssfx_session_enable)
                    vis.clear();
                else if (ps_ssfx_grass_shadows.x <= 0) {
                    if (!psDeviceFlags2.test(rsGrassShadow) ||
                        ((ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) &&
                          (RImplementation.PHASE_SMAP == RImplementation.phase)) // phase smap with shadows
                         || (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) &&
                             (RImplementation.PHASE_NORMAL == RImplementation.phase) &&
                             (!RImplementation.is_sun())) // phase normal with shadows without sun
                         || (!ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS) &&
                             (RImplementation.PHASE_NORMAL == RImplementation.phase))
                         )) // phase normal without shadows
                        // Donor: clear_not_free(). std::vector::clear() keeps the capacity here.
                        vis.clear();
                }
#else
                // The other renderers: the game's own clean-up, Infernis
                // dx10DetailManager_VS.cpp:234-235.
                vis.clear();
#endif
            }
        }
        vOffset += hw_BatchSize * Object.number_vertices;
        iOffset += hw_BatchSize * Object.number_indices;
    }
}
