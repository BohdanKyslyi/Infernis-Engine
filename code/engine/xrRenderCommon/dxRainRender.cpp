#include "stdafx.h"
#include "dxRainRender.h"
#include "xrEngine/Rain.h"
#include "xrEngine/iGame_persistent.h"
#include "xrEngine/Environment.h" 
#include "Blender_rain.h" 
#include <immintrin.h> 
#include <algorithm>

constexpr u32 max_desired_items = 6500;
constexpr float source_radius = 25.5f; 
constexpr float source_offset = 40.f;
constexpr float max_distance = source_offset * 2.0f; 
constexpr float sink_offset = -(max_distance - source_offset);
constexpr float drop_length = 5.f;
constexpr float drop_width = 0.30f;
constexpr u32 particles_cache = 400;
constexpr float particles_time = 0.3f;

dxRainRender::dxRainRender() {
    IReader* F = FS.r_open("$game_meshes$", "dm\\rain.dm");
    VERIFY3(F, "Can't open file.", "dm\\rain.dm");
    DM_Drop = ::RImplementation.model_CreateDM(F);
    
    SH_Rain.create(xr_new<CBlender_rain_drops>(), "rain_drops", "fx\\fx_rain"); 
    
    hGeom_Rain.create(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);
    hGeom_Drops.create(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, RCache.Vertex.Buffer(), RCache.Index.Buffer());
    FS.r_close(F);
}

dxRainRender::~dxRainRender() { ::RImplementation.model_Delete(DM_Drop); }
void dxRainRender::Copy(IRainRender& _in) { *this = static_cast<const dxRainRender&>(_in); }

void dxRainRender::Render(CEffect_Rain& owner) {
    float factor = g_pGamePersistent->Environment().CurrentEnv->rain_density;
    if (factor < EPS_L) return;

    u32 desired_items = iFloor(0.5f * (1.f + factor) * static_cast<float>(max_desired_items));
    if (desired_items > owner.drops.capacity) desired_items = owner.drops.capacity; 
    owner.drops.active_count = desired_items;

    float factor_visual = factor / 2.f + 0.5f;
    Fvector3 f_rain_color = g_pGamePersistent->Environment().CurrentEnv->rain_color;
    u32 u_rain_color = color_rgba_f(f_rain_color.x, f_rain_color.y, f_rain_color.z, factor_visual);
    float b_radius_wrap_sqr = xr::sqr(source_radius + 0.5f);

    Fplane src_plane;
    Fvector norm = { 0.f, -1.f, 0.f };
    Fvector upper;
    upper.set(Device.vCameraPosition.x, Device.vCameraPosition.y + source_offset, Device.vCameraPosition.z);
    src_plane.build(upper, norm);

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
        
        if (owner.drops.dwTime_Hit[id] < current_time) {
            if ((owner.drops.uv_set[id] & 2) == 0) { 
                Fvector hit_pos = { owner.drops.hit_x[id], owner.drops.hit_y[id], owner.drops.hit_z[id] };
                owner.Hit(hit_pos, owner.drops.material_idx[id]);
                owner.drops.uv_set[id] |= 2; 
            }
        }

        if (!owner.drops.is_alive(id, current_time)) {
            owner.Born(id, source_radius);
            owner.drops.uv_set[id] &= ~2; 

            Fvector p = { owner.drops.px[id], owner.drops.py[id], owner.drops.pz[id] };
            Fvector d = { owner.drops.dx[id], owner.drops.dy[id], owner.drops.dz[id] };

            float height = max_distance;
            u16 dummy_mat = u16(-1);
            if (owner.RayPick(p, d, height, collide::rqtBoth, dummy_mat)) {
                owner.RenewItem(id, height, TRUE, dummy_mat);
                owner.drops.hit_x[id] = p.x + d.x * height;
                owner.drops.hit_y[id] = p.y + d.y * height;
                owner.drops.hit_z[id] = p.z + d.z * height;
            } else {
                owner.RenewItem(id, max_distance, FALSE, dummy_mat);
                owner.drops.hit_x[id] = p.x + d.x * max_distance;
                owner.drops.hit_y[id] = p.y + d.y * max_distance;
                owner.drops.hit_z[id] = p.z + d.z * max_distance;
            }
            owner.drops.dwTime_Life[id] = owner.drops.dwTime_Hit[id] + iFloor(1000.f * drop_length / owner.drops.speed[id]);
        }

        Fvector pos = { owner.drops.px[id], owner.drops.py[id], owner.drops.pz[id] };
        Fvector dir = { owner.drops.dx[id], owner.drops.dy[id], owner.drops.dz[id] };

        Fvector wdir; wdir.set(pos.x - vEye.x, 0.f, pos.z - vEye.z);
        
        if (wdir.square_magnitude() > b_radius_wrap_sqr) {
            float wlen = std::sqrt(wdir.square_magnitude());
            if ((pos.y - vEye.y) < sink_offset) {
                owner.drops.invalidate(id); 
            } else {
                Fvector inv_dir, src_p;
                inv_dir.invert(dir);
                wdir.div(wlen);
                pos.mad(pos, wdir, -(wlen + source_radius));
                
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

        Fvector pos_trail;
        pos_trail.mad(pos, dir, -drop_length * factor_visual);

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
        
        float w = drop_width;
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

        #if (RENDER != R_R1)
        int light_count = 0;
        struct LightDist { light* l; float dist; };
        xr_vector<LightDist> nearest_lights;

        for (auto* l : ::RImplementation.Lights_LastFrame) {
            if (!l || !l->flags.bActive) continue;
            
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
        #endif

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

        RCache.set_Geometry(hGeom_Rain);
        RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, vCount, 0, vCount / 2);
        RCache.set_CullMode(CULL_CCW);
    }

    CEffect_Rain::Particle* P = owner.particle_active;
    if (!P) return;

    {
        _IndexStream& _IS = RCache.Index;
        RCache.set_Shader(DM_Drop->shader);

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