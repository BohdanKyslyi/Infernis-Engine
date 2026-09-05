// LightTrack.cpp: implementation of the CROS_impl class.
#include "stdafx.h"
#include "LightTrack.h"
#include "xrRender/RenderVisual.h"
#include "xrEngine/xr_object.h"

#ifdef _EDITOR
#include "igame_persistent.h"
#include "environment.h"
#else
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#endif

CROS_impl::CROS_impl() {
    // Усі змінні тепер ініціалізуються у заголовку (C++11 Default Member Initializers)
    // Це забезпечує максимальну швидкодію конструктора
}

void CROS_impl::add(light* source) {
    for (auto& I : track) {
        if (source == I.source) {
            I.frame_touched = Device.dwFrame;
            return;
        }
    }

    track.push_back(Item());
    Item& L = track.back();
    L.frame_touched = Device.dwFrame;
    L.source = source;
    L.cache.verts[0].set(0.f, 0.f, 0.f);
    L.cache.verts[1].set(0.f, 0.f, 0.f);
    L.cache.verts[2].set(0.f, 0.f, 0.f);
    L.test = 0.f;
    L.energy = 0.f;
}

inline bool pred_energy(const CROS_impl::Light& L1, const CROS_impl::Light& L2) noexcept {
    return L1.energy > L2.energy;
}

static const float hdir[lt_hemisamples][3] = {
    { -0.26287f, 0.52573f, 0.80902f },  { 0.27639f, 0.44721f, 0.85065f },
    { -0.95106f, 0.00000f, 0.30902f },  { -0.95106f, 0.00000f, -0.30902f },
    { 0.58779f, 0.00000f, -0.80902f },  { 0.58779f, 0.00000f, 0.80902f },
    { -0.00000f, 0.00000f, 1.00000f },  { 0.52573f, 0.85065f, 0.00000f },
    { -0.26287f, 0.52573f, -0.80902f }, { -0.42533f, 0.85065f, 0.30902f },
    { 0.95106f, 0.00000f, 0.30902f },   { 0.95106f, 0.00000f, -0.30902f },
    { 0.00000f, 1.00000f, 0.00000f },   { -0.58779f, 0.00000f, 0.80902f },
    { -0.72361f, 0.44721f, 0.52573f },  { -0.72361f, 0.44721f, -0.52573f },
    { -0.58779f, 0.00000f, -0.80902f }, { 0.16246f, 0.85065f, -0.50000f },
    { 0.89443f, 0.44721f, 0.00000f },   { -0.85065f, 0.52573f, -0.00000f },
    { 0.16246f, 0.85065f, 0.50000f },   { 0.68819f, 0.52573f, -0.50000f },
    { 0.27639f, 0.44721f, -0.85065f },  { 0.00000f, 0.00000f, -1.00000f },
    { -0.42533f, 0.85065f, -0.30902f }, { 0.68819f, 0.52573f, 0.50000f }
};

inline void CROS_impl::accum_hemi(float* hemi_cube, Fvector3& dir, float scale) {
    if (dir.x > 0) hemi_cube[CUBE_FACE_POS_X] += dir.x * scale;
    else           hemi_cube[CUBE_FACE_NEG_X] -= dir.x * scale;

    if (dir.y > 0) hemi_cube[CUBE_FACE_POS_Y] += dir.y * scale;
    else           hemi_cube[CUBE_FACE_NEG_Y] -= dir.y * scale;

    if (dir.z > 0) hemi_cube[CUBE_FACE_POS_Z] += dir.z * scale;
    else           hemi_cube[CUBE_FACE_NEG_Z] -= dir.z * scale;
}

void CROS_impl::update(IRenderable* O) {
    if (dwFrame == Device.dwFrame) return;
    dwFrame = Device.dwFrame;
    
    if (!O || !O->renderable.visual) return;
    VERIFY(dynamic_cast<CROS_impl*>(O->renderable_ROS()));

    CObject* _object = dynamic_cast<CObject*>(O);

    vis_data& vis = O->renderable.visual->getVisData();
    Fvector position;
    O->renderable.xform.transform_tiny(position, vis.sphere.P);
    position.y += 0.3f * vis.sphere.R;
    
    Fvector direction;
    direction.random_dir();

    std::fill(std::begin(hemi_cube), std::end(hemi_cube), 0.f);

    bool bFirstTime = (0 == result_count);
    calc_sun_value(position, _object);
    calc_sky_hemi_value(position, _object);
    prepare_lights(position, O);

    CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
    Fvector accum = { desc.ambient.x, desc.ambient.y, desc.ambient.z };
    Fvector hemi = { desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z };
    Fvector sun_ = { desc.sun_color.x, desc.sun_color.y, desc.sun_color.z };
    
    hemi.mul((MODE & IRender_ObjectSpecific::TRACE_HEMI) ? hemi_smooth : 0.2f);
    accum.add(hemi);
    
    sun_.mul((MODE & IRender_ObjectSpecific::TRACE_SUN) ? sun_smooth : 0.2f);
    accum.add(sun_);
    
    if (MODE & IRender_ObjectSpecific::TRACE_LIGHTS) {
        Fvector lacc = { 0.f, 0.f, 0.f };
#if RENDER != R_R1
        float hemi_cube_light[NUM_FACES] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
#endif
        for (u32 lit = 0; lit < lights.size(); ++lit) {
            light* L = lights[lit].source;
            float d = L->position.distance_to(position);

#if RENDER != R_R1
            float a = (1.f / (L->attenuation0 + L->attenuation1 * d + L->attenuation2 * d * d) - d * L->falloff) * (L->flags.bStatic ? 1.f : 2.f);
            a = std::max(a, 0.0f);

            Fvector3 dir;
            dir.sub(L->position, position);
            dir.normalize_safe();

            float koef = (lights[lit].color.r + lights[lit].color.g + lights[lit].color.b) / 3.0f * a * ps_r2_dhemi_light_scale;
            accum_hemi(hemi_cube_light, dir, koef);
#else
            float r = L->range;
            float a = std::clamp(1.f - d / (r + EPS), 0.f, 1.f) * (L->flags.bStatic ? 1.f : 2.f);
#endif
            lacc.x += lights[lit].color.r * a;
            lacc.y += lights[lit].color.g * a;
            lacc.z += lights[lit].color.b * a;
        }
        
#if RENDER != R_R1
        const float minHemiValue = 1.f / 255.f;
        float hemi_light = (lacc.x + lacc.y + lacc.z) / 3.0f * ps_r2_dhemi_light_scale;

        hemi_value += hemi_light;
        hemi_value = std::max(hemi_value, minHemiValue);

        for (size_t i = 0; i < NUM_FACES; ++i) {
            hemi_cube[i] += hemi_cube_light[i] * (1.f - ps_r2_dhemi_light_flow) +
                            ps_r2_dhemi_light_flow * hemi_cube_light[(i + NUM_FACES / 2) % NUM_FACES];
            hemi_cube[i] = std::max(hemi_cube[i], minHemiValue);
        }
#endif
        accum.add(lacc);
    } else {
        accum.set(0.1f, 0.1f, 0.1f);
    }

    if (bFirstTime) {
        hemi_smooth = hemi_value;
        std::copy(std::begin(hemi_cube), std::end(hemi_cube), std::begin(hemi_cube_smooth));
    }

    update_smooth();
    approximate = accum;
}

#if RENDER != R_R1
static const s32 s_iUTFirstTimeMin = 1;
static const s32 s_iUTFirstTimeMax = 1;
static const s32 s_iUTPosChangedMin = 3;
static const s32 s_iUTPosChangedMax = 6;
static const s32 s_iUTIdleMin = 1000;
static const s32 s_iUTIdleMax = 2000;

void CROS_impl::smart_update(IRenderable* O) {
    if (!O || !O->renderable.visual) return;

    --ticks_to_update;

    Fvector position;
    VERIFY(dynamic_cast<CROS_impl*>(O->renderable_ROS()));
    vis_data& vis = O->renderable.visual->getVisData();
    O->renderable.xform.transform_tiny(position, vis.sphere.P);

    if (ticks_to_update <= 0) {
        update(O);
        last_position = position;

        if (result_count < lt_hemisamples)
            ticks_to_update = ::Random.randI(s_iUTFirstTimeMin, s_iUTFirstTimeMax + 1);
        else if (sky_rays_uptodate < lt_hemisamples)
            ticks_to_update = ::Random.randI(s_iUTPosChangedMin, s_iUTPosChangedMax + 1);
        else
            ticks_to_update = ::Random.randI(s_iUTIdleMin, s_iUTIdleMax + 1);
    } else {
        if (!last_position.similar(position, 0.15f)) {
            sky_rays_uptodate = 0;
            update(O);
            last_position = position;

            if (result_count < lt_hemisamples)
                ticks_to_update = ::Random.randI(s_iUTFirstTimeMin, s_iUTFirstTimeMax + 1);
            else
                ticks_to_update = ::Random.randI(s_iUTPosChangedMin, s_iUTPosChangedMax + 1);
        }
    }
}
#endif // RENDER!=R_R1

extern float ps_r2_lt_smooth;

void CROS_impl::update_smooth(IRenderable* O) {
    if (dwFrameSmooth == Device.dwFrame) return;

    dwFrameSmooth = Device.dwFrame;

#if RENDER == R_R1
    if (O && (0 == result_count)) update(O); 
#else             
    smart_update(O);
#endif             

    float l_f = std::clamp(Device.fTimeDelta * ps_r2_lt_smooth, 0.f, 1.f);
    float l_i = 1.f - l_f;
    hemi_smooth = hemi_value * l_f + hemi_smooth * l_i;
    sun_smooth = sun_value * l_f + sun_smooth * l_i;
    
    for (size_t i = 0; i < NUM_FACES; ++i) {
        hemi_cube_smooth[i] = hemi_cube[i] * l_f + hemi_cube_smooth[i] * l_i;
    }
}

void CROS_impl::calc_sun_value(Fvector& position, CObject* _object) {
#if RENDER == R_R1
    auto* sun = static_cast<light*>(RImplementation.L_DB->sun_adapted._get());
#else
    auto* sun = static_cast<light*>(RImplementation.Lights.sun_adapted._get());
#endif

    if (MODE & IRender_ObjectSpecific::TRACE_SUN) {
        if (--result_sun < 0) {
            result_sun += ::Random.randI(lt_hemisamples / 4, lt_hemisamples / 2);
            Fvector direction;
            direction.set(sun->direction).invert().normalize();
            sun_value = !(g_pGameLevel->ObjectSpace.RayTest(position, direction, 500.f, collide::rqtBoth, &cache_sun, _object)) ? 1.f : 0.f;
        }
    }
}

void CROS_impl::calc_sky_hemi_value(Fvector& position, CObject* _object) {
    if (MODE & IRender_ObjectSpecific::TRACE_HEMI) {
#if RENDER != R_R1
        sky_rays_uptodate += ps_r2_dhemi_count;
        sky_rays_uptodate = std::min(sky_rays_uptodate, lt_hemisamples);
#endif 

        for (u32 it = 0; it < static_cast<u32>(ps_r2_dhemi_count); ++it) { 
            u32 sample = (result_count < lt_hemisamples) ? result_count++ : (result_iterator++ % lt_hemisamples);

            Fvector direction;
            direction.set(hdir[sample][0], hdir[sample][1], hdir[sample][2]).normalize();
            result[sample] = !g_pGameLevel->ObjectSpace.RayTest(position, direction, 50.f, collide::rqtStatic, &cache[sample], _object);
        }
    }
    
    int _pass = 0;
    for (int it = 0; it < result_count; ++it) {
        if (result[it]) _pass++;
    }
        
    hemi_value = static_cast<float>(_pass) / static_cast<float>(result_count ? result_count : 1);
    hemi_value *= ps_r2_dhemi_sky_scale;

    for (int it = 0; it < result_count; ++it) {
        if (result[it]) {
            Fvector3 temp_dir{ hdir[it][0], hdir[it][1], hdir[it][2] };
            accum_hemi(hemi_cube, temp_dir, ps_r2_dhemi_sky_scale);
        }
    }
}

void CROS_impl::prepare_lights(Fvector& position, IRenderable* O) {
    CObject* _object = dynamic_cast<CObject*>(O);
    float dt = Device.fTimeDelta;

    vis_data& vis = O->renderable.visual->getVisData();
    float radius = vis.sphere.R;
    
    BOOL bTraceLights = MODE & IRender_ObjectSpecific::TRACE_LIGHTS;
    if ((!O->renderable_ShadowGenerate()) && (!O->renderable_ShadowReceive()))
        bTraceLights = FALSE;
        
    if (bTraceLights) {
        Fvector bb_size = { radius, radius, radius };

#if RENDER != R_R1
        g_SpatialSpace->q_box(RImplementation.lstSpatial, 0, STYPE_LIGHTSOURCEHEMI, position, bb_size);
#else
        g_SpatialSpace->q_box(RImplementation.lstSpatial, 0, STYPE_LIGHTSOURCE, position, bb_size);
#endif
        for (u32 o_it = 0; o_it < RImplementation.lstSpatial.size(); ++o_it) {
            ISpatial* spatial = RImplementation.lstSpatial[o_it];
            light* source = static_cast<light*>(spatial->dcast_Light());
            VERIFY(source); 
            
            float R = radius + source->range;
            if (position.distance_to(source->position) < R
#if RENDER != R_R1
                && source->flags.bStatic
#endif
            ) {
                add(source);
            }
        }

        lights.clear();
#if RENDER == R_R1
        float traceR = radius * 0.5f;
#endif

        // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Swap-and-Pop O(1) видалення замість erase() O(N)
        for (s32 id = 0; id < static_cast<s32>(track.size()); ) {
            if (track[id].frame_touched != Device.dwFrame) {
                track[id] = track.back();
                track.pop_back();
                continue; 
            }

            Fvector P, D;
            float amount = 0.f;
            light* xrL = track[id].source;
            Fvector& LP = xrL->position;
            
#if RENDER == R_R1
            P.mad(position, P.random_dir(), traceR); 
#else
            P = position;
#endif
            float f = D.sub(P, LP).magnitude();
            if (g_pGameLevel->ObjectSpace.RayTest(LP, D.div(f), f, collide::rqtStatic, &track[id].cache, _object))
                amount -= lt_dec;
            else
                amount += lt_inc;
                
            track[id].test += amount * dt;
            track[id].test = std::clamp(track[id].test, -0.5f, 1.0f);
            track[id].energy = 0.9f * track[id].energy + 0.1f * track[id].test;

            float E = track[id].energy * xrL->color.intensity();
            if (E > EPS) {
                lights.push_back(CROS_impl::Light());
                CROS_impl::Light& L = lights.back();
                L.source = xrL;
                L.color.mul_rgb(xrL->color, track[id].energy * 0.5f);
                L.energy = track[id].energy * 0.5f;
                if (!xrL->flags.bStatic) {
                    L.color.mul_rgb(0.5f);
                    L.energy *= 0.5f;
                }
            }
            id++;
        }

#if RENDER == R_R1
        auto* sun = static_cast<light*>(RImplementation.L_DB->sun_adapted._get());
        float E = sun_smooth * sun->color.intensity();
        if (E > EPS) {
            lights.push_back(CROS_impl::Light());
            CROS_impl::Light& L = lights.back();
            L.source = sun;
            L.color.mul_rgb(sun->color, sun_smooth * 0.5f);
            L.energy = sun_smooth;
        }
#endif
        std::sort(std::begin(lights), std::end(lights), pred_energy);
    }
}