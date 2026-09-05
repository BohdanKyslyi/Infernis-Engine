#include "stdafx.h"
#pragma hdrstop

#include "particle_manager.h"
#include "particle_effect.h"
#include "particle_actions_collection.h"
#include <algorithm> 

using namespace PAPI;

// system
CParticleManager PM;
PARTICLES_API IParticleManager* PAPI::ParticleManager() { return &PM; }

CParticleManager::CParticleManager() = default;

CParticleManager::~CParticleManager() {
    for (auto* effect : effect_vec) {
        xr_delete(effect);
    }
    effect_vec.clear();

    for (auto* alist : m_alist_vec) {
        xr_delete(alist);
    }
    m_alist_vec.clear();
}

ParticleEffect* CParticleManager::GetEffectPtr(int effect_id) {
    R_ASSERT(effect_id >= 0 && effect_id < static_cast<int>(effect_vec.size()));
    return effect_vec[effect_id];
}

ParticleActions* CParticleManager::GetActionListPtr(int a_list_num) {
    R_ASSERT(a_list_num >= 0 && a_list_num < static_cast<int>(m_alist_vec.size()));
    return m_alist_vec[a_list_num];
}

// create
int CParticleManager::CreateEffect(u32 max_particles) {
    auto it = std::find(effect_vec.begin(), effect_vec.end(), nullptr);
    int eff_id = -1;

    if (it != effect_vec.end()) {
        eff_id = static_cast<int>(std::distance(effect_vec.begin(), it));
    } else {
        eff_id = static_cast<int>(effect_vec.size());
        effect_vec.push_back(nullptr);
    }

    effect_vec[eff_id] = xr_new<ParticleEffect>(max_particles);
    return eff_id;
}

void CParticleManager::DestroyEffect(int effect_id) {
    R_ASSERT(effect_id >= 0 && effect_id < static_cast<int>(effect_vec.size()));
    xr_delete(effect_vec[effect_id]); 
}

int CParticleManager::CreateActionList() {
    auto it = std::find(m_alist_vec.begin(), m_alist_vec.end(), nullptr);
    int list_id = -1;

    if (it != m_alist_vec.end()) {
        list_id = static_cast<int>(std::distance(m_alist_vec.begin(), it));
    } else {
        list_id = static_cast<int>(m_alist_vec.size());
        m_alist_vec.push_back(nullptr);
    }

    m_alist_vec[list_id] = xr_new<ParticleActions>();
    return list_id;
}

void CParticleManager::DestroyActionList(int alist_id) {
    R_ASSERT(alist_id >= 0 && alist_id < static_cast<int>(m_alist_vec.size()));
    xr_delete(m_alist_vec[alist_id]);
}

// control
void CParticleManager::PlayEffect(int effect_id, int alist_id) {
    ParticleActions* pa = GetActionListPtr(alist_id);
    VERIFY(pa);
    if (!pa) return; 

    pa->lock();
    
    for (auto* action : *pa) {
        VERIFY(action);
        switch (action->type) {
        case PASourceID:
            static_cast<PASource*>(action)->m_Flags.set(PASource::flSilent, FALSE);
            break;
        case PAExplosionID:
            static_cast<PAExplosion*>(action)->age = 0.0f;
            break;
        case PATurbulenceID:
            static_cast<PATurbulence*>(action)->age = 0.0f;
            break;
        default:
            break;
        }
    }
    pa->unlock();
}

void CParticleManager::StopEffect(int effect_id, int alist_id, BOOL deffered) {
    ParticleActions* pa = GetActionListPtr(alist_id);
    VERIFY(pa);
    if (!pa) return;

    pa->lock();

    for (auto* action : *pa) {
        if (action->type == PASourceID) {
            static_cast<PASource*>(action)->m_Flags.set(PASource::flSilent, TRUE);
        }
    }
    
    if (!deffered) {
        ParticleEffect* pe = GetEffectPtr(effect_id);
        pe->p_count = 0;
    }
    pa->unlock();
}

// update&render
void CParticleManager::Update(int effect_id, int alist_id, float dt) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    ParticleActions* pa = GetActionListPtr(alist_id);

    VERIFY(pa && pe);

    pa->lock();

    float kill_old_time = 1.0f;
    for (auto* action : *pa) {
        VERIFY(action);
        action->Execute(pe, dt, kill_old_time);
    }
    
    pa->unlock();
}

void CParticleManager::Render(int effect_id) { }

void CParticleManager::Transform(int alist_id, const Fmatrix& full, const Fvector& vel) {
    ParticleActions* pa = GetActionListPtr(alist_id);
    VERIFY(pa);
    if (!pa) return;

    pa->lock();

    Fmatrix mT;
    mT.translate(full.c);

    for (auto* action : *pa) {
        const bool r = action->m_Flags.is(ParticleAction::ALLOW_ROTATE);
        const Fmatrix& m = r ? full : mT;
        
        action->Transform(m);
        
        if (action->type == PASourceID) {
            auto* source = static_cast<PASource*>(action);
            source->parent_vel = pVector(vel.x, vel.y, vel.z) * source->parent_motion;
        }
    }
    pa->unlock();
}

// effect
void CParticleManager::RemoveParticle(int effect_id, u32 p_id) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    pe->Remove(p_id);
}
void CParticleManager::SetMaxParticles(int effect_id, u32 max_particles) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    pe->Resize(max_particles);
}
void CParticleManager::SetCallback(int effect_id, OnBirthParticleCB b, OnDeadParticleCB d, void* owner, u32 param) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    pe->b_cb = b;
    pe->d_cb = d;
    pe->owner = owner;
    pe->param = param;
}
void CParticleManager::GetParticles(int effect_id, Particle*& particles, u32& cnt) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    particles = pe->particles;
    cnt = pe->p_count;
}
u32 CParticleManager::GetParticlesCount(int effect_id) {
    ParticleEffect* pe = GetEffectPtr(effect_id);
    return pe->p_count;
}

// action
ParticleAction* CParticleManager::CreateAction(PActionEnum type) {
    ParticleAction* pa = nullptr;
    switch (type) {
    case PAAvoidID:           pa = xr_new<PAAvoid>(); break;
    case PABounceID:          pa = xr_new<PABounce>(); break;
    case PACopyVertexBID:     pa = xr_new<PACopyVertexB>(); break;
    case PADampingID:         pa = xr_new<PADamping>(); break;
    case PAExplosionID:       pa = xr_new<PAExplosion>(); break;
    case PAFollowID:          pa = xr_new<PAFollow>(); break;
    case PAGravitateID:       pa = xr_new<PAGravitate>(); break;
    case PAGravityID:         pa = xr_new<PAGravity>(); break;
    case PAJetID:             pa = xr_new<PAJet>(); break;
    case PAKillOldID:         pa = xr_new<PAKillOld>(); break;
    case PAMatchVelocityID:   pa = xr_new<PAMatchVelocity>(); break;
    case PAMoveID:            pa = xr_new<PAMove>(); break;
    case PAOrbitLineID:       pa = xr_new<PAOrbitLine>(); break;
    case PAOrbitPointID:      pa = xr_new<PAOrbitPoint>(); break;
    case PARandomAccelID:     pa = xr_new<PARandomAccel>(); break;
    case PARandomDisplaceID:  pa = xr_new<PARandomDisplace>(); break;
    case PARandomVelocityID:  pa = xr_new<PARandomVelocity>(); break;
    case PARestoreID:         pa = xr_new<PARestore>(); break;
    case PASinkID:            pa = xr_new<PASink>(); break;
    case PASinkVelocityID:    pa = xr_new<PASinkVelocity>(); break;
    case PASourceID:          pa = xr_new<PASource>(); break;
    case PASpeedLimitID:      pa = xr_new<PASpeedLimit>(); break;
    case PATargetColorID:     pa = xr_new<PATargetColor>(); break;
    case PATargetSizeID:      pa = xr_new<PATargetSize>(); break;
    case PATargetRotateID:
    case PATargetRotateDID:   pa = xr_new<PATargetRotate>(); break;
    case PATargetVelocityID:
    case PATargetVelocityDID: pa = xr_new<PATargetVelocity>(); break;
    case PAVortexID:          pa = xr_new<PAVortex>(); break;
    case PATurbulenceID:      pa = xr_new<PATurbulence>(); break;
    case PAScatterID:         pa = xr_new<PAScatter>(); break;
    default: NODEFAULT;
    }
    
    if (pa) pa->type = type;
    return pa;
}

u32 CParticleManager::LoadActions(int alist_id, IReader& R) {
    ParticleActions* pa = GetActionListPtr(alist_id);
    VERIFY(pa);
    pa->clear();
    
    if (R.length()) {
        u32 cnt = R.r_u32();
        for (u32 k = 0; k < cnt; k++) {
            auto* act = CreateAction(static_cast<PActionEnum>(R.r_u32()));
            act->Load(R);
            pa->append(act);
        }
    }
    return static_cast<u32>(pa->size());
}

void CParticleManager::SaveActions(int alist_id, IWriter& W) {
    ParticleActions* pa = GetActionListPtr(alist_id);
    VERIFY(pa);
    
    pa->lock();
    W.w_u32(static_cast<u32>(pa->size()));
    
    for (auto* action : *pa) {
        action->Save(W);
    }
    
    pa->unlock();
}