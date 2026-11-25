//----------------------------------------------------
// file: PSObject.cpp
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "ParticlesObject.h"
#include "../xrEngine/defines.h"
#include "xrRender/RenderVisual.h"
#include "xrRender/ParticleCustom.h"
#include "../xrEngine/render.h"
#include "../xrEngine/IGame_Persistent.h"
#include "../xrEngine/environment.h"
// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the frame parts
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини кадру
#include "../xrEngine/frame_parts.h"

const Fvector zero_vel = { 0.f, 0.f, 0.f };

CParticlesObject::CParticlesObject(LPCSTR p_name, BOOL bAutoRemove, bool destroy_on_game_load)
    : inherited(destroy_on_game_load) {
    Init(p_name, 0, bAutoRemove);
}

void CParticlesObject::Init(LPCSTR p_name, IRender_Sector* S, BOOL bAutoRemove) {
    m_bLooped = false;
    m_bStopping = false;
    m_bAutoRemove = bAutoRemove;
    float time_limit = 0.0f;

    // create visual
    renderable.visual = Render->model_CreateParticles(p_name);
    VERIFY(renderable.visual);
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    time_limit = V->GetTimeLimit();

    if (time_limit > 0.f) {
        m_iLifeTime = iFloor(time_limit * 1000.f);
    } else {
        if (bAutoRemove) {
            R_ASSERT3(!m_bAutoRemove, "Can't set auto-remove flag for looped particle system.",
                      p_name);
        } else {
            m_iLifeTime = 0;
            m_bLooped = true;
        }
    }

    // spatial
    spatial.type = 0;
    spatial.sector = S;

    // sheduled
    shedule.t_min = 20;
    shedule.t_max = 50;
    shedule_register();

    dwLastTime = Device.dwTimeGlobal;
    mt_dt = 0;
    // Written by Ivan Danko, 2026-11-17: FPS plan step 2b - not taken by the render's batch yet
    // Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - пачка рендера ще не брала цей ефект
    m_batch_frame = 0;
}

//----------------------------------------------------
CParticlesObject::~CParticlesObject() {
    VERIFY(0 == mt_dt);

    //	we do not need this since CPS_Instance does it
    //	shedule_unregister		();
}

void CParticlesObject::UpdateSpatial() {
    // spatial	(+ workaround occasional bug inside particle-system)
    vis_data& vis = renderable.visual->getVisData();
    if (xr::valid(vis.sphere)) {
        Fvector P;
        float R;
        renderable.xform.transform_tiny(P, vis.sphere.P);
        R = vis.sphere.R;
        if (0 == spatial.type) {
            // First 'valid' update - register
            spatial.type = STYPE_RENDERABLE;
            spatial.sphere.set(P, R);
            spatial_register();
        } else {
            BOOL bMove = FALSE;
            if (!P.similar(spatial.sphere.P, EPS_L * 10.f))
                bMove = TRUE;
            if (!fsimilar(R, spatial.sphere.R, 0.15f))
                bMove = TRUE;
            if (bMove) {
                spatial.sphere.set(P, R);
                spatial_move();
            }
        }
    }
}

const shared_str CParticlesObject::Name() {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    return (V) ? V->Name() : "";
}

//----------------------------------------------------
void CParticlesObject::Play(bool bHudMode) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    if (bHudMode)
        V->SetHudMode(bHudMode);

    V->Play();
    dwLastTime = Device.dwTimeGlobal - 33ul;
    mt_dt = 0;
    PerformAllTheWork(0);
    m_bStopping = false;
}

void CParticlesObject::play_at_pos(const Fvector& pos, BOOL xform) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    Fmatrix m;
    m.translate(pos);
    V->UpdateParent(m, zero_vel, xform);
    V->Play();
    dwLastTime = Device.dwTimeGlobal - 33ul;
    mt_dt = 0;
    PerformAllTheWork(0);
    m_bStopping = false;
}

void CParticlesObject::Stop(BOOL bDefferedStop) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->Stop(bDefferedStop);
    m_bStopping = true;
}

void CParticlesObject::shedule_Update(u32 _dt) {
    // A particle definition reaches its time limit when emission stops, while
    // already emitted particles can remain alive during the deferred tail.
    // The legacy CPS auto-remove deleted the whole render object at that first
    // moment. Temporarily defer it and wait for the visual to finish instead.
    const bool wait_for_particle_tail =
        m_bAutoRemove && m_iLifeTime <= static_cast<int>(_dt);

    if (wait_for_particle_tail)
        m_bAutoRemove = FALSE;

    inherited::shedule_Update(_dt);

    if (wait_for_particle_tail)
        m_bAutoRemove = TRUE;

    // Update
    if (m_bDead)
        return;
    u32 dt = Device.dwTimeGlobal - dwLastTime;
    // Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - an effect the DX11 render's batch took this
    // frame or the frame before ( BatchUpdate ) is left to that batch: the next drawn frame updates it on
    // all cores. An effect out of sight is updated here, as before.
    // Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - ефект, який пачка рендера DX11 узяла в цьому
    // або в попередньому кадрі ( BatchUpdate ), лишається цій пачці: наступний намальований кадр оновить
    // його на всіх ядрах. Ефект поза полем зору оновлюється тут, як раніше.
    if (dt && m_batch_frame && Device.dwFrame - m_batch_frame <= 1)
        dt = 0;
    if (dt) {
        if constexpr (false) { //.psDeviceFlags.test(mtParticles))	{    //. AlexMX comment this line// NO
                 //UNCOMMENT - DON'T WORK PROPERLY
            mt_dt = dt;
            fastdelegate::FastDelegate0<> delegate(this, &CParticlesObject::PerformAllTheWork_mt);
            Device.seqParallel.push_back(delegate);
        } else {
            mt_dt = 0;
            IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
            VERIFY(V);
            // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the particles as a part
            // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частинки як частина
            const u64 fp_particles = frame_part_begin();
            V->OnFrame(dt);
            frame_part_end(FP_PARTICLES, fp_particles);
        }
        dwLastTime = Device.dwTimeGlobal;
    }
    UpdateSpatial();

    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);

    if (m_bAutoRemove && m_iLifeTime <= 0 && !V->IsPlaying())
        PSI_destroy();
}

void CParticlesObject::PerformAllTheWork(u32 _dt) {
    // Update
    u32 dt = Device.dwTimeGlobal - dwLastTime;
    if (dt) {
        IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
        VERIFY(V);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the particles as a part
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частинки як частина
        const u64 fp_particles = frame_part_begin();
        V->OnFrame(dt);
        frame_part_end(FP_PARTICLES, fp_particles);
        dwLastTime = Device.dwTimeGlobal;
    }
    UpdateSpatial();
}

void CParticlesObject::PerformAllTheWork_mt() {
    if (0 == mt_dt)
        return; //???
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->OnFrame(mt_dt);
    mt_dt = 0;
}

// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the effect's update in the DX11 render's batch on
// all cores ( xrRenderPC_R4\r4_R_render.cpp ): the same update renderable_Render runs, without its own
// time measurement ( the render measures the whole batch ). It runs on a task worker or on the main
// thread; the effects that are not safe there ( child effects, world collisions ) the render keeps on
// the main thread.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - оновлення ефекту в пачці рендера DX11 на всіх
// ядрах ( xrRenderPC_R4\r4_R_render.cpp ): те саме оновлення, що й у renderable_Render, без власного
// заміру часу ( рендер заміряє всю пачку ). Виконується в робочому потоці задач або в головному потоці;
// ефекти, небезпечні там ( дочірні ефекти, зіткнення зі світом ), рендер лишає головному потоку.
bool CParticlesObject::BatchUpdate() {
    m_batch_frame = Device.dwFrame;
    const u32 dt = Device.dwTimeGlobal - dwLastTime;
    if (!dt)
        return false;
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->OnFrame(dt);
    dwLastTime = Device.dwTimeGlobal;
    return true;
}

namespace {
// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the entry the render calls ( g_ps_batch_update,
// xrEngine\PS_instance.h ); set while this module is loaded.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - точка входу, яку викликає рендер
// ( g_ps_batch_update, xrEngine\PS_instance.h ); задана, поки цей модуль завантажено.
bool ps_batch_update(IRenderable* renderable) {
    CParticlesObject* object = dynamic_cast<CParticlesObject*>(renderable);
    return object ? object->BatchUpdate() : false;
}

struct ps_batch_update_entry {
    ps_batch_update_entry() { g_ps_batch_update = &ps_batch_update; }
    ~ps_batch_update_entry() { g_ps_batch_update = nullptr; }
} ps_batch_update_entry_instance;
} // namespace

void CParticlesObject::SetXFORM(const Fmatrix& m) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->UpdateParent(m, zero_vel, TRUE);
    renderable.xform.set(m);
    UpdateSpatial();
}

void CParticlesObject::UpdateParent(const Fmatrix& m, const Fvector& vel) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->UpdateParent(m, vel, FALSE);
    UpdateSpatial();
}

Fvector& CParticlesObject::Position() {
    vis_data& vis = renderable.visual->getVisData();
    return vis.sphere.P;
}

float CParticlesObject::shedule_Scale() {
    return Device.vCameraPosition.distance_to(Position()) / 200.f;
}

void CParticlesObject::renderable_Render() {
    VERIFY(renderable.visual);
    u32 dt = Device.dwTimeGlobal - dwLastTime;
    // Edited by Ivan Danko, 2026-11-14: FPS fix step 1 - the effect is updated only when time has
    // passed since its last update, as in Anomaly ( Monolith ParticlesObject.cpp:263-277 ). One
    // frame draws it in several passes ( the 3D scope lens view, the main view, the second-order
    // geometry ); every pass after the first one had dt 0 and only redid the same frame's work.
    // Змінено (Ivan Danko, 2026-11-14): крок 1 виправлення FPS - ефект оновлюється лише тоді, коли
    // з його останнього оновлення минув час, як у Anomaly ( Monolith ParticlesObject.cpp:263-277 ).
    // Один кадр малює його в кількох проходах ( лінза 3D-прицілу, основний вид, геометрія другого
    // порядку ); кожен прохід після першого мав dt 0 і лише повторював роботу того самого кадру.
    if (!dt) {
        ::Render->set_Transform(&renderable.xform);
        ::Render->add_Visual(renderable.visual);
        return;
    }
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the particles as a part ( in the
    // render )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частинки як частина ( у рендері )
    const u64 fp_particles = frame_part_begin();
    V->OnFrame(dt);
    frame_part_end(FP_PARTICLES, fp_particles);
    dwLastTime = Device.dwTimeGlobal;
    ::Render->set_Transform(&renderable.xform);
    ::Render->add_Visual(renderable.visual);
}

bool CParticlesObject::IsAutoRemove() {
    if (m_bAutoRemove)
        return true;
    else
        return false;
}
void CParticlesObject::SetAutoRemove(bool auto_remove) {
    VERIFY(m_bStopping || !IsLooped());
    m_bAutoRemove = auto_remove;
}

// Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - Monolith's CParticlesObject::SetHudMode. Adapted:
// without the dedicated server check (our engine has no g_dedicated_server, IsPlaying below has none).
// Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - CParticlesObject::SetHudMode з Monolith.
// Адаптовано: без перевірки виділеного сервера (у нашому рушії немає g_dedicated_server, IsPlaying
// нижче теж без неї).
void CParticlesObject::SetHudMode(bool bHudMode) {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->SetHudMode(bHudMode);
}

bool CParticlesObject::IsPlaying() {
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    return !!V->IsPlaying();
}
