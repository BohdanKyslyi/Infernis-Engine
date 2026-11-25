#include "stdafx.h"
#include "xrEngine/igame_persistent.h"
#include "xrRenderCommon/FBasicVisual.h"
#include "xrEngine/customhud.h"
#include "xrEngine/xr_object.h"

#include "xrRenderCommon/QueryHelper.h"
// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the render parts
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини рендеру
#include "r4_frame_parts.h"
// Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the visual types ( MT_* )
// Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - типи візуалів ( MT_* )
#include "xrEngine/fmesh.h"
// Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - the particles' update batch ( r4_ps_batch_update )
// Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - пачка оновлення частинок ( r4_ps_batch_update )
#include "xrRenderCommon/ParticleEffect.h"
#include "xrRenderCommon/ParticleGroup.h"
#include "xrEngine/PS_instance.h"
#include "xrEngine/frame_parts.h"
#include "xrCore/xrTasks.h"

// Written by Ivan Danko, 2026-11-15: FPS fix step 1 "do not draw what cannot be seen" ( "r__far_cull",
// "r__occlusion_ext", xrRender_console.cpp ). Far small: an object whose diameter on the screen
// ( measured on a 1080-pixel-high screen with this view's zoom - the 3D scope lens has its own ) is
// under the limit is neither drawn nor updated in this view; characters and monsters always stay;
// the objects the game would drop anyway at its own size limit are dropped before their update
// now. Static meshes: r__dsgraph_build.cpp. Occlusion test: the 3D scope lens view uses the main
// view's buffer of this frame, the main view after the lens gets its test back ( it stayed off
// while aiming ), the transparent pass ( particles ) skips what this frame's test found hidden.
// "cpu_frame_stats 1" prints what was cut.
// Створено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS "не малювати те, чого не видно"
// ( "r__far_cull", "r__occlusion_ext", xrRender_console.cpp ). Дрібне далеке: об'єкт, діаметр
// якого на екрані ( міряється на екрані висотою 1080 пікселів зі збільшенням цього виду - лінза
// 3D-прицілу має своє ) менший за межу, у цьому виді не малюється й не оновлюється; персонажі й
// монстри лишаються завжди; об'єкти, які гра й так відкинула б за своєю межею розміру, тепер
// відкидаються до оновлення. Статичні сітки: r__dsgraph_build.cpp. Перевірка перекриття: вид
// лінзи 3D-прицілу використовує буфер основного виду цього кадру, основний вид після лінзи
// отримує свою перевірку назад ( під час прицілювання вона лишалася вимкненою ), прохід
// прозорого ( частинки ) пропускає те, що перевірка цього кадру визнала прихованим.
// "cpu_frame_stats 1" пише, скільки відкинуто.
extern int ps_r__far_cull;
extern int ps_r__occlusion_ext;
extern ENGINE_API int ps_cpu_frame_stats;
extern float r_ssaDISCARD;
float r4_far_cut_static_k = 0.f;           // r__dsgraph_build.cpp
int r4_far_cut_count_view = -1;            // r__dsgraph_build.cpp
u32 r4_far_cut_static_count[2] = { 0, 0 }; // r__dsgraph_build.cpp

namespace {
float far_cut_k_objects = 0.f, far_cut_k_particles = 0.f;
int far_cut_view = -1;         // this render_main counts for: -1 none, 0 the main view, 1 the lens
bool vis_forward_pass = false; // render_forward's render_main
u32 lens_hom_frame = u32(-1);  // the frame whose 3D scope lens pass switched the test off

// what was cut ( "cpu_frame_stats 1" ): this frame per view, the 5 s window per view
u32 vis_cut_particles[2] = { 0, 0 }, vis_cut_objects[2] = { 0, 0 }, vis_cut_hidden[2] = { 0, 0 };
struct vis_cut_window {
    u32 start = 0;
    u32 frames[2] = { 0, 0 };
    double particles[2] = { 0, 0 }, objects[2] = { 0, 0 }, statics[2] = { 0, 0 }, hidden[2] = { 0, 0 };
};
vis_cut_window vis_cut;

// render_main: this view's limits. The diameter on a 1080-pixel-high screen = radius * scale /
// distance, scale = mProject._22 * 1080 ( the view's own zoom ); cut when radius^2 * k < distance^2.
void far_cut_begin(bool count) {
    const int level = ps_r__far_cull;
    if (level <= 0) {
        far_cut_k_objects = far_cut_k_particles = 0.f;
        r4_far_cut_static_k = 0.f;
        far_cut_view = r4_far_cut_count_view = -1;
        return;
    }
    const float scale = Device.mProject._22 * 1080.f;
    const float px_objects = level >= 2 ? 4.f : 2.f;
    const float px_particles = level >= 2 ? 6.f : 3.f;
    far_cut_k_objects = xr::sqr(scale / px_objects);
    far_cut_k_particles = xr::sqr(scale / px_particles);
    r4_far_cut_static_k = far_cut_k_objects;
    far_cut_view = count ? (Device.scopeLensPass ? 1 : 0) : -1;
    r4_far_cut_count_view = far_cut_view;
}

void far_cut_end() {
    far_cut_k_objects = far_cut_k_particles = 0.f;
    r4_far_cut_static_k = 0.f;
    far_cut_view = r4_far_cut_count_view = -1;
}

// a renderable under the limit, or one the game's own size limit drops anyway after its update
bool far_cut_renderable(IRenderable* renderable, ISpatial* spatial) {
    if (far_cut_k_objects <= 0.f)
        return false;
    const dxRender_Visual* V = (const dxRender_Visual*)renderable->renderable.visual;
    if (!V || V->Type == MT_SKELETON_ANIM)
        return false; // characters and monsters always stay
    const bool particles = V->Type == MT_PARTICLE_EFFECT || V->Type == MT_PARTICLE_GROUP;
    const float R = spatial->spatial.sphere.R;
    const float distSQ = Device.vCameraPosition.distance_to_sqr(spatial->spatial.sphere.P) + EPS;
    if (R * R * (particles ? far_cut_k_particles : far_cut_k_objects) >= distSQ &&
        R > r_ssaDISCARD * distSQ)
        return false;
    if (far_cut_view >= 0)
        (particles ? vis_cut_particles : vis_cut_objects)[far_cut_view]++;
    return true;
}

// the transparent pass: this frame's result of this view, else a test of this frame's buffer
BOOL forward_visible(CHOM& hom, const vis_data& v) {
    BOOL visible;
    if (v.hom_tested == Device.dwFrame)
        visible = v.hom_frame != Device.dwFrame + 1; // hidden = tested again next frame
    else if (Device.dwFrame < v.hom_frame)
        visible = TRUE; // found visible within the last frames
    else
        visible = hom.visible_frame(v.box);
    if (!visible)
        vis_cut_hidden[Device.scopeLensPass ? 1 : 0]++;
    return visible;
}

// the end of a view: add its frame; the main view prints the window every 5 s
void vis_cut_view_end(bool lens) {
    const int view = lens ? 1 : 0;
    if (ps_cpu_frame_stats) {
        vis_cut.frames[view]++;
        vis_cut.particles[view] += vis_cut_particles[view];
        vis_cut.objects[view] += vis_cut_objects[view];
        vis_cut.statics[view] += r4_far_cut_static_count[view];
        vis_cut.hidden[view] += vis_cut_hidden[view];
    }
    vis_cut_particles[view] = vis_cut_objects[view] = vis_cut_hidden[view] = 0;
    r4_far_cut_static_count[view] = 0;
    if (!ps_cpu_frame_stats) {
        vis_cut = vis_cut_window();
        return;
    }
    if (lens)
        return;
    if (!vis_cut.start)
        vis_cut.start = Device.dwTimeContinual;
    if (Device.dwTimeContinual - vis_cut.start < 5000)
        return;
    const double m = double(vis_cut.frames[0]);
    const double l = vis_cut.frames[1] ? double(vis_cut.frames[1]) : 1.0;
    Msg("* Visibility cut [a frame, main view | 3D scope lens view]: far small ( r__far_cull %d ): "
        "particles %.0f | %.0f, objects %.0f | %.0f, static meshes %.0f | %.0f | hidden in the "
        "transparent pass ( r__occlusion_ext %d ): %.0f | %.0f | lens frames %u of %u",
        ps_r__far_cull, vis_cut.particles[0] / m, vis_cut.particles[1] / l, vis_cut.objects[0] / m,
        vis_cut.objects[1] / l, vis_cut.statics[0] / m, vis_cut.statics[1] / l, ps_r__occlusion_ext,
        vis_cut.hidden[0] / m, vis_cut.hidden[1] / l, vis_cut.frames[1], vis_cut.frames[0]);
    vis_cut = vis_cut_window();
    vis_cut.start = Device.dwTimeContinual;
}
} // namespace

IC bool pred_sp_sort(ISpatial* _1, ISpatial* _2) {
    float d1 = _1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition);
    float d2 = _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition);
    return d1 < d2;
}

// Only the centre of the second view can be seen through an aimed scope.
// Narrow visibility queries without changing the actual rendering projection:
// screen-space sampling and the weapon's point of aim stay exactly the same.
static Fmatrix scope_visibility_transform() {
    if (!Device.scopeLensPass || !pSettings ||
        !pSettings->section_exist("shader_extensions") ||
        !pSettings->line_exist("shader_extensions", "scope_world_cull_scale"))
        return Device.mFullTransform;

    const float scale = pSettings->r_float("shader_extensions", "scope_world_cull_scale");
    if (!(scale >= 0.5f && scale < 1.f))
        return Device.mFullTransform;

    const float half_fov = atanf(tanf(deg2rad(Device.fFOV * 0.5f)) * scale);
    Fmatrix projection, transform;
    projection.build_projection(2.f * half_fov, Device.fASPECT, VIEWPORT_NEAR,
                                g_pGamePersistent->Environment().CurrentEnv->far_plane);
    transform.mul(projection, Device.mView);
    return transform;
}

// Written by Ivan Danko, 2026-11-17: FPS plan step 2b "particles on all cores" ( "mt_particles", on by
// default, xrEngine\xr_ioc_cmd.cpp ). A view no longer updates and hands over each particle effect the
// moment it meets it: it collects them, and after its walk over the objects updates all of them in one
// batch on the task system's workers and on this thread ( xrCore\xrTasks.h ), then hands them to the
// render as before. Each effect is updated at most once a frame ( the stamp on its visual ): the zfill,
// main, transparent and 3D scope lens views and the shadow passes ( r__dsgraph_render.cpp ) share that
// update. Kept on this thread, after the batch: groups that start child effects ( they create and
// delete effects ) and effects that collide with the world ( the collision query is not made for many
// threads ). The update itself is the game's ( xrGame\ParticlesObject.cpp, BatchUpdate ), which also
// leaves these effects out of its scheduler. "mt_particles 0" = every effect is updated where it is
// met, as before.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS "частинки на всіх ядрах" ( "mt_particles",
// увімкнено за замовчуванням, xrEngine\xr_ioc_cmd.cpp ). Вид більше не оновлює й не передає кожен
// ефект частинок тієї миті, коли його зустрічає: він їх збирає, а після обходу об'єктів оновлює всі
// однією пачкою в робочих потоках системи задач і в цьому потоці ( xrCore\xrTasks.h ), потім передає
// їх рендеру, як раніше. Кожен ефект оновлюється щонайбільше раз на кадр ( позначка на його візуалі ):
// види zfill, основний, прозорий і лінзи 3D-приціла та проходи тіней ( r__dsgraph_render.cpp ) ділять
// це оновлення. У цьому потоці, після пачки, лишаються: групи, що запускають дочірні ефекти ( вони
// створюють і видаляють ефекти ), та ефекти, що стикаються зі світом ( запит зіткнень не розрахований
// на багато потоків ). Саме оновлення - гри ( xrGame\ParticlesObject.cpp, BatchUpdate ), яка також не
// оновлює ці ефекти у своєму планувальнику. "mt_particles 0" = кожен ефект оновлюється там, де його
// зустріли, як раніше.
extern thread_local bool r4_ps_batch_thread; // ParticleEffect.cpp

// the batch is on: the switch and the game's entry
bool r4_ps_batch_on() { return g_ps_batch_update && psDeviceFlags.test(mtParticles); }

// a particle effect or group ( the objects of the batch )
bool r4_ps_batch_is(IRenderable* renderable) {
    dxRender_Visual* visual = (dxRender_Visual*)renderable->renderable.visual;
    return visual && (visual->Type == MT_PARTICLE_EFFECT || visual->Type == MT_PARTICLE_GROUP);
}

namespace {
// an effect whose update has to stay on this thread ( see above )
bool r4_ps_batch_main_only(dxRender_Visual* visual) {
    if (visual->Type == MT_PARTICLE_EFFECT) {
        const PS::CPEDef* def = static_cast<PS::CParticleEffect*>(visual)->GetDefinition();
        return !def || def->m_Flags.is(PS::CPEDef::dfCollision);
    }
    const PS::CParticleGroup* group = static_cast<PS::CParticleGroup*>(visual);
    const PS::CPGDef* def = group->GetDefinition();
    if (!def)
        return true;
    for (const PS::CPGDef::SEffect* effect : def->m_Effects)
        if (effect->m_Flags.is_any(PS::CPGDef::SEffect::flOnPlayChild |
                                   PS::CPGDef::SEffect::flOnBirthChild |
                                   PS::CPGDef::SEffect::flOnDeadChild))
            return true;
    for (const PS::CParticleGroup::SItem& item : group->items) {
        if (!item._children_related.empty() || !item._children_free.empty())
            return true;
        if (!item._effect)
            continue;
        const PS::CPEDef* item_def =
            static_cast<PS::CParticleEffect*>(item._effect)->GetDefinition();
        if (item_def && item_def->m_Flags.is(PS::CPEDef::dfCollision))
            return true;
    }
    return false;
}

xr_vector<IRenderable*> r4_ps_batch_workers; // this call's effects for all cores
xr_vector<IRenderable*> r4_ps_batch_main;    // this call's effects for this thread
xr_vector<IRenderable*> r4_ps_view;          // render_main: the view's effects
} // namespace

// the update batch of the effects met ( each once a frame ); on the main thread
void r4_ps_batch_update(const xr_vector<IRenderable*>& list) {
    const u32 frame = Device.dwFrame;
    r4_ps_batch_workers.clear();
    r4_ps_batch_main.clear();
    for (IRenderable* renderable : list) {
        dxRender_Visual* visual = (dxRender_Visual*)renderable->renderable.visual;
        dxParticleCustom* particles = static_cast<dxParticleCustom*>(visual);
        if (particles->ps_batch_frame == frame)
            continue; // taken this frame already ( an earlier view, or met twice )
        particles->ps_batch_frame = frame;
        (r4_ps_batch_main_only(visual) ? r4_ps_batch_main : r4_ps_batch_workers)
            .push_back(renderable);
    }
    if (r4_ps_batch_workers.empty() && r4_ps_batch_main.empty())
        return;
    const u64 start = frame_part_begin();
    std::atomic<u32> updated(0);
    const u32 count = u32(r4_ps_batch_workers.size());
    u32 grain = count / (xrTasks::Slots() * 8);
    if (grain < 4)
        grain = 4;
    xrTasks::ParallelFor(count, grain, [&updated](u32 begin, u32 end) {
        const bool was = r4_ps_batch_thread;
        r4_ps_batch_thread = true;
        u32 done = 0;
        for (u32 i = begin; i < end; ++i)
            if (g_ps_batch_update(r4_ps_batch_workers[i]))
                ++done;
        r4_ps_batch_thread = was;
        if (done)
            updated.fetch_add(done, std::memory_order_relaxed);
    });
    u32 total = updated.load(std::memory_order_relaxed);
    for (IRenderable* renderable : r4_ps_batch_main)
        if (g_ps_batch_update(renderable))
            ++total;
    if (start)
        frame_part_add_n(FP_PARTICLES, frame_part_now() - start, total);
}

void CRender::render_main(Fmatrix& m_ViewProjection, bool _fportals) {
    PIX_EVENT(render_main);
    //	Msg						("---begin");
    marker++;
    // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - this view's far small limits ( the main
    // calc counts what is cut )
    // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - межі дрібного далекого для цього
    // виду ( основний розрахунок рахує відкинуте )
    far_cut_begin(_fportals);

    // Calculate sector(s) and their objects
    if (pLastSector) {
        //!!!
        //!!! BECAUSE OF PARALLEL HOM RENDERING TRY TO DELAY ACCESS TO HOM AS MUCH AS POSSIBLE
        //!!!
        {
            // Traverse object database
            g_SpatialSpace->q_frustum(lstRenderables, ISpatial_DB::O_ORDERED,
                                      STYPE_RENDERABLE + STYPE_LIGHTSOURCE, ViewBase);

            // (almost) Exact sorting order (front-to-back)
            std::sort(lstRenderables.begin(), lstRenderables.end(), pred_sp_sort);

            // Determine visibility for dynamic part of scene
            set_Object(0);
            u32 uID_LTRACK = 0xffffffff;
            if (phase == PHASE_NORMAL) {
                uLastLTRACK++;
                if (lstRenderables.size())
                    uID_LTRACK = uLastLTRACK % lstRenderables.size();

                // update light-vis for current entity / actor
                CObject* O = g_pGameLevel->CurrentViewEntity();
                if (O) {
                    CROS_impl* R = (CROS_impl*)O->ROS();
                    if (R)
                        R->update(O);
                }

                // update light-vis for selected entity
                // track lighting environment
                if (lstRenderables.size()) {
                    IRenderable* renderable = lstRenderables[uID_LTRACK]->dcast_Renderable();
                    if (renderable) {
                        CROS_impl* T = (CROS_impl*)renderable->renderable_ROS();
                        if (T)
                            T->update(renderable);
                    }
                }
            }
        }

        // Traverse sector/portal structure
        PortalTraverser.traverse(
            pLastSector, ViewBase, Device.vCameraPosition, m_ViewProjection,
            CPortalTraverser::VQ_HOM + CPortalTraverser::VQ_SSA + CPortalTraverser::VQ_FADE
            //. disabled scissoring (HW.Caps.bScissor?CPortalTraverser::VQ_SCISSOR:0)	// generate
            //scissoring info
        );

        // Determine visibility for static geometry hierrarhy
        for (u32 s_it = 0; s_it < PortalTraverser.r_sectors.size(); s_it++) {
            CSector* sector = (CSector*)PortalTraverser.r_sectors[s_it];
            dxRender_Visual* root = sector->root();
            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++) {
                set_Frustum(&(sector->r_frustums[v_it]));
                add_Geometry(root);
            }
        }

        // Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - the view's particle effects wait for one
        // update batch after this walk ( r4_ps_batch_update above )
        // Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - ефекти частинок виду чекають на одну
        // пачку оновлення після цього обходу ( r4_ps_batch_update вище )
        const bool ps_batch = r4_ps_batch_on();
        // Traverse frustums
        for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++) {
            ISpatial* spatial = lstRenderables[o_it];
            spatial->spatial_updatesector();
            CSector* sector = (CSector*)spatial->spatial.sector;
            if (0 == sector)
                continue; // disassociated from S/P structure

            if (spatial->spatial.type & STYPE_LIGHTSOURCE) {
                // lightsource
                light* L = (light*)(spatial->dcast_Light());
                VERIFY(L);
                float lod = L->get_LOD();
                if (lod > EPS_L) {
                    vis_data& vis = L->get_homdata();
                    if (HOM.visible(vis))
                        Lights.add_light(L);
                }
                continue;
            }

            if (PortalTraverser.i_marker != sector->r_marker)
                continue; // inactive (untouched) sector
            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++) {
                CFrustum& view = sector->r_frustums[v_it];
                if (!view.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
                    continue;

                if (spatial->spatial.type & STYPE_RENDERABLE) {
                    // renderable
                    IRenderable* renderable = spatial->dcast_Renderable();
                    VERIFY(renderable);
                    // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - far small ( r__far_cull )
                    // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - дрібне далеке
                    // ( r__far_cull )
                    if (far_cut_renderable(renderable, spatial))
                        break; // exit loop on frustums

                    // Occlusion
                    //	casting is faster then using getVis method
                    vis_data& v_orig = ((dxRender_Visual*)renderable->renderable.visual)->vis;
                    vis_data v_copy = v_orig;
                    v_copy.box.xform(renderable->renderable.xform);
                    // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the transparent pass takes
                    // this frame's occlusion result ( r__occlusion_ext )
                    // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - прохід прозорого
                    // бере результат перевірки перекриття цього кадру ( r__occlusion_ext )
                    BOOL bVisible = (vis_forward_pass && ps_r__occlusion_ext)
                                        ? forward_visible(HOM, v_copy)
                                        : HOM.visible(v_copy);
                    v_orig.marker = v_copy.marker;
                    v_orig.accept_frame = v_copy.accept_frame;
                    v_orig.hom_frame = v_copy.hom_frame;
                    v_orig.hom_tested = v_copy.hom_tested;
                    if (!bVisible)
                        break; // exit loop on frustums

                    // Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - a particle effect waits for the
                    // batch
                    // Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - ефект частинок чекає на пачку
                    if (ps_batch && r4_ps_batch_is(renderable)) {
                        r4_ps_view.push_back(renderable);
                        break; // exit loop on frustums
                    }
                    // Rendering
                    set_Object(renderable);
                    renderable->renderable_Render();
                    set_Object(0);
                }
                break; // exit loop on frustums
            }
        }
        // Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - the view's particle effects: one update
        // batch on all cores, then into the render as before, in the order met
        // Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - ефекти частинок виду: одна пачка
        // оновлення на всіх ядрах, потім у рендер, як раніше, у порядку зустрічі
        if (!r4_ps_view.empty()) {
            r4_ps_batch_update(r4_ps_view);
            for (IRenderable* renderable : r4_ps_view) {
                set_Object(renderable);
                renderable->renderable_Render();
                set_Object(0);
            }
            r4_ps_view.clear();
        }
        if (g_pGameLevel && (phase == PHASE_NORMAL) && !Device.scopeLensPass)
            g_hud->Render_Last(); // HUD
    } else {
        set_Object(0);
        if (g_pGameLevel && (phase == PHASE_NORMAL) && !Device.scopeLensPass)
            g_hud->Render_Last(); // HUD
    }
    // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the limits end with this view ( the
    // shadow passes never cut )
    // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - межі закінчуються з цим видом
    // ( проходи тіней ніколи не відкидають )
    far_cut_end();
}

void CRender::render_menu() {
    PIX_EVENT(render_menu);
    //	Globals
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();

    // Main Render
    {
        Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
        g_pGamePersistent->OnRenderPPUI_main();                  // PP-UI
    }

    // Distort
    {
        FLOAT ColorRGBA[4] = { 127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f };
        Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
        HW.pContext->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
        g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
    }

    // Actual Display
    Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
    RCache.set_Shader(Target->s_menu);
    RCache.set_Geometry(Target->g_menu);

    Fvector2 p0, p1;
    u32 Offset;
    u32 C = color_rgba(255, 255, 255, 255);
    float _w = float(Device.dwWidth);
    float _h = float(Device.dwHeight);
    float d_Z = EPS_S;
    float d_W = 1.f;
    p0.set(.5f / _w, .5f / _h);
    p1.set((_w + .5f) / _w, (_h + .5f) / _h);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
    pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
    pv++;
    pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
    pv++;
    pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
    pv++;
    pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
    pv++;
    RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;
void CRender::Render() {
    PIX_EVENT(CRender_Render);

    if (Device.scopeLensPass)
        m_bScopeLensRendered = false;
    g_r = 1;
    VERIFY(0 == mapDistort.size());

    rmNormal();

    bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
    if (_menu_pp) {
        render_menu();
        return;
    };

    IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
    bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

    if (!(g_pGameLevel && g_hud) || bMenu) {
        Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
        return;
    }

    if (m_bFirstFrameAfterReset) {
        m_bFirstFrameAfterReset = false;
        return;
    }

    //.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

    // Configure
    RImplementation.o.distortion = FALSE; // disable distorion
    Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
    BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) &&
                (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS);
    if (o.sunstatic)
        bSUN = FALSE;
    // Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false",
    // bSUN?"true":"false");

    // HOM
    Fmatrix visibility_transform = scope_visibility_transform();
    ViewBase.CreateFromMatrix(visibility_transform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    View = 0;
    if (!Device.scopeLensPass && !ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC)) {
        HOM.Enable();
        HOM.Render(ViewBase);
    } else if (Device.scopeLensPass) {
        // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the 3D scope lens view tests against
        // the main view's buffer of this frame ( the same eye ) and keeps the main view's state
        // ( r__occlusion_ext; 0 = off, as before ). Its lights switch the test off below, so the
        // main view switches it back on.
        // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - вид лінзи 3D-прицілу
        // перевіряється по буферу основного виду цього кадру ( те саме око ) і не змінює стану
        // основного виду ( r__occlusion_ext; 0 = вимкнено, як раніше ). Його світло нижче вимикає
        // перевірку, тож основний вид вмикає її знову.
        if (ps_r__occlusion_ext) {
            HOM.EnableFrame();
            CHOM::SetLensView(true);
        } else
            HOM.Disable();
        lens_hom_frame = Device.dwFrame;
    } else if (lens_hom_frame == Device.dwFrame) {
        // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the main view after the 3D scope lens
        // pass gets its occlusion test back, as in a frame without the lens ( it stayed off while
        // aiming )
        // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - основний вид після проходу
        // лінзи 3D-прицілу отримує перевірку перекриття назад, як у кадрі без лінзи ( під час
        // прицілювання вона лишалася вимкненою )
        HOM.EnableFrame();
    }

    //******* Z-prefill calc - DEFERRER RENDERER
    if (ps_r2_ls_flags.test(R2FLAG_ZFILL)) {
        PIX_EVENT(DEFER_Z_FILL);
        Device.Statistic->RenderCALC.Begin();
        float z_distance = ps_r2_zfill;
        Fmatrix m_zfill, m_project;
        m_project.build_projection(
            deg2rad(Device.fFOV /* *Device.fASPECT*/), Device.fASPECT, VIEWPORT_NEAR,
            z_distance * g_pGamePersistent->Environment().CurrentEnv->far_plane);
        m_zfill.mul(m_project, Device.mView);
        r_pmask(true, false); // enable priority "0"
        set_Recorder(NULL);
        phase = PHASE_SMAP;
        render_main(m_zfill, false);
        r_pmask(true, false); // disable priority "1"
        Device.Statistic->RenderCALC.End();

        // flush
        Target->phase_scene_prepare();
        RCache.set_ColorWriteEnable(FALSE);
        r_dsgraph_render_graph(0);
        RCache.set_ColorWriteEnable();
    } else {
        Target->phase_scene_prepare();
    }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the render parts ( "cpu_frame_stats
    // 1", r4_frame_parts.h ): visibility, HOM and the z-prefill end here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини рендеру ( "cpu_frame_stats
    // 1", r4_frame_parts.h ): тут закінчуються видимість, HOM і попереднє заповнення глибини
    r4_frame_parts_mark(R4P_VISIBILITY);

    //*******
    // Sync point
    Device.Statistic->RenderDUMP_Wait_S.Begin();
    if (1) {
        CTimer T;
        T.Start();
        BOOL result = FALSE;
        HRESULT hr = S_FALSE;
        // while	((hr=q_sync_point[q_sync_count]->GetData
        // (&result,sizeof(result),D3DGETDATA_FLUSH))==S_FALSE) {
        while ((hr = GetData(q_sync_point[q_sync_count], &result, sizeof(result))) == S_FALSE) {
            if (!SwitchToThread())
                Sleep(ps_r2_wait_sleep);
            if (T.GetElapsed_ms() > 500) {
                result = FALSE;
                break;
            }
        }
    }
    Device.Statistic->RenderDUMP_Wait_S.End();
    q_sync_count = (q_sync_count + 1) % HW.Caps.iGPUNum;
    // CHK_DX
    // (q_sync_point[q_sync_count]->Issue(D3DISSUE_END));
    CHK_DX(EndQuery(q_sync_point[q_sync_count]));
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the wait for the GPU ends here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується очікування
    // відеокарти
    r4_frame_parts_mark(R4P_GPU_WAIT);

    //******* Main calc - DEFERRER RENDERER
    // Main calc
    Device.Statistic->RenderCALC.Begin();
    r_pmask(true, false, true); // enable priority "0",+ capture wmarks
    if (bSUN)
        set_Recorder(&main_coarse_structure);
    else
        set_Recorder(NULL);
    phase = PHASE_NORMAL;
    render_main(visibility_transform, true);
    set_Recorder(NULL);
    r_pmask(true, false); // disable priority "1"
    Device.Statistic->RenderCALC.End();
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the main calc ends here
    // ( visibility )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується основний
    // розрахунок ( видимість )
    r4_frame_parts_mark(R4P_VISIBILITY);

    // SSFX: clear the motion vectors buffer and fill it with the sky motion. The G-buffer and
    // forward passes write the geometry on top of it.
    if (RImplementation.o.ssfx_motionvectors) {
        Target->u_setrt(Device.dwWidth, Device.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

        RCache.set_Stencil(FALSE);
        g_pGamePersistent->Environment().RenderSky(true);

        RCache.Index.Flush();
        RCache.Vertex.Flush();

        RCache.set_xform_world(Fidentity);
    }

    BOOL split_the_scene_to_minimize_wait = FALSE;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_SPLIT_SCENE))
        split_the_scene_to_minimize_wait = TRUE;

    //******* Main render :: PART-0	-- first
    if (!split_the_scene_to_minimize_wait) {
        PIX_EVENT(DEFER_PART0_NO_SPLIT);
        // level, DO NOT SPLIT
        Target->phase_scene_begin();
        if (!Device.scopeLensPass)
            r_dsgraph_render_hud();
        r_dsgraph_render_graph(0);
        r_dsgraph_render_lods(true, true);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the scene so far, then the
        // grass
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - сцена до цього місця, потім
        // трава
        r4_frame_parts_mark(R4P_SCENE);
        if (Details)
            Details->Render();
        r4_frame_parts_mark(R4P_GRASS);
        Target->phase_scene_end();
    } else {
        PIX_EVENT(DEFER_PART0_SPLIT);
        // level, SPLIT
        Target->phase_scene_begin();
        r_dsgraph_render_graph(0);
        Target->disable_aniso();
    }

    //******* Occlusion testing of volume-limited light-sources
    Target->phase_occq();
    LP_normal.clear();
    LP_pending.clear();
    if (RImplementation.o.dx10_msaa)
        RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);
    {
        PIX_EVENT(DEFER_TEST_LIGHT_VIS);
        // perform tests
        size_t count = 0;
        light_Package& LP = Lights.package;

        // stats
        stats.l_shadowed = LP.v_shadowed.size();
        stats.l_unshadowed = LP.v_point.size() + LP.v_spot.size();
        stats.l_total = stats.l_shadowed + stats.l_unshadowed;

        // perform tests
        count = std::max(count, LP.v_point.size());
        count = std::max(count, LP.v_spot.size());
        count = std::max(count, LP.v_shadowed.size());
        for (u32 it = 0; it < count; it++) {
            if (it < LP.v_point.size()) {
                light* L = LP.v_point[it];
                if (!Device.scopeLensPass)
                    L->vis_prepare();
                if (!Device.scopeLensPass && L->vis.pending)
                    LP_pending.v_point.push_back(L);
                else
                    LP_normal.v_point.push_back(L);
            }
            if (it < LP.v_spot.size()) {
                light* L = LP.v_spot[it];
                if (!Device.scopeLensPass)
                    L->vis_prepare();
                if (!Device.scopeLensPass && L->vis.pending)
                    LP_pending.v_spot.push_back(L);
                else
                    LP_normal.v_spot.push_back(L);
            }
            if (it < LP.v_shadowed.size()) {
                light* L = LP.v_shadowed[it];
                if (!Device.scopeLensPass)
                    L->vis_prepare();
                if (!Device.scopeLensPass && L->vis.pending)
                    LP_pending.v_shadowed.push_back(L);
                else
                    LP_normal.v_shadowed.push_back(L);
            }
        }
    }
    LP_normal.sort();
    LP_pending.sort();

    //******* Main render :: PART-1 (second)
    if (split_the_scene_to_minimize_wait) {
        PIX_EVENT(DEFER_PART1_SPLIT);
        // skybox can be drawn here
#if 0
            if (!RImplementation.o.dx10_msaa)
                Target->u_setrt(Target->rt_Generic_0, Target->rt_Generic_1, 0, HW.pBaseZB);
            else
                Target->u_setrt(Target->rt_Generic_0_r, Target->rt_Generic_1, 0,
                                RImplementation.Target->rt_MSAADepth->pZRT);
            RCache.set_CullMode(CULL_NONE);
            RCache.set_Stencil(FALSE);

            // draw skybox
            RCache.set_ColorWriteEnable();
            // CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	FALSE
            // ));
            RCache.set_Z(FALSE);
            g_pGamePersistent->Environment().RenderSky();
            // CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	TRUE
            // ));
            RCache.set_Z(TRUE);
#endif

        // level
        Target->phase_scene_begin();
        if (!Device.scopeLensPass)
            r_dsgraph_render_hud();
        r_dsgraph_render_lods(true, true);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the scene so far, then the
        // grass
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - сцена до цього місця, потім
        // трава
        r4_frame_parts_mark(R4P_SCENE);
        if (Details)
            Details->Render();
        r4_frame_parts_mark(R4P_GRASS);
        Target->phase_scene_end();
    }

    if (!Device.scopeLensPass && g_hud && g_hud->RenderActiveItemUIQuery()) {
        Target->phase_wallmarks();
        r_dsgraph_render_hud_ui();
    }

    // Wall marks
    if (Wallmarks) {
        PIX_EVENT(DEFER_WALLMARKS);
        Target->phase_wallmarks();
        g_r = 0;
        Wallmarks->Render(); // wallmarks has priority as normal geometry
    }

    // Update incremental shadowmap-visibility solver
    // Edited by Ivan Danko, 2026-11-14: not in the 3D scope lens pass - the list holds the main
    // view's lamps, and only the main view's next pass reads their shadow tests (testQ_frame);
    // reading and clearing it here lost those queries for good ("Too many occlusion queries").
    // Змінено (Ivan Danko, 2026-11-14): не в проході 3D-лінзи - у списку ліхтарі основного кадру,
    // і їхні перевірки тіні читає лише наступний прохід основного кадру (testQ_frame); читання й
    // очищення тут губило ці запити назавжди ("Too many occlusion queries").
    if (!Device.scopeLensPass) {
        PIX_EVENT(DEFER_FLUSH_OCCLUSION);
        u32 it = 0;
        for (it = 0; it < Lights_LastFrame.size(); it++) {
            if (0 == Lights_LastFrame[it])
                continue;
            try {
                Lights_LastFrame[it]->svis.flushoccq();
            } catch (...) {
                Msg("! Failed to flush-OCCq on light [%d] %X", it, *(u32*)(&Lights_LastFrame[it]));
            }
        }
        Lights_LastFrame.clear();
    }

    // full screen pass to mark msaa-edge pixels in highest stencil bit
    if (RImplementation.o.dx10_msaa) {
        PIX_EVENT(MARK_MSAA_EDGES);
        Target->mark_msaa_edges();
    }

    //	TODO: DX10: Implement DX10 rain.
    if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF)) {
        PIX_EVENT(DEFER_RAIN);
        render_rain();
    }

    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the rest of the scene ends here
    // ( the light tests, the wallmarks, the rain )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується решта сцени
    // ( перевірки світла, сліди на стінах, дощ )
    r4_frame_parts_mark(R4P_SCENE);

    // Screen Space Shaders: contact shadows for the sun and for the eight nearest shadow casting
    // lights. They run before the lighting, so the matrix pair that phase_combine normally
    // computes has to be available already - the same pair, just computed earlier in the frame.
    if (RImplementation.o.ssfx_core) {
        {
            static Fmatrix mm_saved_viewproj;

            if (!Device.scopeLensPass) {
                // (new-camera) -> (world) -> (old_viewproj)
                Fmatrix m_invview;
                m_invview.invert(Device.mView);
                Target->Matrix_previous.mul(mm_saved_viewproj, m_invview);
                Target->Matrix_current.set(Device.mProject);
                mm_saved_viewproj.set(Device.mFullTransform);
            }
        }

        if (RImplementation.o.ssfx_sss && !Device.scopeLensPass) {
            static bool sss_rendered, sss_extended_rendered;

            // SSS Shadows
            if (ps_ssfx_sss_quality.z > 0) {
                Target->phase_ssfx_sss();
                sss_rendered = true;
            } else {
                if (sss_rendered) { // Clear buffer
                    sss_rendered = false;
                    FLOAT ColorRGBA[4] = { 1, 1, 1, 1 };
                    HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
                }
            }

            if (ps_ssfx_sss_quality.w > 0) {
                // Extra lights
                Target->phase_ssfx_sss_ext(Lights.package);
                sss_extended_rendered = true;
            } else {
                if (sss_extended_rendered) { // Clear buffer
                    sss_extended_rendered = false;
                    FLOAT ColorRGBA[4] = { 1, 1, 1, 1 };
                    HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
                }
            }
        }
    }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the contact shadows end here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчуються контактні тіні
    r4_frame_parts_mark(R4P_SSS);

    // Directional light - fucking sun
    if (bSUN) {
        PIX_EVENT(DEFER_SUN);
        RImplementation.stats.l_visible++;
        if (!ps_r2_ls_flags_ext.is(R2FLAGEXT_SUN_OLD))
            render_sun_cascades();
        else {
            render_sun_near();
            render_sun();
            render_sun_filtered();
        }
        Target->accum_direct_blend();
    }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the sun ends here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується сонце
    r4_frame_parts_mark(R4P_SUN);

    {
        PIX_EVENT(DEFER_SELF_ILLUM);
        Target->phase_accumulator();
        // Render emissive geometry, stencil - write 0x0 at pixel pos
        RCache.set_xform_project(Device.mProject);
        RCache.set_xform_view(Device.mView);
        // Stencil - write 0x1 at pixel pos -
        if (!RImplementation.o.dx10_msaa)
            RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP,
                               D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        else
            RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP,
                               D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        // RCache.set_Stencil
        // (TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
        RCache.set_CullMode(CULL_CCW);
        RCache.set_ColorWriteEnable();
        // Screen Space Shaders build bloom from a separate emissive buffer, so the emissive lists
        // are kept here and drawn a second time just below.
        RImplementation.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
    }

    if (RImplementation.o.ssfx_bloom) {
        // Render emissive geometry into `rt_ssfx_bloom_emissive`
        FLOAT ColorRGBA[4] = { 0, 0, 0, 0 };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
        Target->u_setrt(Target->rt_ssfx_bloom_emissive, 0, 0,
                        !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
        RImplementation.r_dsgraph_render_emissive(true, true);
    }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the emissive geometry ends here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується геометрія, що
    // світиться
    r4_frame_parts_mark(R4P_EMISSIVE);

    // Lighting, non dependant on OCCQ
    {
        PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
        Target->phase_accumulator();
        HOM.Disable();
        render_lights(LP_normal);
    }

    // Lighting, dependant on OCCQ
    {
        PIX_EVENT(DEFER_LIGHT_OCCQ);
        render_lights(LP_pending);
    }

    // Screen Space Shaders: blur and resolve the spot/omni volumetric buffer once all the lights
    // have been accumulated, before the combine reads it.
    if (RImplementation.o.ssfx_volumetric)
        Target->phase_ssfx_volumetric_blur();
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the lights end here
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчуються джерела світла
    r4_frame_parts_mark(R4P_LIGHTS);

    // Postprocess
    {
        PIX_EVENT(DEFER_LIGHT_COMBINE);
        Target->phase_combine();
    }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the post-process ends here
    // ( phase_combine marks its own parts )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - тут закінчується
    // постобробка ( phase_combine позначає свої частини сам )
    r4_frame_parts_mark(R4P_POST);

    // Screen Space Shaders. Monolith r4_R_render.cpp:652-653. Puts the grass fade distance back
    // after the shadow passes moved it, and drops the visibility lists those passes kept alive.
    // Skipped for the 3D scope's lens pass, which is untouchable: that pass and the main view
    // share one frame's visibility result, so it may not be dropped between them.
    if (Details && !Device.scopeLensPass)
        Details->details_clear();

    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the view reached its end: its
    // parts count ( the lens view as one part )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - вид дійшов до кінця: його
    // частини зараховуються ( вид лінзи - як одна частина )
    r4_frame_parts_pass_end();
    if (Device.scopeLensPass)
        m_bScopeLensRendered = true;
    // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the lens view ends here; what this view cut
    // ( "cpu_frame_stats 1" )
    // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - вид лінзи тут закінчується; що цей
    // вид відкинув ( "cpu_frame_stats 1" )
    CHOM::SetLensView(false);
    vis_cut_view_end(Device.scopeLensPass);

    VERIFY(0 == mapDistort.size());
}

bool CRender::CaptureScopeLens() {
    if (!Device.scopeLensPass || !Target)
        return false;

    if (!m_bScopeLensRendered)
        return false;
    Target->CaptureScopeLens();
    return true;
}

void CRender::render_forward() {
    VERIFY(0 == mapDistort.size());
    RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

    //******* Main render - second order geometry (the one, that doesn't support deffering)
    //.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
    {
        // level
        r_pmask(false, true); // enable priority "1"
        phase = PHASE_NORMAL;
        Fmatrix visibility_transform = scope_visibility_transform();
        // Edited by Ivan Danko, 2026-11-15: FPS fix step 1 - the transparent pass ( forward_visible )
        // Змінено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - прохід прозорого ( forward_visible )
        vis_forward_pass = true;
        render_main(visibility_transform, false); //
        vis_forward_pass = false;
        //	Igor: we don't want to render old lods on next frame.
        mapLOD.clear();
        r_dsgraph_render_graph(1);                     // normal level, secondary priority
        PortalTraverser.fade_render();                 // faded-portals
        // Postprocessing uses the HUD depth/normal buffers and can reintroduce
        // the optic housing over an opaque lens. Draw sorted HUD after it.
        r_dsgraph_render_sorted(!Device.scopeLensActive || Device.scopeLensPass);
        // Monolith r4_R_render.cpp:675 comments the next call out: with SSS on the rain is drawn
        // by phase_combine, before this pass. With SSS off it stays here, where the game has it.
        if (!RImplementation.o.ssfx_core)
            g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
    }

    RImplementation.o.distortion = FALSE; // disable distorion
}
