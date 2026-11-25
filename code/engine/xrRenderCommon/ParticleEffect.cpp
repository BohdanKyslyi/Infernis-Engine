#include "stdafx.h"
#pragma hdrstop

#include "ParticleEffect.h"

#ifndef _EDITOR
#include <xmmintrin.h>
#include "xrCPU_Pipe/ttapi.h"
#include <vector> 
#endif

using namespace PAPI;
using namespace PS;

const u32 PS::uDT_STEP = 33;
const float PS::fDT_STEP = float(uDT_STEP) / 1000.f;
#if RENDER == R_R4

// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - true on a thread while it runs the DX11 render's
// batch of particle updates ( xrRenderPC_R4\r4_R_render.cpp ): a particle born there takes its random
// numbers from that thread's own generator, the shared one ( ::Random ) is not made for many threads.
// Outside the batch ::Random, as before.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - true у потоці, поки він виконує пачку
// оновлень частинок рендера DX11 ( xrRenderPC_R4\r4_R_render.cpp ): частинка, що там народжується,
// бере випадкові числа з власного генератора цього потоку, спільний ( ::Random ) не розрахований на
// багато потоків. Поза пачкою - ::Random, як раніше.
thread_local bool r4_ps_batch_thread = false;

static CRandom& ps_birth_random() {
    if (!r4_ps_batch_thread)
        return ::Random;
    static thread_local CRandom thread_random(static_cast<s32>(GetCurrentThreadId()));
    return thread_random;
}
#endif

static void ApplyTexgen(const Fmatrix& mVP) {
    Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
    Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f,  0.0f, 1.0f };
#else
    float _w = float(RDEVICE.dwWidth);
    float _h = float(RDEVICE.dwHeight);
    float o_w = (.5f / _w);
    float o_h = (.5f / _h);
    Fmatrix mTexelAdjust = { 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,       -0.5f,      0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f };
#endif

    mTexgen.mul(mTexelAdjust, mVP);
    RCache.set_c("mVPTexgen", mTexgen);
}

void PS::OnEffectParticleBirth(void* owner, u32, PAPI::Particle& m, u32) {
    auto* PE = static_cast<CParticleEffect*>(owner);
    VERIFY(PE);
    CPEDef* PED = PE->GetDefinition();
    if (PED) {
#if RENDER == R_R4
        // Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - the random numbers of the thread that runs
        // the update ( ps_birth_random above )
        // Змінено (Ivan Danko, 2026-11-17): крок 2б плану FPS - випадкові числа потоку, що виконує
        // оновлення ( ps_birth_random вище )
        CRandom& rnd = ps_birth_random();
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame = static_cast<u16>(iFloor(rnd.randI(PED->m_Frame.m_iFrameCount) * 255.f));
        if (PED->m_Flags.is(CPEDef::dfAnimated) && PED->m_Flags.is(CPEDef::dfRandomPlayback) &&
            rnd.randI(2))
            m.flags.set(Particle::ANIMATE_CCW, TRUE);
#else
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame = static_cast<u16>(iFloor(Random.randI(PED->m_Frame.m_iFrameCount) * 255.f));
        if (PED->m_Flags.is(CPEDef::dfAnimated) && PED->m_Flags.is(CPEDef::dfRandomPlayback) &&
            Random.randI(2))
            m.flags.set(Particle::ANIMATE_CCW, TRUE);
#endif
    }
}

void PS::OnEffectParticleDead(void*, u32, PAPI::Particle&, u32) {
}

//------------------------------------------------------------------------------
// class CParticleEffect
//------------------------------------------------------------------------------
CParticleEffect::CParticleEffect() {
    m_HandleEffect = ParticleManager()->CreateEffect(1);
    VERIFY(m_HandleEffect >= 0);
    m_HandleActionList = ParticleManager()->CreateActionList();
    VERIFY(m_HandleActionList >= 0);
    m_RT_Flags.zero();
    m_Def = nullptr;
    m_fElapsedLimit = 0.0f;
    m_MemDT = 0;
    m_InitialPosition.set(0.0f, 0.0f, 0.0f);
    m_DestroyCallback = nullptr;
    m_CollisionCallback = nullptr;
    m_XFORM.identity();
}

CParticleEffect::~CParticleEffect() {
    OnDeviceDestroy();
    ParticleManager()->DestroyEffect(m_HandleEffect);
    ParticleManager()->DestroyActionList(m_HandleActionList);
}

void CParticleEffect::Play() {
    m_RT_Flags.set(flRT_DefferedStop, FALSE);
    m_RT_Flags.set(flRT_Playing, TRUE);
    ParticleManager()->PlayEffect(m_HandleEffect, m_HandleActionList);
}

void CParticleEffect::Stop(BOOL bDefferedStop) {
    ParticleManager()->StopEffect(m_HandleEffect, m_HandleActionList, bDefferedStop);
    if (bDefferedStop) {
        m_RT_Flags.set(flRT_DefferedStop, TRUE);
    } else {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }
}

void CParticleEffect::RefreshShader() {
    OnDeviceDestroy();
    OnDeviceCreate();
}

void CParticleEffect::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) {
    m_RT_Flags.set(flRT_XFORM, bXFORM);
#if RENDER == R_R4
    // Written by Ivan Danko, 2026-11-18: FPS plan step 2b part B - the effect gets its place again: its
    // vertices in the particle vertex buffer are no longer valid ( r4_ps_geom_* below ).
    // Створено (Ivan Danko, 2026-11-18): крок 2б плану FPS, частина B - ефект знову отримує своє місце:
    // його вершини в буфері вершин частинок більше не дійсні ( r4_ps_geom_* нижче ).
    ps_geom_stamp = 0;
#endif
    if (bXFORM)
        m_XFORM.set(m);
    else {
        m_InitialPosition = m.c;
        ParticleManager()->Transform(m_HandleActionList, m, velocity);
    }
}

void CParticleEffect::OnFrame(u32 frame_dt) {
#if RENDER == R_R4
    // Written by Ivan Danko, 2026-11-18: FPS plan step 2b part B - the particles change: their vertices in
    // the particle vertex buffer are no longer valid ( r4_ps_geom_* below ).
    // Створено (Ivan Danko, 2026-11-18): крок 2б плану FPS, частина B - частинки змінюються: їхні
    // вершини в буфері вершин частинок більше не дійсні ( r4_ps_geom_* нижче ).
    ps_geom_stamp = 0;
#endif
    if (m_Def && m_RT_Flags.is(flRT_Playing)) {
        m_MemDT += frame_dt;
#if RENDER == R_R4
        // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the effect's own update step times
        // particle_update_mod, as in Anomaly (Monolith CParticleEffect::OnFrame); these two hide the
        // fixed 33 ms step PS::uDT_STEP / PS::fDT_STEP in the code below.
        // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - власний крок оновлення ефекту,
        // помножений на particle_update_mod, як в Anomaly (Monolith CParticleEffect::OnFrame); ці дві
        // змінні затуляють фіксований крок 33 мс PS::uDT_STEP / PS::fDT_STEP у коді нижче.
        const u32 uDT_STEP = m_Def->GetUStep();
        const float fDT_STEP = m_Def->GetFStep();
#endif

        int StepCount = 0;
        if (m_MemDT >= uDT_STEP) {
            StepCount = m_MemDT / uDT_STEP;
            m_MemDT = m_MemDT % uDT_STEP;
            clamp(StepCount, 0, 3);
        }

        for (; StepCount; StepCount--) {
            if (m_Def->m_Flags.is(CPEDef::dfTimeLimit)) {
                if (!m_RT_Flags.is(flRT_DefferedStop)) {
                    m_fElapsedLimit -= fDT_STEP;
                    if (m_fElapsedLimit < 0.0f) {
                        m_fElapsedLimit = m_Def->m_fTimeLimit;
                        Stop(true);
                        break;
                    }
                }
            }
            ParticleManager()->Update(m_HandleEffect, m_HandleActionList, fDT_STEP);

            PAPI::Particle* particles;
            u32 p_cnt;
            ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

            if (m_Def->m_Flags.is(CPEDef::dfFramed | CPEDef::dfAnimated))
                m_Def->ExecuteAnimate(particles, p_cnt, fDT_STEP);
            if (m_Def->m_Flags.is(CPEDef::dfCollision))
                m_Def->ExecuteCollision(particles, p_cnt, fDT_STEP, this, m_CollisionCallback);

            if (p_cnt) {
                vis.box.invalidate();
                float p_size = 0.0f;
                for (u32 i = 0; i < p_cnt; i++) {
                    Particle& m = particles[i];
                    vis.box.modify((Fvector&)m.pos);
                    if (m.size.x > p_size) p_size = m.size.x;
                    if (m.size.y > p_size) p_size = m.size.y;
                    if (m.size.z > p_size) p_size = m.size.z;
                }
                vis.box.grow(p_size);
                vis.box.getsphere(vis.sphere.P, vis.sphere.R);
            }
            if (m_RT_Flags.is(flRT_DefferedStop) && (0 == p_cnt)) {
                m_RT_Flags.set(flRT_Playing | flRT_DefferedStop, FALSE);
                break;
            }
        }
    } else {
        vis.box.set(m_InitialPosition, m_InitialPosition);
        vis.box.grow(EPS_L);
        vis.box.getsphere(vis.sphere.P, vis.sphere.R);
    }
}

BOOL CParticleEffect::Compile(CPEDef* def) {
    m_Def = def;
    if (m_Def) {
        RefreshShader();

        IReader F(m_Def->m_Actions.pointer(), m_Def->m_Actions.size());
        ParticleManager()->LoadActions(m_HandleActionList, F);
        ParticleManager()->SetMaxParticles(m_HandleEffect, m_Def->m_MaxParticles);
        ParticleManager()->SetCallback(m_HandleEffect, OnEffectParticleBirth, OnEffectParticleDead, this, 0);

        if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
            m_fElapsedLimit = m_Def->m_fTimeLimit;
    }
    if (def)
        shader = def->m_CachedShader;
    return TRUE;
}

void CParticleEffect::SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p) {
    ParticleManager()->SetCallback(m_HandleEffect, bc, dc, owner, p);
}

u32 CParticleEffect::ParticlesCount() {
    return ParticleManager()->GetParticlesCount(m_HandleEffect);
}

void CParticleEffect::Copy(dxRender_Visual*) {
    FATAL("Can't duplicate particle system - NOT IMPLEMENTED");
}

void CParticleEffect::OnDeviceCreate() {
    if (m_Def) {
        if (m_Def->m_Flags.is(CPEDef::dfSprite)) {
            geom.create(FVF::F_LIT, RCache.Vertex.Buffer(), RCache.QuadIB);
            if (m_Def)
                shader = m_Def->m_CachedShader;
        }
    }
}

void CParticleEffect::OnDeviceDestroy() {
    if (m_Def) {
        if (m_Def->m_Flags.is(CPEDef::dfSprite)) {
            geom.destroy();
            shader.destroy();
        }
    }
}

#ifndef _EDITOR
//----------------------------------------------------
IC void FillSprite_fpu(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos,
                       const Fvector2& lt, const Fvector2& rb, float r1, float r2, u32 clr,
                       float angle) {
    float sa = std::sin(angle);
    float ca = std::cos(angle);

    Fvector Vr, Vt;

    Vr.x = T.x * r1 * sa + R.x * r1 * ca;
    Vr.y = T.y * r1 * sa + R.y * r1 * ca;
    Vr.z = T.z * r1 * sa + R.z * r1 * ca;

    Vt.x = T.x * r2 * ca - R.x * r2 * sa;
    Vt.y = T.y * r2 * ca - R.y * r2 * sa;
    Vt.z = T.z * r2 * ca - R.z * r2 * sa;

    Fvector a, b, c, d;

    a.sub(Vt, Vr);
    b.add(Vt, Vr);

    c.invert(a);
    d.invert(b);

    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y);
    pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y);
    pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y);
    pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y);
    pv++;
}

inline void fsincos(const float angle, float& sine, float& cosine) {
    sine = std::sin(angle);
    cosine = std::cos(angle);
}

IC void FillSprite(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos,
                   const Fvector2& lt, const Fvector2& rb, float r1, float r2, u32 clr, float sina,
                   float cosa) {
    __m128 Vr, Vt, _T, _R, _pos, _zz, _sa, _ca, a, b, c, d;

    _sa = _mm_set1_ps(sina);
    _ca = _mm_set1_ps(cosa);

    _T = _mm_load_ss((float*)&T.x);
    _T = _mm_loadh_pi(_T, (__m64*)&T.y);

    _R = _mm_load_ss((float*)&R.x);
    _R = _mm_loadh_pi(_R, (__m64*)&R.y);

    _pos = _mm_load_ss((float*)&pos.x);
    _pos = _mm_loadh_pi(_pos, (__m64*)&pos.y);

    _zz = _mm_setzero_ps();

    Vr = _mm_mul_ps(_mm_set1_ps(r1), _mm_add_ps(_mm_mul_ps(_T, _sa), _mm_mul_ps(_R, _ca)));
    Vt = _mm_mul_ps(_mm_set1_ps(r2), _mm_sub_ps(_mm_mul_ps(_T, _ca), _mm_mul_ps(_R, _sa)));

    a = _mm_sub_ps(Vt, Vr);
    b = _mm_add_ps(Vt, Vr);
    c = _mm_sub_ps(_zz, a);
    d = _mm_sub_ps(_zz, b);

    a = _mm_add_ps(a, _pos);
    d = _mm_add_ps(d, _pos);
    b = _mm_add_ps(b, _pos);
    c = _mm_add_ps(c, _pos);

    _mm_store_ss((float*)&pv->p.x, d);
    _mm_storeh_pi((__m64*)&pv->p.y, d);
    pv->color = clr;
    pv->t.set(lt.x, rb.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, a);
    _mm_storeh_pi((__m64*)&pv->p.y, a);
    pv->color = clr;
    pv->t.set(lt.x, lt.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, c);
    _mm_storeh_pi((__m64*)&pv->p.y, c);
    pv->color = clr;
    pv->t.set(rb.x, rb.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, b);
    _mm_storeh_pi((__m64*)&pv->p.y, b);
    pv->color = clr;
    pv->t.set(rb.x, lt.y);
    pv++;
}

IC void FillSprite(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt,
                   const Fvector2& rb, float r1, float r2, u32 clr, float sina, float cosa) {
    const Fvector& T = dir;
    Fvector R;

    __m128 _t, _t1, _t2, _r, _r1, _r2;

    _t = _mm_load_ss((float*)&T.x);
    _t = _mm_loadh_pi(_t, (__m64*)&T.y);

    _r = _mm_load_ss((float*)&RDEVICE.vCameraDirection.x);
    _r = _mm_loadh_pi(_r, (__m64*)&RDEVICE.vCameraDirection.y);

    _t1 = _mm_shuffle_ps(_t, _t, _MM_SHUFFLE(0, 3, 1, 2));
    _t2 = _mm_shuffle_ps(_t, _t, _MM_SHUFFLE(2, 0, 1, 3));

    _r1 = _mm_shuffle_ps(_r, _r, _MM_SHUFFLE(2, 0, 1, 3));
    _r2 = _mm_shuffle_ps(_r, _r, _MM_SHUFFLE(0, 3, 1, 2));

    _t1 = _mm_mul_ps(_t1, _r1);
    _t2 = _mm_mul_ps(_t2, _r2);

    _t1 = _mm_sub_ps(_t1, _t2);

    _t2 = _mm_mul_ps(_t1, _t1);                              
    _r1 = _mm_movehl_ps(_t2, _t2);                           
    _t2 = _mm_add_ss(_t2, _r1);                              
    _r1 = _mm_shuffle_ps(_r1, _r1, _MM_SHUFFLE(1, 1, 1, 1)); 
    _t2 = _mm_add_ss(_t2, _r1);                              

    _r1 = _mm_set_ss(std::numeric_limits<float>::min());

    if (_mm_comigt_ss(_t2, _r1)) {
        _t2 = _mm_rsqrt_ss(_t2);
        _t2 = _mm_shuffle_ps(_t2, _t2, _MM_SHUFFLE(0, 0, 0, 0));
        _t1 = _mm_mul_ps(_t1, _t2);
    }

    _mm_store_ss((float*)&R.x, _t1);
    _mm_storeh_pi((__m64*)&R.y, _t1);

    FillSprite(pv, T, R, pos, lt, rb, r1, r2, clr, sina, cosa);
}

extern ENGINE_API float psHUD_FOV;

struct PRS_PARAMS {
    FVF::LIT* pv;
    u32 p_from;
    u32 p_to;
    PAPI::Particle* particles;
    CParticleEffect* pPE;
};

inline void magnitude_sse(Fvector& vec, float& res) {
    __m128 tv, tu;

    tv = _mm_load_ss((float*)&vec.x);                     
    tv = _mm_loadh_pi(tv, (__m64*)&vec.y);                
    tv = _mm_mul_ps(tv, tv);                              
    tu = _mm_movehl_ps(tv, tv);                           
    tv = _mm_add_ss(tv, tu);                              
    tu = _mm_shuffle_ps(tu, tu, _MM_SHUFFLE(1, 1, 1, 1)); 
    tv = _mm_add_ss(tv, tu);                              
    tv = _mm_sqrt_ss(tv);                                 
    _mm_store_ss((float*)&res, tv);
}

void ParticleRenderStream(void* lpvParams) {
    float sina = 0.0f, cosa = 0.0f;
#if RENDER == R_R4
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author Xottab_DUTY): FPS plan step 2 - the rotation fix of Anomaly (Monolith
    // ParticleRenderStream, Xottab_DUTY: "changed angle to be float instead of DWORD"): the sine and
    // cosine were taken of the angle's bits read as an integer, so rotated particles stood at wrong
    // angles; now of the angle itself.
    // Перенесення з Monolith (автор Xottab_DUTY) - Ivan Danko, 2026-11-17: крок 2 плану FPS - виправлення повороту з Anomaly (Monolith
    // ParticleRenderStream, Xottab_DUTY: кут став float замість DWORD): синус і косинус бралися від
    // бітів кута, прочитаних як ціле число, тож повернуті частинки стояли під хибними кутами; тепер -
    // від самого кута.
    float angle = 0xFFFFFFFF;
#else
    DWORD angle = 0xFFFFFFFF;
#endif

    PRS_PARAMS* pParams = (PRS_PARAMS*)lpvParams;

    FVF::LIT* pv = pParams->pv;
    u32 p_from = pParams->p_from;
    u32 p_to = pParams->p_to;
    PAPI::Particle* particles = pParams->particles;
    CParticleEffect& pPE = *pParams->pPE;

    for (u32 i = p_from; i < p_to; i++) {
        PAPI::Particle& m = particles[i];
        Fvector2 lt, rb;
        lt.set(0.f, 0.f);
        rb.set(1.f, 1.f);

        _mm_prefetch((char*)&particles[i + 1], _MM_HINT_NTA);

#if RENDER == R_R4
        // Ported by Ivan Danko, 2026-11-17, from Monolith (author Xottab_DUTY): FPS plan step 2 - the angle as a float (see above, Monolith).
        // Перенесення з Monolith (автор Xottab_DUTY) - Ivan Danko, 2026-11-17: крок 2 плану FPS - кут як float (див. вище, Monolith).
        if (angle != m.rot.x) {
            angle = m.rot.x;
            sina = std::sinf(*(float*)&angle);
            cosa = std::cosf(*(float*)&angle);
        }
#else
        if (angle != *((DWORD*)&m.rot.x)) {
            angle = *((DWORD*)&m.rot.x);
            sina = std::sin(angle);
            cosa = std::cos(angle);
        }
#endif

        _mm_prefetch(64 + (char*)&particles[i + 1], _MM_HINT_NTA);

        if (pPE.m_Def->m_Flags.is(CPEDef::dfFramed))
            pPE.m_Def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);

        float r_x = m.size.x * 0.5f;
        float r_y = m.size.y * 0.5f;
        float speed = 0.0f;
        bool speed_calculated = false;

        if (pPE.m_Def->m_Flags.is(CPEDef::dfVelocityScale)) {
            magnitude_sse(m.vel, speed);
            speed_calculated = true;
            r_x += speed * pPE.m_Def->m_VelocityScale.x;
            r_y += speed * pPE.m_Def->m_VelocityScale.y;
        }

        if (pPE.m_Def->m_Flags.is(CPEDef::dfAlignToPath)) {
            if (!speed_calculated)
                magnitude_sse(m.vel, speed);
            if ((speed < EPS_S) && pPE.m_Def->m_Flags.is(CPEDef::dfWorldAlign)) {
                Fmatrix M;
                M.setXYZ(pPE.m_Def->m_APDefaultRotation);
                if (pPE.m_RT_Flags.is(CParticleEffect::flRT_XFORM)) {
                    Fvector p;
                    pPE.m_XFORM.transform_tiny(p, m.pos);
                    M.mulA_43(pPE.m_XFORM);
                    FillSprite(pv, M.k, M.i, p, lt, rb, r_x, r_y, m.color, sina, cosa);
                } else {
                    FillSprite(pv, M.k, M.i, m.pos, lt, rb, r_x, r_y, m.color, sina, cosa);
                }
            } else if ((speed >= EPS_S) && pPE.m_Def->m_Flags.is(CPEDef::dfFaceAlign)) {
                Fmatrix M;
                M.identity();
                M.k.div(m.vel, speed);
                M.j.set(0, 1, 0);
                if (std::abs(M.j.dotproduct(M.k)) > .99f)
                    M.j.set(0, 0, 1);
                M.i.crossproduct(M.j, M.k);
                M.i.normalize();
                M.j.crossproduct(M.k, M.i);
                M.j.normalize();
                if (pPE.m_RT_Flags.is(CParticleEffect::flRT_XFORM)) {
                    Fvector p;
                    pPE.m_XFORM.transform_tiny(p, m.pos);
                    M.mulA_43(pPE.m_XFORM);
                    FillSprite(pv, M.j, M.i, p, lt, rb, r_x, r_y, m.color, sina, cosa);
                } else {
                    FillSprite(pv, M.j, M.i, m.pos, lt, rb, r_x, r_y, m.color, sina, cosa);
                }
            } else {
                Fvector dir;
                if (speed >= EPS_S)
                    dir.div(m.vel, speed);
                else
                    dir.setHP(-pPE.m_Def->m_APDefaultRotation.y, -pPE.m_Def->m_APDefaultRotation.x);
                if (pPE.m_RT_Flags.is(CParticleEffect::flRT_XFORM)) {
                    Fvector p, d;
                    pPE.m_XFORM.transform_tiny(p, m.pos);
                    pPE.m_XFORM.transform_dir(d, dir);
                    FillSprite(pv, p, d, lt, rb, r_x, r_y, m.color, sina, cosa);
                } else {
                    FillSprite(pv, m.pos, dir, lt, rb, r_x, r_y, m.color, sina, cosa);
                }
            }
        } else {
            if (pPE.m_RT_Flags.is(CParticleEffect::flRT_XFORM)) {
                Fvector p;
                pPE.m_XFORM.transform_tiny(p, m.pos);
                FillSprite(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, p, lt, rb, r_x, r_y,
                           m.color, sina, cosa);
            } else {
                FillSprite(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, m.pos, lt, rb, r_x, r_y,
                           m.color, sina, cosa);
            }
        }
    }
}
#if RENDER == R_R4

// Written by Ivan Danko, 2026-11-18: FPS plan step 2b part B ( "all particle geometry of a frame in one
// batch on all cores" ). Before, every draw of a sprite effect built its vertices again on the drawing
// thread ( ParticleRenderStream ) into the common vertex stream: in the 3D scope's lens, in the main
// view and once more for the heat haze. Now the effects the frame will draw wait in a queue ( the scene
// graph puts them there, r__dsgraph_build.cpp ); at the first draw the vertices of all of them are built
// at once, on all cores, into the buffer below, and every draw of the frame takes them from there. An
// effect that changed after that ( an update, a move ) or did not fit is drawn the old way. Only with
// "mt_particles 1"; "mt_particles 0" = the old way for all.
// Створено (Ivan Danko, 2026-11-18): крок 2б плану FPS, частина B ( "уся геометрія частинок кадру однією
// пачкою на всіх ядрах" ). Раніше кожне малювання ефекту-спрайта знову будувало його вершини в потоці
// малювання ( ParticleRenderStream ) у спільний потік вершин: у лінзі 3D-прицілу, в основному виді й ще
// раз для марева. Тепер ефекти, які кадр малюватиме, чекають у черзі ( туди їх ставить граф сцени,
// r__dsgraph_build.cpp ); на першому малюванні вершини їх усіх будуються разом, на всіх ядрах, у буфер
// нижче, і кожне малювання кадру бере їх звідти. Ефект, що змінився після цього ( оновлення, рух ) або
// не вмістився, малюється по-старому. Лише з "mt_particles 1"; "mt_particles 0" = по-старому для всіх.
#include "xrCore/xrTasks.h"

bool r4_ps_batch_on(); // xrRenderPC_R4\r4_R_render.cpp: "mt_particles 1" and the game's update entry

namespace {
constexpr u32 r4_ps_geom_quads = 4096;           // RCache.QuadIB holds 4096 quads: one a particle
constexpr u32 r4_ps_geom_piece = 256;            // particles of one effect in one piece of work
constexpr u32 r4_ps_geom_task = 512;             // particles a task at least ( less costs more to hand out )
constexpr u32 r4_ps_geom_min = 4 * 1024 * 1024;  // the buffer's first size, bytes
constexpr u32 r4_ps_geom_max = 64 * 1024 * 1024; // the buffer's biggest size, bytes
constexpr u32 r4_ps_geom_step = 1024 * 1024;     // the buffer grows in these steps, bytes

struct r4_ps_geom_state {
    ID3DVertexBuffer* vb = nullptr;     // the buffer
    ref_geom* geom = nullptr;           // its geometry ( on the heap: no destructor at the DLL's unload )
    bool broken = false;                // no buffer could be made: the old way until the next reset
    u32 size = 0;                       // the buffer's size, bytes
    u32 used = 0;                       // filled in this frame, bytes
    u32 need = 0;                       // asked for in this frame, bytes ( the next frame's size )
    u32 frame = 0;                      // the frame of used / need / stamp
    u32 stamp = 1;                      // new with every frame and every buffer, never 0
    bool discarded = false;             // mapped with DISCARD in this frame already
    xr_vector<CParticleEffect*> queue;  // this frame's effects waiting for their vertices
    xr_vector<CParticleEffect*> filled; // the effects of one fill
    xr_vector<PRS_PARAMS> pieces;       // the pieces of work of one fill
};
r4_ps_geom_state r4_ps_geom;

void r4_ps_geom_free(r4_ps_geom_state& g) {
    if (g.geom) {
        g.geom->destroy();
        xr_delete(g.geom);
    }
    if (g.vb) {
        HW.stats_manager.decrement_stats_vb(g.vb);
        _RELEASE(g.vb);
    }
    g.size = 0;
    g.used = 0;
    g.discarded = false;
    if (++g.stamp == 0)
        g.stamp = 1;
}

bool r4_ps_geom_create(r4_ps_geom_state& g, u32 size) {
    D3D_BUFFER_DESC desc = {};
    desc.ByteWidth = size;
    desc.Usage = D3D_USAGE_DYNAMIC;
    desc.BindFlags = D3D_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    ID3DVertexBuffer* vb = nullptr;
    if (FAILED(HW.pDevice->CreateBuffer(&desc, nullptr, &vb)) || !vb)
        return false;
    HW.stats_manager.increment_stats_vb(vb);
    g.vb = vb;
    g.geom = xr_new<ref_geom>();
    g.geom->create(FVF::F_LIT, g.vb, RCache.QuadIB);
    if ((*g.geom)->vb_stride != sizeof(FVF::LIT)) {
        r4_ps_geom_free(g);
        return false;
    }
    g.size = size;
    g.used = 0;
    g.discarded = false;
    return true;
}

// the first call of a frame: the last frame's queue goes untouched ( those effects may be gone ), the
// new stamp leaves every effect unfilled, the buffer grows when the last frame asked for more
void r4_ps_geom_frame(r4_ps_geom_state& g) {
    if (g.frame == Device.dwFrame)
        return;
    g.frame = Device.dwFrame;
    if (g.vb && g.need > g.size && g.size < r4_ps_geom_max) {
        const u32 old_size = g.size;
        u64 want = u64(g.need) + g.need / 4;
        want = (want + r4_ps_geom_step - 1) / r4_ps_geom_step * r4_ps_geom_step;
        const u32 size = u32(std::min<u64>(want, r4_ps_geom_max));
        r4_ps_geom_free(g);
        if (!r4_ps_geom_create(g, size) && !r4_ps_geom_create(g, old_size))
            g.broken = true;
    }
    g.used = 0;
    g.need = 0;
    g.discarded = false;
    if (++g.stamp == 0)
        g.stamp = 1;
    g.queue.clear();
}

// the vertices of every effect in the queue at once: the places first ( one after another behind what
// this frame filled already ), then one map of the buffer, the pieces on all cores, unmap
void r4_ps_geom_fill(r4_ps_geom_state& g) {
    r4_ps_geom_frame(g);
    if (g.queue.empty())
        return;
    if (!g.vb && !g.broken && !r4_ps_geom_create(g, r4_ps_geom_min))
        g.broken = true;
    if (!g.vb) {
        for (CParticleEffect* e : g.queue)
            e->ps_geom_queued = 0;
        g.queue.clear();
        return;
    }

    constexpr u32 stride = sizeof(FVF::LIT);
    u32 at = g.used;
    g.filled.clear();
    for (CParticleEffect* e : g.queue) {
        e->ps_geom_queued = 0;
        PAPI::Particle* particles;
        u32 p_cnt;
        ParticleManager()->GetParticles(e->GetHandleEffect(), particles, p_cnt);
        if (!p_cnt || p_cnt > r4_ps_geom_quads)
            continue; // nothing to draw, or more than the index buffer holds: the old way, as before
        const u32 bytes = p_cnt * 4 * stride;
        g.need += bytes;
        if (bytes > g.size - at)
            continue; // does not fit: the old way, the next frame's buffer is bigger
        e->ps_geom_offset = at / stride;
        e->ps_geom_count = p_cnt * 4;
        at += bytes;
        g.filled.push_back(e);
    }
    g.queue.clear();
    if (g.filled.empty())
        return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    const D3D_MAP mode = g.discarded ? D3D_MAP_WRITE_NO_OVERWRITE : D3D_MAP_WRITE_DISCARD;
    if (FAILED(HW.pContext->Map(g.vb, 0, mode, 0, &mapped)) || !mapped.pData) {
        g.filled.clear();
        return;
    }
    g.discarded = true;

    FVF::LIT* base = static_cast<FVF::LIT*>(mapped.pData);
    u32 total = 0;
    g.pieces.clear();
    for (CParticleEffect* e : g.filled) {
        PAPI::Particle* particles;
        u32 p_cnt;
        ParticleManager()->GetParticles(e->GetHandleEffect(), particles, p_cnt);
        const u32 count = std::min(p_cnt, e->ps_geom_count / 4); // never past the effect's place
        total += count;
        for (u32 from = 0; from < count; from += r4_ps_geom_piece) {
            PRS_PARAMS piece;
            piece.pv = base + e->ps_geom_offset + from * 4;
            piece.p_from = from;
            piece.p_to = std::min(count, from + r4_ps_geom_piece);
            piece.particles = particles;
            piece.pPE = e;
            g.pieces.push_back(piece);
        }
    }
    PRS_PARAMS* pieces = g.pieces.data();
    const u32 n = u32(g.pieces.size());
    u32 tasks = total / r4_ps_geom_task;
    const u32 most = xrTasks::Slots() * 4;
    if (tasks > most)
        tasks = most;
    if (tasks < 1)
        tasks = 1;
    const u32 grain = (n + tasks - 1) / tasks;
    xrTasks::ParallelFor(n, grain, [pieces](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i)
            ParticleRenderStream(&pieces[i]);
    });
    HW.pContext->Unmap(g.vb, 0);

    g.used = at;
    for (CParticleEffect* e : g.filled)
        e->ps_geom_stamp = g.stamp;
    g.filled.clear();
}

// one draw of an effect from the buffer: true = drawn, false = draw it the old way; the draw itself is
// the old way's ( CParticleEffect::Render below ) with this buffer and the effect's place in it
bool r4_ps_geom_draw(CParticleEffect& e, u32 p_cnt) {
    r4_ps_geom_state& g = r4_ps_geom;
    r4_ps_geom_frame(g);
    if (e.ps_geom_stamp != g.stamp) {
        if (e.ps_geom_queued != Device.dwFrame)
            return false;
        r4_ps_geom_fill(g);
        if (e.ps_geom_stamp != g.stamp)
            return false;
    }
    const u32 dwCount = p_cnt << 2;
    if (!g.geom || e.ps_geom_count != dwCount)
        return false;

    Fmatrix Pold = Device.mProject;
    Fmatrix FTold = Device.mFullTransform;
    if (e.GetHudMode()) {
        RDEVICE.mProject.build_projection(deg2rad(psHUD_FOV * Device.fFOV), Device.fASPECT,
                                          IE_VIEWPORT_NEAR,
                                          g_pGamePersistent->Environment().CurrentEnv->far_plane);

        Device.mFullTransform.mul(Device.mProject, Device.mView);
        RCache.set_xform_project(Device.mProject);
        RImplementation.rmNear();
        ApplyTexgen(Device.mFullTransform);
    }

    RCache.set_xform_world(Fidentity);
    RCache.set_Geometry(*g.geom);

    RCache.set_CullMode(e.m_Def->m_Flags.is(CPEDef::dfCulling)
                            ? (e.m_Def->m_Flags.is(CPEDef::dfCullCCW) ? CULL_CCW : CULL_CW)
                            : CULL_NONE);
    RCache.Render(D3DPT_TRIANGLELIST, e.ps_geom_offset, 0, dwCount, 0, dwCount / 2);
    RCache.set_CullMode(CULL_CCW);
    if (e.GetHudMode()) {
        RImplementation.rmNormal();
        Device.mProject = Pold;
        Device.mFullTransform = FTold;
        RCache.set_xform_project(Device.mProject);
        ApplyTexgen(Device.mFullTransform);
    }
    return true;
}
} // namespace

// the scene graph puts here every effect the frame will draw ( r__dsgraph_build.cpp, insert_dynamic )
void r4_ps_geom_queue(dxRender_Visual* visual) {
    r4_ps_geom_state& g = r4_ps_geom;
    if (g.broken || !r4_ps_batch_on())
        return;
    auto* e = static_cast<CParticleEffect*>(visual);
    if (!e->m_Def || !e->m_Def->m_Flags.is(CPEDef::dfSprite))
        return;
    r4_ps_geom_frame(g);
    if (e->ps_geom_stamp == g.stamp || e->ps_geom_queued == Device.dwFrame)
        return;
    e->ps_geom_queued = Device.dwFrame;
    g.queue.push_back(e);
}

// the renderer's end and a device reset ( r4.cpp: CRender::destroy, CRender::reset_begin ): the buffer
// goes before the resources it depends on; a new one is made when it is needed
void r4_ps_geom_release() {
    r4_ps_geom_state& g = r4_ps_geom;
    r4_ps_geom_free(g);
    g.broken = false;
    g.need = 0;
    g.frame = 0;
    g.queue.clear();
    g.filled.clear();
    g.pieces.clear();
}
#endif

void CParticleEffect::Render(float) {
    u32 dwOffset, dwCount;
    PAPI::Particle* particles;
    u32 p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

    if (p_cnt) {
        if (m_Def && m_Def->m_Flags.is(CPEDef::dfSprite)) {
#if RENDER == R_R4
            // Written by Ivan Danko, 2026-11-18: FPS plan step 2b part B - the vertices from the frame's
            // particle vertex buffer, built for all the effects at once ( r4_ps_geom_draw above ); the
            // code below is the old way, for what is not there.
            // Створено (Ivan Danko, 2026-11-18): крок 2б плану FPS, частина B - вершини з буфера вершин
            // частинок кадру, збудовані для всіх ефектів разом ( r4_ps_geom_draw вище ); код нижче -
            // старий шлях для того, чого там немає.
            if (r4_ps_geom_draw(*this, p_cnt))
                return;
#endif
            FVF::LIT* pv_start = (FVF::LIT*)RCache.Vertex.Lock(p_cnt * 4 * 4, geom->vb_stride, dwOffset);
            FVF::LIT* pv = pv_start;

#if RENDER == R_R4
            // Edited by Ivan Danko, 2026-11-14: FPS fix step 1 - an effect whose vertices are one piece
            // of work fills them right here. Handing that one piece to a task worker and waiting for it
            // cost more than the work itself, and a far view draws hundreds of effects a frame. Bigger
            // effects are cut into pieces as before; the #else part is the code as it was.
            // Змінено (Ivan Danko, 2026-11-14): крок 1 виправлення FPS - ефект, вершини якого є одним
            // шматком роботи, заповнює їх одразу тут. Передача цього шматка робочому потоку й
            // очікування на нього коштували більше за саму роботу, а дальній вид малює сотні ефектів
            // за кадр. Більші ефекти діляться на шматки, як і раніше; у частині #else - код без змін.
            size_t nWorkers = ttapi.threads.size();

            if (p_cnt < nWorkers * 20)
                nWorkers = 1;

            if (nWorkers == 1) {
                PRS_PARAMS prsParam;
                prsParam.pv = pv;
                prsParam.p_from = 0;
                prsParam.p_to = p_cnt;
                prsParam.particles = particles;
                prsParam.pPE = this;
                ParticleRenderStream((void*)&prsParam);
            } else {
                // КРИТИЧНИЙ ФІКС: Безпечна пам'ять (std::vector) замість _alloca
                std::vector<PRS_PARAMS> prsParams(nWorkers);

                u32 nSlice = p_cnt / 128;
                u32 nStep = ((p_cnt - nSlice) / nWorkers);

                for (u32 i = 0; i < nWorkers; ++i) {
                    prsParams[i].pv = pv + i * nStep * 4;
                    prsParams[i].p_from = i * nStep;
                    prsParams[i].p_to = (i == (nWorkers - 1)) ? p_cnt : (prsParams[i].p_from + nStep);
                    prsParams[i].particles = particles;
                    prsParams[i].pPE = this;

                    PRS_PARAMS* pParam = &prsParams[i];
                    ttapi.threads[i]->addJob([pParam] { ParticleRenderStream((void*)pParam); });
                }

                ttapi.wait();
            }
#else
            size_t nWorkers = ttapi.threads.size();

            if (p_cnt < nWorkers * 20)
                nWorkers = 1;

            // КРИТИЧНИЙ ФІКС: Безпечна пам'ять (std::vector) замість _alloca
            std::vector<PRS_PARAMS> prsParams(nWorkers);

            u32 nSlice = p_cnt / 128;
            u32 nStep = ((p_cnt - nSlice) / nWorkers);

            for (u32 i = 0; i < nWorkers; ++i) {
                prsParams[i].pv = pv + i * nStep * 4;
                prsParams[i].p_from = i * nStep;
                prsParams[i].p_to = (i == (nWorkers - 1)) ? p_cnt : (prsParams[i].p_from + nStep);
                prsParams[i].particles = particles;
                prsParams[i].pPE = this;
                
                PRS_PARAMS* pParam = &prsParams[i];
                ttapi.threads[i]->addJob([pParam] { ParticleRenderStream((void*)pParam); });
            }

            ttapi.wait();
#endif

            dwCount = p_cnt << 2;

            RCache.Vertex.Unlock(dwCount, geom->vb_stride);
            if (dwCount) {
#ifndef _EDITOR
                Fmatrix Pold = Device.mProject;
                Fmatrix FTold = Device.mFullTransform;
                if (GetHudMode()) {
                    RDEVICE.mProject.build_projection(
                        deg2rad(psHUD_FOV * Device.fFOV), Device.fASPECT, IE_VIEWPORT_NEAR,
                        g_pGamePersistent->Environment().CurrentEnv->far_plane);

                    Device.mFullTransform.mul(Device.mProject, Device.mView);
                    RCache.set_xform_project(Device.mProject);
                    RImplementation.rmNear();
                    ApplyTexgen(Device.mFullTransform);
                }
#endif

                RCache.set_xform_world(Fidentity);
                RCache.set_Geometry(geom);

                RCache.set_CullMode(
                    m_Def->m_Flags.is(CPEDef::dfCulling)
                        ? (m_Def->m_Flags.is(CPEDef::dfCullCCW) ? CULL_CCW : CULL_CW)
                        : CULL_NONE);
                RCache.Render(D3DPT_TRIANGLELIST, dwOffset, 0, dwCount, 0, dwCount / 2);
                RCache.set_CullMode(CULL_CCW);
#ifndef _EDITOR
                if (GetHudMode()) {
                    RImplementation.rmNormal();
                    Device.mProject = Pold;
                    Device.mFullTransform = FTold;
                    RCache.set_xform_project(Device.mProject);
                    ApplyTexgen(Device.mFullTransform);
                }
#endif
            }
        }
    }
}

#else

IC void FillSprite(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos,
                   const Fvector2& lt, const Fvector2& rb, float r1, float r2, u32 clr,
                   float angle) {
    float sa = std::sin(angle);
    float ca = std::cos(angle);
    Fvector Vr, Vt;
    Vr.x = T.x * r1 * sa + R.x * r1 * ca;
    Vr.y = T.y * r1 * sa + R.y * r1 * ca;
    Vr.z = T.z * r1 * sa + R.z * r1 * ca;
    Vt.x = T.x * r2 * ca - R.x * r2 * sa;
    Vt.y = T.y * r2 * ca - R.y * r2 * sa;
    Vt.z = T.z * r2 * ca - R.z * r2 * sa;

    Fvector a, b, c, d;
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.invert(a);
    d.invert(b);
    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y);
    pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y);
    pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y);
    pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y);
    pv++;
}

IC void FillSprite(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt,
                   const Fvector2& rb, float r1, float r2, u32 clr, float angle) {
    float sa = std::sin(angle);
    float ca = std::cos(angle);
    const Fvector& T = dir;
    Fvector R;
    R.crossproduct(T, RDEVICE.vCameraDirection).normalize_safe();
    Fvector Vr, Vt;
    Vr.x = T.x * r1 * sa + R.x * r1 * ca;
    Vr.y = T.y * r1 * sa + R.y * r1 * ca;
    Vr.z = T.z * r1 * sa + R.z * r1 * ca;
    Vt.x = T.x * r2 * ca - R.x * r2 * sa;
    Vt.y = T.y * r2 * ca - R.y * r2 * sa;
    Vt.z = T.z * r2 * ca - R.z * r2 * sa;

    Fvector a, b, c, d;
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.invert(a);
    d.invert(b);
    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y);
    pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y);
    pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y);
    pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y);
    pv++;
}

extern ENGINE_API float psHUD_FOV;
void CParticleEffect::Render(float) {
    u32 dwOffset, dwCount;
    PAPI::Particle* particles;
    u32 p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

    if (p_cnt > 0) {
        if (m_Def && m_Def->m_Flags.is(CPEDef::dfSprite)) {
            FVF::LIT* pv_start = (FVF::LIT*)RCache.Vertex.Lock(p_cnt * 4 * 4, geom->vb_stride, dwOffset);
            FVF::LIT* pv = pv_start;

            for (u32 i = 0; i < p_cnt; i++) {
                PAPI::Particle& m = particles[i];

                Fvector2 lt, rb;
                lt.set(0.f, 0.f);
                rb.set(1.f, 1.f);
                if (m_Def->m_Flags.is(CPEDef::dfFramed))
                    m_Def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);
                
                float r_x = m.size.x * 0.5f;
                float r_y = m.size.y * 0.5f;
                
                if (m_Def->m_Flags.is(CPEDef::dfVelocityScale)) {
                    float speed = m.vel.magnitude();
                    r_x += speed * m_Def->m_VelocityScale.x;
                    r_y += speed * m_Def->m_VelocityScale.y;
                }
                
                if (m_Def->m_Flags.is(CPEDef::dfAlignToPath)) {
                    float speed = m.vel.magnitude();
                    if ((speed < EPS_S) && m_Def->m_Flags.is(CPEDef::dfWorldAlign)) {
                        Fmatrix M;
                        M.setXYZ(m_Def->m_APDefaultRotation);
                        if (m_RT_Flags.is(flRT_XFORM)) {
                            Fvector p;
                            m_XFORM.transform_tiny(p, m.pos);
                            M.mulA_43(m_XFORM);
                            FillSprite(pv, M.k, M.i, p, lt, rb, r_x, r_y, m.color, m.rot.x);
                        } else {
                            FillSprite(pv, M.k, M.i, m.pos, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    } else if ((speed >= EPS_S) && m_Def->m_Flags.is(CPEDef::dfFaceAlign)) {
                        Fmatrix M;
                        M.identity();
                        M.k.div(m.vel, speed);
                        M.j.set(0, 1, 0);
                        if (std::abs(M.j.dotproduct(M.k)) > .99f)
                            M.j.set(0, 0, 1);
                        M.i.crossproduct(M.j, M.k);
                        M.i.normalize();
                        M.j.crossproduct(M.k, M.i);
                        M.j.normalize();
                        if (m_RT_Flags.is(flRT_XFORM)) {
                            Fvector p;
                            m_XFORM.transform_tiny(p, m.pos);
                            M.mulA_43(m_XFORM);
                            FillSprite(pv, M.j, M.i, p, lt, rb, r_x, r_y, m.color, m.rot.x);
                        } else {
                            FillSprite(pv, M.j, M.i, m.pos, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    } else {
                        Fvector dir;
                        if (speed >= EPS_S)
                            dir.div(m.vel, speed);
                        else
                            dir.setHP(-m_Def->m_APDefaultRotation.y, -m_Def->m_APDefaultRotation.x);
                        if (m_RT_Flags.is(flRT_XFORM)) {
                            Fvector p, d;
                            m_XFORM.transform_tiny(p, m.pos);
                            m_XFORM.transform_dir(d, dir);
                            FillSprite(pv, p, d, lt, rb, r_x, r_y, m.color, m.rot.x);
                        } else {
                            FillSprite(pv, m.pos, dir, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    }
                } else {
                    if (m_RT_Flags.is(flRT_XFORM)) {
                        Fvector p;
                        m_XFORM.transform_tiny(p, m.pos);
                        FillSprite(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, p, lt, rb, r_x,
                                   r_y, m.color, m.rot.x);
                    } else {
                        FillSprite(pv, RDEVICE.vCameraTop, RDEVICE.vCameraRight, m.pos, lt, rb, r_x,
                                   r_y, m.color, m.rot.x);
                    }
                }
            }
            dwCount = u32(pv - pv_start);
            RCache.Vertex.Unlock(dwCount, geom->vb_stride);
            if (dwCount) {
#ifndef _EDITOR
                Fmatrix Pold = Device.mProject;
                Fmatrix FTold = Device.mFullTransform;
                if (GetHudMode()) {
                    RDEVICE.mProject.build_projection(
                        deg2rad(psHUD_FOV * Device.fFOV), Device.fASPECT, IE_VIEWPORT_NEAR,
                        g_pGamePersistent->Environment().CurrentEnv->far_plane);

                    Device.mFullTransform.mul(Device.mProject, Device.mView);
                    RCache.set_xform_project(Device.mProject);
                    RImplementation.rmNear();
                    ApplyTexgen(Device.mFullTransform);
                }
#endif

                RCache.set_xform_world(Fidentity);
                RCache.set_Geometry(geom);

                RCache.set_CullMode(
                    m_Def->m_Flags.is(CPEDef::dfCulling)
                        ? (m_Def->m_Flags.is(CPEDef::dfCullCCW) ? CULL_CCW : CULL_CW)
                        : CULL_NONE);
                RCache.Render(D3DPT_TRIANGLELIST, dwOffset, 0, dwCount, 0, dwCount / 2);
                RCache.set_CullMode(CULL_CCW);
#ifndef _EDITOR
                if (GetHudMode()) {
                    RImplementation.rmNormal();
                    Device.mProject = Pold;
                    Device.mFullTransform = FTold;
                    RCache.set_xform_project(Device.mProject);
                    ApplyTexgen(Device.mFullTransform);
                }
#endif
            }
        }
    }
}

#endif // _EDITOR