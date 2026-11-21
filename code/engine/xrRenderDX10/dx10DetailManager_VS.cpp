#include "stdafx.h"
#include "xrRenderCommon/DetailManager.h"

#include "xrEngine/igame_persistent.h"
#include "xrEngine/ssfx_console_vars.h"
#include "xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

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
