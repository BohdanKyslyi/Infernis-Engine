#ifndef particle_actionsH
#define particle_actionsH
#pragma once

namespace PAPI {

struct ParticleEffect;

struct PARTICLES_API ParticleAction {
    enum { ALLOW_ROTATE = (1 << 1) };
    Flags32 m_Flags;
    PActionEnum type;
    
    ParticleAction() { m_Flags.zero(); }
    
    virtual ~ParticleAction() = default;

    virtual void Execute(ParticleEffect* pe, const float dt, float& m_max) = 0;
    virtual void Transform(const Fmatrix& m) = 0;

    virtual void Load(IReader& F) = 0;
    virtual void Save(IWriter& F) = 0;
};

using PAVec = xr_vector<ParticleAction*>;
using PAVecIt = PAVec::iterator;

class ParticleActions {
    PAVec actions;
    bool m_bLocked = false;

public:
    ParticleActions() {
        actions.reserve(4);
    }
    
    ~ParticleActions() { clear(); }
    
    void clear() {
        R_ASSERT(!m_bLocked);
        for (auto* action : actions) {
            xr_delete(action);
        }
        actions.clear();
    }
    
    void append(ParticleAction* pa) {
        R_ASSERT(!m_bLocked);
        actions.push_back(pa);
    }
    
    [[nodiscard]] bool empty() const { return actions.empty(); }
    [[nodiscard]] PAVecIt begin() { return actions.begin(); }
    [[nodiscard]] PAVecIt end() { return actions.end(); }
    [[nodiscard]] int size() const { return static_cast<int>(actions.size()); }
    
    void resize(int cnt) {
        R_ASSERT(!m_bLocked);
        actions.resize(cnt);
    }
    
    void copy(ParticleActions* src);
    
    void lock() {
        R_ASSERT(!m_bLocked);
        m_bLocked = true;
    }
    
    void unlock() {
        R_ASSERT(m_bLocked);
        m_bLocked = false;
    }
};

}; // namespace PAPI
#endif