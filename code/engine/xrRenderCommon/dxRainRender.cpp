#include "stdafx.h"
#include "dxRainRender.h"
#include "xrEngine/Rain.h"
#include "xrEngine/iGame_persistent.h"
#include "xrEngine/Environment.h"
// Screen Space Shaders: ps_ssfx_rain_1 / _2 / _3 and ps_ssfx_rain_drops_setup.
#include "xrEngine/ssfx_console_vars.h"
#if (RENDER != R_R1)
#include "Blender_rain.h"
#endif
#include <immintrin.h> 
#include <algorithm>

constexpr u32 max_desired_items = 6500;
constexpr float source_radius = 25.5f; 
constexpr float source_offset = 40.f;
constexpr float sink_offset = -source_offset;
constexpr float drop_length = 5.f;
constexpr float drop_width = 0.30f;
constexpr u32 particles_cache = 400;
constexpr float particles_time = 0.3f;

dxRainRender::dxRainRender() {
    IReader* F = FS.r_open("$game_meshes$", "dm\\rain.dm");
    VERIFY3(F, "Can't open file.", "dm\\rain.dm");
    DM_Drop = ::RImplementation.model_CreateDM(F);
    
    #if (RENDER == R_R1)
    SH_Rain.create("effects\\rain", "fx\\fx_rain");
    #else
    #if defined(USE_DX11)
        // Screen Space Shaders - rain. Ported from Monolith dxRainRender.cpp:36-43.
        //
        // The donor builds both shaders from its own script files, effects_rain.s and
        // effects_rain_splash.s, which the SSFX folder ships. They are what declares the
        // ssfx_rain_setup constant and reads the $user$ssfx_rain buffer, so the streak brightness,
        // alpha, refraction and reflection settings only reach a shader through this path.
        //
        // Without the addon this engine keeps its own CBlender_rain_drops, so vanilla rain is
        // untouched. That is why the branch is on the detected flag and not a compile-time choice.
        if (RImplementation.o.ssfx_rain) {
            SH_Rain.create("effects\\rain", "fx\\fx_rain");
            SH_Splash.create("effects\\rain_splash", "fx\\fx_rain");
        } else
    #endif
        SH_Rain.create(xr_new<CBlender_rain_drops>(), "rain_drops", "fx\\fx_rain");
    #endif

    hGeom_Rain.create(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);
    hGeom_Drops.create(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, RCache.Vertex.Buffer(), RCache.Index.Buffer());
    FS.r_close(F);
}

dxRainRender::~dxRainRender() { ::RImplementation.model_Delete(DM_Drop); }
void dxRainRender::Copy(IRainRender& _in) { *this = static_cast<const dxRainRender&>(_in); }

void dxRainRender::Render(CEffect_Rain& owner) {
    float factor = g_pGamePersistent->Environment().CurrentEnv->rain_density;
    if (factor < EPS_L) return;

#if RENDER == R_R4
    // Screen Space Shaders: while SSS runs a drop is born 20 m above the camera ( Rain.cpp, from
    // Monolith Rain.cpp:22-24 ), so this pass takes the donor's three distances too, Monolith
    // dxRainRender.cpp:9-11. These names hide the file's constants for the rest of the function;
    // with SSS off they keep the game's values. The other renderers do not compile this block.
    const float source_offset = ssfx_session_enable ? 20.f : ::source_offset;
    // ( with SSS off: the game's max_distance, source_offset * 2 - Rain.cpp:25; the weather editor took
    //   that constant out of this file )
    const float max_distance = ssfx_session_enable ? source_offset * 1.5f : ::source_offset * 2.0f;
    const float sink_offset = -(max_distance - source_offset);
#endif
    // Screen Space Shaders - rain. Ported from Monolith dxRainRender.cpp:65-85.
    //
    // Everything the player can change about a drop arrives here. With the addon installed all
    // five come from SSFX; the constants are only what a build without the addon falls back to.
    float _drop_len = drop_length;
    float _drop_width = drop_width;
    float _drop_speed = 1.0f;
    u32 rain_max_particles = max_desired_items;
    float rain_radius = source_radius;

#if defined(USE_DX11)
    if (RImplementation.o.ssfx_rain) {
        _drop_len = ps_ssfx_rain_1.x;   // "Drop length"
        _drop_width = ps_ssfx_rain_1.y; // "Drop width"
        _drop_speed = ps_ssfx_rain_1.z; // "Drop speed", a multiplier inside Born
        rain_max_particles = u32(ps_ssfx_rain_drops_setup.x);
        rain_radius = ps_ssfx_rain_drops_setup.y;
    }
#endif

    // How the drop count follows the weather, and how far a drop may drift before it is recycled.
    // Both expressions below are the donor's - Monolith dxRainRender.cpp:87 and :99. This engine
    // had 0.5f * ( 1 + factor ) and a wrap radius of radius + 0.5f. They agree with the donor at
    // full rain and differ in a drizzle, and the rain is SSFX's, so the donor's are what run.
    // With SSS off, and on every renderer but DX11, the game's own two lines run instead -
    // Infernis dxRainRender.cpp:45 and :52.
#if defined(USE_DX11)
    u32 desired_items =
        RImplementation.o.ssfx_rain
            ? iFloor(0.01f * (1.f + factor * 99.0f) * static_cast<float>(rain_max_particles))
            : iFloor(0.5f * (1.f + factor) * static_cast<float>(max_desired_items));
#else
    u32 desired_items = iFloor(0.5f * (1.f + factor) * static_cast<float>(max_desired_items));
#endif
    if (desired_items > owner.drops.capacity) desired_items = owner.drops.capacity;
    owner.drops.active_count = desired_items;

    float factor_visual = factor / 2.f + 0.5f;
    Fvector3 f_rain_color = g_pGamePersistent->Environment().CurrentEnv->rain_color;
    u32 u_rain_color = color_rgba_f(f_rain_color.x, f_rain_color.y, f_rain_color.z, factor_visual);
#if defined(USE_DX11)
    float b_radius_wrap_sqr =
        RImplementation.o.ssfx_rain ? xr::sqr(rain_radius * 1.5f) : xr::sqr(source_radius + 0.5f);
#else
    float b_radius_wrap_sqr = xr::sqr(source_radius + 0.5f);
#endif

#if RENDER == R_R4
    // Screen Space Shaders: the plane the donor's wrap below aims a drop back from ( source_offset
    // above the camera ). The weather editor's wrap, for the game's rain, does without it.
    Fplane src_plane;
    Fvector norm = { 0.f, -1.f, 0.f };
    Fvector upper;
    upper.set(Device.vCameraPosition.x, Device.vCameraPosition.y + source_offset, Device.vCameraPosition.z);
    src_plane.build(upper, norm);
#endif

    u32 vOffset;
    FVF::LIT* verts = static_cast<FVF::LIT*>(RCache.Vertex.Lock(desired_items * 4, hGeom_Rain->vb_stride, vOffset));
    FVF::LIT* start = verts;
    const Fvector& vEye = Device.vCameraPosition;

    float dt = Device.fTimeDelta;
    u32 current_time = Device.dwTimeGlobal;

    __m256 v_dt = _mm256_set1_ps(dt);
    u32 count = owner.drops.active_count;
    u32 i = 0;

    for (; i + 8 <= count; i += 8) {
        __m256 v_spd = _mm256_load_ps(&owner.drops.speed[i]);
        __m256 v_mult = _mm256_mul_ps(v_spd, v_dt); 

        __m256 v_px = _mm256_load_ps(&owner.drops.px[i]);
        __m256 v_dx = _mm256_load_ps(&owner.drops.dx[i]);
        _mm256_store_ps(&owner.drops.px[i], _mm256_fmadd_ps(v_dx, v_mult, v_px)); 

        __m256 v_py = _mm256_load_ps(&owner.drops.py[i]);
        __m256 v_dy = _mm256_load_ps(&owner.drops.dy[i]);
        _mm256_store_ps(&owner.drops.py[i], _mm256_fmadd_ps(v_dy, v_mult, v_py));

        __m256 v_pz = _mm256_load_ps(&owner.drops.pz[i]);
        __m256 v_dz = _mm256_load_ps(&owner.drops.dz[i]);
        _mm256_store_ps(&owner.drops.pz[i], _mm256_fmadd_ps(v_dz, v_mult, v_pz));
    }

    for (; i < count; ++i) { 
        float mult = owner.drops.speed[i] * dt;
        owner.drops.px[i] += owner.drops.dx[i] * mult;
        owner.drops.py[i] += owner.drops.dy[i] * mult;
        owner.drops.pz[i] += owner.drops.dz[i] * mult;
    }

	Device.Statistic->TEST1.Begin();
    for (u32 id = 0; id < count; ++id) {
        
        if (owner.drops.dwTime_Life[id] != 0 && owner.drops.dwTime_Hit[id] < current_time) {
            if ((owner.drops.uv_set[id] & 2) == 0) { 
                Fvector hit_pos = { owner.drops.hit_x[id], owner.drops.hit_y[id], owner.drops.hit_z[id] };
                owner.Hit(hit_pos, owner.drops.material_idx[id]);
                owner.drops.uv_set[id] |= 2; 
            }
        }

        if (!owner.drops.is_alive(id, current_time)) {
            owner.Born(id, rain_radius, _drop_speed);
#if defined(USE_DX11)
            // Screen Space Shaders: after a hit the streak runs on for the SSS "Drop length", as before
            // this port; RenewItem ( Born calls it ) counts the game's drop_length. A drop that hits
            // nothing has dwTime_Hit = u32(-1) and ends with its path, as in the donor.
            if (RImplementation.o.ssfx_rain && owner.drops.dwTime_Hit[id] != u32(-1))
                owner.drops.dwTime_Life[id] =
                    owner.drops.dwTime_Hit[id] + iFloor(1000.f * _drop_len / owner.drops.speed[id]);
#endif
        }

        Fvector pos = { owner.drops.px[id], owner.drops.py[id], owner.drops.pz[id] };
        Fvector dir = { owner.drops.dx[id], owner.drops.dy[id], owner.drops.dz[id] };

        // Test the wind-aligned volume at camera height. A strongly tilted
        // drop may be far outside a vertical cylinder while on a valid path.
        const float relative_y = pos.y - vEye.y;
        const float plane_x = weather_motion::rain_plane_offset(
            pos.x, vEye.x, relative_y, dir.x, dir.y);
        const float plane_z = weather_motion::rain_plane_offset(
            pos.z, vEye.z, relative_y, dir.z, dir.y);
#if RENDER == R_R4
        // Screen Space Shaders: the donor's wrap, as before this port ( Monolith dxRainRender.cpp ) -
        // the SSS drop is born source_offset above the camera and tilts 35 degrees at most ( Rain.cpp ).
        if (ssfx_session_enable) {
            Fvector wdir; wdir.set(pos.x - vEye.x, 0.f, pos.z - vEye.z);

            if (wdir.square_magnitude() > b_radius_wrap_sqr) {
                float wlen = std::sqrt(wdir.square_magnitude());
                if ((pos.y - vEye.y) < sink_offset) {
                    owner.drops.invalidate(id); 
                } else {
                    Fvector inv_dir, src_p;
                    inv_dir.invert(dir);
                    wdir.div(wlen);
                    pos.mad(pos, wdir, -(wlen + rain_radius));

                    owner.drops.px[id] = pos.x;
                    owner.drops.py[id] = pos.y;
                    owner.drops.pz[id] = pos.z;

                    if (src_plane.intersectRayPoint(pos, inv_dir, src_p)) {
                        float dist_sqr = pos.distance_to_sqr(src_p);
                        float height = max_distance;
                        u16 dummy_mat = u16(-1);
                        if (owner.RayPick(src_p, dir, height, collide::rqtBoth, dummy_mat)) {
                            if (xr::sqr(height) <= dist_sqr) owner.drops.invalidate(id);
                            else owner.RenewItem(id, height - std::sqrt(dist_sqr), TRUE, dummy_mat);
                        } else {
                            owner.RenewItem(id, max_distance - std::sqrt(dist_sqr), FALSE, dummy_mat);
                        }
                    } else owner.drops.invalidate(id);
                }
            }
        } else
#endif
        if (plane_x * plane_x + plane_z * plane_z > b_radius_wrap_sqr ||
            relative_y < sink_offset || relative_y > source_offset + .5f) {
            // Rebirth schedules a new hit from the new origin. Never wrap a
            // trajectory while keeping an old impact point or negative range.
            owner.Born(id, source_radius);
            pos.set(owner.drops.px[id], owner.drops.py[id], owner.drops.pz[id]);
            dir.set(owner.drops.dx[id], owner.drops.dy[id], owner.drops.dz[id]);
        }

        Fvector pos_trail;
        pos_trail.mad(pos, dir, -_drop_len * factor_visual);

        if (current_time >= owner.drops.dwTime_Hit[id]) {
            Fvector hit_pos = { owner.drops.hit_x[id], owner.drops.hit_y[id], owner.drops.hit_z[id] };
            pos = hit_pos;
            if (pos_trail.y < hit_pos.y) pos_trail = hit_pos;
        }

        {
            Fvector head_to_eye, trail_to_eye;
            head_to_eye.sub(pos, vEye);
            trail_to_eye.sub(pos_trail, vEye);

            const float head_depth = head_to_eye.dotproduct(Device.vCameraDirection);
            const float trail_depth = trail_to_eye.dotproduct(Device.vCameraDirection);

            constexpr float rain_near_depth = 1.0f;

            if (head_depth <= rain_near_depth || trail_depth <= rain_near_depth)
                continue;
        }

        Fvector sC, lineD;
        sC.sub(pos, pos_trail);

        const float segment_len_sqr = sC.square_magnitude();
        if (segment_len_sqr < EPS_L)
            continue;

        lineD.set(sC);
        lineD.normalize_safe();

        sC.mul(0.5f);
        float sR = sC.magnitude();
        sC.add(pos_trail);

        if (!::Render->ViewBase.testSphere_dirty(sC, sR))
            continue;

		static constexpr Fvector2 UV[2][4] = {
			{ { 0.f, 1.f }, { 0.f, 0.f }, { 1.f, 1.f }, { 1.f, 0.f } },
			{ { 1.f, 0.f }, { 1.f, 1.f }, { 0.f, 0.f }, { 0.f, 1.f } }
		};

        Fvector P, lineTop, camDir;

        // Per-drop eye direction keeps the rain strip facing the camera.
        camDir.sub(sC, vEye);
        camDir.normalize_safe();

        lineTop.crossproduct(camDir, lineD);

        // Degenerate case: camera looks almost exactly along the streak.
        if (lineTop.square_magnitude() < EPS)
            lineTop.set(Device.vCameraRight);

        lineTop.normalize_safe();
        
        float w = _drop_width;
        u32 s = owner.drops.uv_set[id] & 1;
        
        P.mad(pos_trail, lineTop, -w); verts->set(P, u_rain_color, UV[s][0].x, UV[s][0].y); verts++;
        P.mad(pos_trail, lineTop, w);  verts->set(P, u_rain_color, UV[s][1].x, UV[s][1].y); verts++;
        P.mad(pos, lineTop, -w);       verts->set(P, u_rain_color, UV[s][2].x, UV[s][2].y); verts++;
        P.mad(pos, lineTop, w);        verts->set(P, u_rain_color, UV[s][3].x, UV[s][3].y); verts++;
    }
	
    Device.Statistic->TEST1.End();
    u32 vCount = static_cast<u32>(verts - start);
    RCache.Vertex.Unlock(vCount, hGeom_Rain->vb_stride);

    if (vCount) {
        RCache.set_CullMode(CULL_NONE);
        RCache.set_xform_world(Fidentity);
        RCache.set_Shader(SH_Rain);

#if defined(USE_DX11)
        // Screen Space Shaders. Monolith dxRainRender.cpp:252. effects_rain.ps reads all four:
        // .x alpha, .y brightness, .z refraction, .w reflection - the "Drop visibility" and
        // "Drop brightness" sliders among them.
        if (RImplementation.o.ssfx_rain)
            RCache.set_c("ssfx_rain_setup", ps_ssfx_rain_2);
#endif

#if (RENDER != R_R1)
        if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv) {
            CEnvDescriptor* env = g_pGamePersistent->Environment().CurrentEnv;
            RCache.set_c("L_sun_dir_w", env->sun_dir.x, env->sun_dir.y, env->sun_dir.z, 0.f);
            RCache.set_c("L_sun_color", env->sun_color.x, env->sun_color.y, env->sun_color.z, 1.f);
            RCache.set_c("L_ambient", env->ambient.x, env->ambient.y, env->ambient.z, 1.f);
        }

        Fvector4 dyn_light_pos[4];
        Fvector4 dyn_light_dir[4]; 
        Fvector4 dyn_light_color[4];
        
        for (int j = 0; j < 4; j++) {
            dyn_light_pos[j].set(0.f, 0.f, 0.f, 0.f);
            dyn_light_dir[j].set(0.f, -1.f, 0.f, 0.f);
            dyn_light_color[j].set(0.f, 0.f, 0.f, 0.f);
        }

        int light_count = 0;
        struct LightDist { light* l; float dist; };
        xr_vector<LightDist> nearest_lights;

        for (auto* l : ::RImplementation.Lights_LastFrame) {
            if (!l || !l->flags.bActive) continue;
#if RENDER == R_R4
            // The faces of a shadowed point light are active only for the Screen Space Shaders
            // screen space shadows (light::export_, DX11 only). They were never active before, so
            // they stay out of the rain lighting, exactly as without the addon.
            if (l->flags.type == IRender_Light::OMNIPART) continue;
#endif
            
            if (l->flags.type != IRender_Light::POINT && 
                l->flags.type != IRender_Light::SPOT && 
                l->flags.type != IRender_Light::OMNIPART) continue;

            float dist = Device.vCameraPosition.distance_to(l->position);
            
            if (dist < (l->range + 15.0f)) {
                nearest_lights.push_back({l, dist});
            }
        }

        std::sort(nearest_lights.begin(), nearest_lights.end(), [](const LightDist& a, const LightDist& b) {
            return a.dist < b.dist;
        });

        for (size_t k = 0; k < nearest_lights.size() && light_count < 4; ++k) {
            light* l = nearest_lights[k].l;
            dyn_light_pos[light_count].set(l->position.x, l->position.y, l->position.z, 1.0f / (l->range + EPS));
            
            float is_spot = (l->flags.type == IRender_Light::SPOT) ? 1.0f : 0.0f;
            dyn_light_dir[light_count].set(l->direction.x, l->direction.y, l->direction.z, is_spot);
            
            dyn_light_color[light_count].set(l->color.r, l->color.g, l->color.b, std::cos(l->cone * 0.5f));
            light_count++;
        }

        RCache.set_c("dyn_light_pos_0", dyn_light_pos[0]);
        RCache.set_c("dyn_light_dir_0", dyn_light_dir[0]);
        RCache.set_c("dyn_light_color_0", dyn_light_color[0]);
        
        RCache.set_c("dyn_light_pos_1", dyn_light_pos[1]);
        RCache.set_c("dyn_light_dir_1", dyn_light_dir[1]);
        RCache.set_c("dyn_light_color_1", dyn_light_color[1]);
        
        RCache.set_c("dyn_light_pos_2", dyn_light_pos[2]);
        RCache.set_c("dyn_light_dir_2", dyn_light_dir[2]);
        RCache.set_c("dyn_light_color_2", dyn_light_color[2]);
        
        RCache.set_c("dyn_light_pos_3", dyn_light_pos[3]);
        RCache.set_c("dyn_light_dir_3", dyn_light_dir[3]);
        RCache.set_c("dyn_light_color_3", dyn_light_color[3]);
#endif

        RCache.set_Geometry(hGeom_Rain);
        RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, vCount, 0, vCount / 2);
        RCache.set_CullMode(CULL_CCW);
    }

    CEffect_Rain::Particle* P = owner.particle_active;
    if (!P) return;

    {
        _IndexStream& _IS = RCache.Index;

        // Screen Space Shaders - the splash where a drop lands. Monolith dxRainRender.cpp:68/81/263.
        //
        // Deviation from the donor, and deliberate: Monolith binds a reference to the detail
        // model's own shader ( ref_shader& _splash_SH = DM_Drop->shader ) and assigns SH_Splash
        // through it, which overwrites the model's shader permanently on the first rainy frame.
        // A local copy renders the same thing without altering the loaded model.
        ref_shader splash_SH = DM_Drop->shader;
#if defined(USE_DX11)
        if (RImplementation.o.ssfx_rain) {
            splash_SH = SH_Splash;
            // .x alpha, .y refraction - the "Splash visibility" slider.
            RCache.set_c("ssfx_rain_setup", ps_ssfx_rain_3);
        }
#endif
        RCache.set_Shader(splash_SH);

        Fmatrix mXform, mScale;
        int pcount = 0;
        u32 v_offset, i_offset;
        u32 vCount_Lock = particles_cache * DM_Drop->number_vertices;
        u32 iCount_Lock = particles_cache * DM_Drop->number_indices;
        
        auto* v_ptr = static_cast<IRender_DetailModel::fvfVertexOut*>(RCache.Vertex.Lock(vCount_Lock, hGeom_Drops->vb_stride, v_offset));
        u16* i_ptr = _IS.Lock(iCount_Lock, i_offset);
        
        while (P) {
            CEffect_Rain::Particle* next = P->next;
            P->time -= dt;
            if (P->time < 0) {
                owner.p_free(P);
                P = next;
                continue;
            }

            if (::Render->ViewBase.testSphere_dirty(P->bounds.P, P->bounds.R)) {
                float scale = P->time / particles_time;
                mScale.scale(scale, scale, scale);
                mXform.mul_43(P->mXForm, mScale);

                DM_Drop->transfer(mXform, v_ptr, u_rain_color, i_ptr, pcount * DM_Drop->number_vertices);
                v_ptr += DM_Drop->number_vertices;
                i_ptr += DM_Drop->number_indices;
                pcount++;

                if (pcount >= static_cast<int>(particles_cache)) {
                    u32 dwNumPrimitives = iCount_Lock / 3;
                    RCache.Vertex.Unlock(vCount_Lock, hGeom_Drops->vb_stride);
                    _IS.Unlock(iCount_Lock);
                    RCache.set_Geometry(hGeom_Drops);
                    RCache.Render(D3DPT_TRIANGLELIST, v_offset, 0, vCount_Lock, i_offset, dwNumPrimitives);

                    v_ptr = static_cast<IRender_DetailModel::fvfVertexOut*>(RCache.Vertex.Lock(vCount_Lock, hGeom_Drops->vb_stride, v_offset));
                    i_ptr = _IS.Lock(iCount_Lock, i_offset);
                    pcount = 0;
                }
            }
            P = next;
        }

        vCount_Lock = pcount * DM_Drop->number_vertices;
        iCount_Lock = pcount * DM_Drop->number_indices;
        u32 dwNumPrimitives = iCount_Lock / 3;
        RCache.Vertex.Unlock(vCount_Lock, hGeom_Drops->vb_stride);
        _IS.Unlock(iCount_Lock);
        
        if (pcount) {
            RCache.set_Geometry(hGeom_Drops);
            RCache.Render(D3DPT_TRIANGLELIST, v_offset, 0, vCount_Lock, i_offset, dwNumPrimitives);
        }
    }
}

const Fsphere& dxRainRender::GetDropBounds() const { return DM_Drop->bv_sphere; }