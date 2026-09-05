#include "stdafx.h"
#pragma hdrstop

#include "ParticleEffectDef.h"
#include "ParticleEffect.h"
#ifdef _EDITOR
#include "UI_ToolsCustom.h"
#include "ParticleEffectActions.h"
#endif

using namespace PAPI;
using namespace PS;

CPEDef::CPEDef() {
    m_Flags.zero();
}

CPEDef::~CPEDef() {
#ifdef _EDITOR
    for (auto* action : m_EActionList) {
        xr_delete(action);
    }
    m_EActionList.clear();
#endif
}

void CPEDef::CreateShader() {
    if (m_ShaderName.size() && m_TextureName.size())
        m_CachedShader.create(*m_ShaderName, *m_TextureName);
}

void CPEDef::DestroyShader() { 
    m_CachedShader.destroy(); 
}

void CPEDef::SetName(LPCSTR name) { 
    m_Name = name; 
}

void CPEDef::ExecuteAnimate(Particle* particles, u32 p_cnt, float dt) {
    const float speedFac = m_Frame.m_fSpeed * dt;
    const float iFrameCountF = static_cast<float>(m_Frame.m_iFrameCount);
    
    for (u32 i = 0; i < p_cnt; ++i) {
        Particle& m = particles[i];
        
        float f = (static_cast<float>(m.frame) / 255.0f) + 
                  (m.flags.is(Particle::ANIMATE_CCW) ? -1.0f : 1.0f) * speedFac;
                  
        if (f > iFrameCountF)
            f -= iFrameCountF;
        else if (f < 0.0f)
            f += iFrameCountF;
            
        m.frame = static_cast<u16>(iFloor(f * 255.0f));
    }
}

void CPEDef::ExecuteCollision(PAPI::Particle* particles, u32 p_cnt, float dt,
                              CParticleEffect* owner, CollisionCallback cb) {
    pVector pt, n;
    
    for (int i = static_cast<int>(p_cnt) - 1; i >= 0; --i) {
        Particle& m = particles[i];

        bool pick_needed;
        int pick_cnt = 0;
        
        do {
            pick_needed = false;
            Fvector dir;
            dir.sub(m.pos, m.posB);
            
            float dist = dir.magnitude();
            if (dist >= EPS) {
                dir.div(dist); 
#ifdef _EDITOR
                if (Tools->RayPick(m.posB, dir, dist, &pt, &n)) {
#else
                collide::rq_result RQ;
                collide::rq_target RT = m_Flags.is(dfCollisionDyn) ? collide::rqtBoth : collide::rqtStatic;
                
                if (g_pGameLevel->ObjectSpace.RayPick(m.posB, dir, dist, RT, RQ, nullptr)) {
                    pt.mad(m.posB, dir, RQ.range);
                    
                    if (RQ.O) {
                        n.set(0.0f, 1.0f, 0.0f);
                    } else {
                        CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
                        Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();
                        n.mknormal(verts[T->verts[0]], verts[T->verts[1]], verts[T->verts[2]]);
                    }
#endif
                    pick_cnt++;
                    
                    if (cb && (pick_cnt == 1)) {
                        if (!cb(owner, m, pt, n))
                            break;
                    }
                        
                    if (m_Flags.is(dfCollisionDel)) {
                        ParticleManager()->RemoveParticle(owner->GetHandleEffect(), i);
                    } else {
                        float nmag = m.vel * n;
                        pVector vn(n * nmag);   
                        pVector vt(m.vel - vn); 

                        if (vt.length2() <= m_fCollideSqrCutoff) {
                            m.vel = vt - vn * m_fCollideResilience;
                        } else {
                            m.vel = vt * m_fCollideOneMinusFriction - vn * m_fCollideResilience;
                        }
                        
                        m.pos = m.posB + m.vel * dt;
                        pick_needed = true;
                    }
                }
            } else {
                m.pos = m.posB;
            }
        } while (pick_needed && (pick_cnt < 2));
    }
}

//------------------------------------------------------------------------------
// I/O part
//------------------------------------------------------------------------------
BOOL CPEDef::Load(IReader& F) {
    R_ASSERT(F.find_chunk(PED_CHUNK_VERSION));
    u16 version = F.r_u16();

    if (version != PED_VERSION)
        return FALSE;

    R_ASSERT(F.find_chunk(PED_CHUNK_NAME));
    F.r_stringZ(m_Name);

    R_ASSERT(F.find_chunk(PED_CHUNK_EFFECTDATA));
    m_MaxParticles = F.r_u32();

    {
        u32 action_list = F.find_chunk(PED_CHUNK_ACTIONLIST);
        R_ASSERT(action_list);
        m_Actions.w(F.pointer(), action_list);
    }

    F.r_chunk(PED_CHUNK_FLAGS, &m_Flags);

    if (m_Flags.is(dfSprite)) {
        R_ASSERT(F.find_chunk(PED_CHUNK_SPRITE));
        F.r_stringZ(m_ShaderName);
        F.r_stringZ(m_TextureName);
    }

    if (m_Flags.is(dfFramed)) {
        R_ASSERT(F.find_chunk(PED_CHUNK_FRAME));
        F.r(&m_Frame, sizeof(SFrame));
    }

    if (m_Flags.is(dfTimeLimit)) {
        R_ASSERT(F.find_chunk(PED_CHUNK_TIMELIMIT));
        m_fTimeLimit = F.r_float();
    }

    if (m_Flags.is(dfCollision)) {
        R_ASSERT(F.find_chunk(PED_CHUNK_COLLISION));
        m_fCollideOneMinusFriction = F.r_float();
        m_fCollideResilience = F.r_float();
        m_fCollideSqrCutoff = F.r_float();
    }

    if (m_Flags.is(dfVelocityScale)) {
        R_ASSERT(F.find_chunk(PED_CHUNK_VEL_SCALE));
        F.r_fvector3(m_VelocityScale);
    }

    if (m_Flags.is(dfAlignToPath)) {
        if (F.find_chunk(PED_CHUNK_ALIGN_TO_PATH)) {
            F.r_fvector3(m_APDefaultRotation);
        }
    }

#ifdef _EDITOR
    if (pCreateEAction && F.find_chunk(PED_CHUNK_EDATA)) {
        m_EActionList.resize(F.r_u32());
        for (auto& action : m_EActionList) {
            PAPI::PActionEnum type = static_cast<PAPI::PActionEnum>(F.r_u32());
            action = pCreateEAction(type);
            action->Load(F);
        }
        Compile(m_EActionList);
    }
#endif

    return TRUE;
}

BOOL CPEDef::Load2(CInifile& ini) {
    m_MaxParticles = ini.r_u32("_effect", "max_particles");
    m_Flags.assign(ini.r_u32("_effect", "flags"));

    if (m_Flags.is(dfSprite)) {
        m_ShaderName = ini.r_string("sprite", "shader");
        m_TextureName = ini.r_string("sprite", "texture");
    }

    if (m_Flags.is(dfFramed)) {
        m_Frame.m_fTexSize = ini.r_fvector2("frame", "tex_size");
        m_Frame.reserved = ini.r_fvector2("frame", "reserved");
        m_Frame.m_iFrameDimX = ini.r_s32("frame", "dim_x");
        m_Frame.m_iFrameCount = ini.r_s32("frame", "frame_count");
        m_Frame.m_fSpeed = ini.r_float("frame", "speed");
    }

    if (m_Flags.is(dfTimeLimit)) {
        m_fTimeLimit = ini.r_float("timelimit", "value");
    }

    if (m_Flags.is(dfCollision)) {
        m_fCollideOneMinusFriction = ini.r_float("collision", "one_minus_friction");
        m_fCollideResilience = ini.r_float("collision", "collide_resilence");
        m_fCollideSqrCutoff = ini.r_float("collision", "collide_sqr_cutoff");
    }

    if (m_Flags.is(dfVelocityScale)) {
        m_VelocityScale = ini.r_fvector3("velocity_scale", "value");
    }

    if (m_Flags.is(dfAlignToPath)) {
        m_APDefaultRotation = ini.r_fvector3("align_to_path", "default_rotation");
    }
    
#ifdef _EDITOR
    if (pCreateEAction) {
        u32 count = ini.r_u32("_effect", "action_count");
        m_EActionList.resize(count);
        u32 action_id = 0;
        for (auto& action : m_EActionList) {
            string256 sect;
            xr_sprintf(sect, sizeof(sect), "action_%04d", action_id);
            PAPI::PActionEnum type = static_cast<PAPI::PActionEnum>(ini.r_u32(sect, "action_type"));
            action = pCreateEAction(type);
            action->Load2(ini, sect);
            ++action_id;
        }
        Compile(m_EActionList);
    }
#endif

    return TRUE;
}

void CPEDef::Save2(CInifile& ini) {
    ini.w_u16("_effect", "version", PED_VERSION);
    ini.w_u32("_effect", "max_particles", m_MaxParticles);
    ini.w_u32("_effect", "flags", m_Flags.get());

    if (m_Flags.is(dfSprite)) {
        ini.w_string("sprite", "shader", m_ShaderName.c_str());
        ini.w_string("sprite", "texture", m_TextureName.c_str());
    }

    if (m_Flags.is(dfFramed)) {
        ini.w_fvector2("frame", "tex_size", m_Frame.m_fTexSize);
        ini.w_fvector2("frame", "reserved", m_Frame.reserved);
        ini.w_s32("frame", "dim_x", m_Frame.m_iFrameDimX);
        ini.w_s32("frame", "frame_count", m_Frame.m_iFrameCount);
        ini.w_float("frame", "speed", m_Frame.m_fSpeed);
    }

    if (m_Flags.is(dfTimeLimit)) {
        ini.w_float("timelimit", "value", m_fTimeLimit);
    }

    if (m_Flags.is(dfCollision)) {
        ini.w_float("collision", "one_minus_friction", m_fCollideOneMinusFriction);
        ini.w_float("collision", "collide_resilence", m_fCollideResilience);
        ini.w_float("collision", "collide_sqr_cutoff", m_fCollideSqrCutoff);
    }

    if (m_Flags.is(dfVelocityScale)) {
        ini.w_fvector3("velocity_scale", "value", m_VelocityScale);
    }

    if (m_Flags.is(dfAlignToPath)) {
        ini.w_fvector3("align_to_path", "default_rotation", m_APDefaultRotation);
    }
    
#ifdef _EDITOR
    ini.w_u32("_effect", "action_count", static_cast<u32>(m_EActionList.size()));
    u32 action_id = 0;
    for (auto* action : m_EActionList) {
        string256 sect;
        xr_sprintf(sect, sizeof(sect), "action_%04d", action_id);
        ini.w_u32(sect, "action_type", action->type);
        action->Save2(ini, sect);
        ++action_id;
    }
#endif
}

void CPEDef::Save(IWriter& F) {
    F.open_chunk(PED_CHUNK_VERSION);
    F.w_u16(PED_VERSION);
    F.close_chunk();

    F.open_chunk(PED_CHUNK_NAME);
    F.w_stringZ(m_Name);
    F.close_chunk();

    F.open_chunk(PED_CHUNK_EFFECTDATA);
    F.w_u32(m_MaxParticles);
    F.close_chunk();

    F.open_chunk(PED_CHUNK_ACTIONLIST);
    F.w(m_Actions.pointer(), m_Actions.size());
    F.close_chunk();

    F.w_chunk(PED_CHUNK_FLAGS, &m_Flags, sizeof(m_Flags));

    if (m_Flags.is(dfSprite)) {
        F.open_chunk(PED_CHUNK_SPRITE);
        F.w_stringZ(m_ShaderName);
        F.w_stringZ(m_TextureName);
        F.close_chunk();
    }

    if (m_Flags.is(dfFramed)) {
        F.open_chunk(PED_CHUNK_FRAME);
        F.w(&m_Frame, sizeof(SFrame));
        F.close_chunk();
    }

    if (m_Flags.is(dfTimeLimit)) {
        F.open_chunk(PED_CHUNK_TIMELIMIT);
        F.w_float(m_fTimeLimit);
        F.close_chunk();
    }

    if (m_Flags.is(dfCollision)) {
        F.open_chunk(PED_CHUNK_COLLISION);
        F.w_float(m_fCollideOneMinusFriction);
        F.w_float(m_fCollideResilience);
        F.w_float(m_fCollideSqrCutoff);
        F.close_chunk();
    }

    if (m_Flags.is(dfVelocityScale)) {
        F.open_chunk(PED_CHUNK_VEL_SCALE);
        F.w_fvector3(m_VelocityScale);
        F.close_chunk();
    }

    if (m_Flags.is(dfAlignToPath)) {
        F.open_chunk(PED_CHUNK_ALIGN_TO_PATH);
        F.w_fvector3(m_APDefaultRotation);
        F.close_chunk();
    }
    
#ifdef _EDITOR
    F.open_chunk(PED_CHUNK_EDATA);
    F.w_u32(static_cast<u32>(m_EActionList.size()));
    for (auto* action : m_EActionList) {
        F.w_u32(action->type);
        action->Save(F);
    }
    F.close_chunk();
#endif
}

#ifdef _EDITOR
void PS::CPEDef::Compile(EPAVec& v) {
    m_Actions.clear();
    m_Actions.w_u32(static_cast<u32>(v.size()));
    u32 cnt = 0;

    for (auto* action : v) {
        if (action->flags.is(EParticleAction::flEnabled)) {
            action->Compile(m_Actions);
            cnt++;
        }
    }
    m_Actions.seek(0);
    m_Actions.w_u32(cnt);
}
#endif