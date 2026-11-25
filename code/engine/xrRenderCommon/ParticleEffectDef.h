#ifndef ParticleEffectDefH
#define ParticleEffectDefH
#pragma once

#include "Shader.h"

namespace PAPI {
struct Particle;
struct ParticleEffect;
struct PAHeader;
struct ParticleAction;
using PAVec = xr_vector<ParticleAction*>;
} // namespace PAPI

struct EParticleAction;

namespace PS {
class CParticleEffect;

typedef BOOL (*CollisionCallback)(CParticleEffect* E, PAPI::Particle& P, const Fvector& pt,
                                  const Fvector& norm); // TRUE-continue collision exec
typedef void (*DestroyCallback)(CParticleEffect* E, PAPI::Particle& P);

class PFunction;

struct SFrame {
    Fvector2 m_fTexSize = {32.0f / 256.0f, 64.0f / 128.0f};
    Fvector2 reserved = {0.0f, 0.0f};
    int m_iFrameDimX = 8;
    int m_iFrameCount = 16;
    float m_fSpeed = 24.0f;

    void InitDefault() {
        m_fTexSize.set(32.0f / 256.0f, 64.0f / 128.0f);
        m_iFrameDimX = 8;
        m_iFrameCount = 16;
        m_fSpeed = 24.0f;
    }
    
    IC void CalculateTC(int frame, Fvector2& lt, Fvector2& rb) const {
        lt.x = static_cast<float>(frame % m_iFrameDimX) * m_fTexSize.x;
        lt.y = static_cast<float>(frame / m_iFrameDimX) * m_fTexSize.y;
        rb.x = lt.x + m_fTexSize.x;
        rb.y = lt.y + m_fTexSize.y;
    }
};

class ECORE_API CPEDef {
public:
    enum {
        dfSprite = (1 << 0),
        dfFramed = (1 << 10),
        dfAnimated = (1 << 11),
        dfRandomFrame = (1 << 12),
        dfRandomPlayback = (1 << 13),
        dfTimeLimit = (1 << 14),
        dfAlignToPath = (1 << 15),
        dfCollision = (1 << 16),
        dfCollisionDel = (1 << 17),
        dfVelocityScale = (1 << 18),
        dfCollisionDyn = (1 << 19),
        dfWorldAlign = (1 << 20),
        dfFaceAlign = (1 << 21),
        dfCulling = (1 << 22),
        dfCullCCW = (1 << 23),
    };
    
    shared_str m_Name;
    Flags32 m_Flags;
    
    // texture
    shared_str m_ShaderName;
    shared_str m_TextureName;
    ref_shader m_CachedShader;
    SFrame m_Frame;
    
    // compiled actions
    CMemoryWriter m_Actions;
#if RENDER == R_R4
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the update step of every effect as in Anomaly
    // (Monolith ParticleEffectDef.h): 33 ms by default, a loose .pe file may set its own
    // ([_effect] update_step); the console value particle_update_mod scales it (GetUStep / GetFStep).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - крок оновлення кожного ефекту як в Anomaly
    // (Monolith ParticleEffectDef.h): типово 33 мс, окремий файл .pe може задати власний
    // ([_effect] update_step); консольне значення particle_update_mod масштабує його (GetUStep / GetFStep).
    u32 m_uStep = 33;
    float m_fStep = 33.0f / 1000.0f;
#endif
    
    float m_fTimeLimit = 0.0f;           
    int m_MaxParticles = 0;              
    Fvector m_VelocityScale = {0.0f, 0.0f, 0.0f};     
    Fvector m_APDefaultRotation = {-PI_DIV_2, 0.0f, 0.0f}; 
    
    // collision
    float m_fCollideOneMinusFriction = 1.0f;
    float m_fCollideResilience = 0.0f;
    float m_fCollideSqrCutoff = 0.0f;

public:
    BOOL SaveActionList(IWriter& F);
    BOOL LoadActionList(IReader& F);
    
    // execute
    void ExecuteAnimate(PAPI::Particle* particles, u32 p_cnt, float dt);
    void ExecuteCollision(PAPI::Particle* particles, u32 p_cnt, float dt, CParticleEffect* owner, CollisionCallback cb);

public:
    CPEDef();
    ~CPEDef();
#if RENDER == R_R4
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the effect's update step times
    // particle_update_mod, as in Monolith (ms / seconds).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - крок оновлення ефекту, помножений на
    // particle_update_mod, як у Monolith (мс / секунди).
    u32 GetUStep();
    float GetFStep();
#endif

    void SetName(LPCSTR name);
    [[nodiscard]] IC LPCSTR Name() const { return *m_Name; }
    void CreateShader();
    void DestroyShader();

    void Save(IWriter& F);
    BOOL Load(IReader& F);

    void Save2(CInifile& ini);
    BOOL Load2(CInifile& ini);

#if RENDER == R_R4
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the editor action list in the game, as in
    // Anomaly (Monolith builds it outside the editor too): the actions of a loose .pe file and the
    // editor data of particles.xr are compiled into m_Actions (ParticleEffectActions.h).
    // Adapted: DEFINE_VECTOR -> using (our xrCore has no DEFINE_VECTOR).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - список дій редактора у грі, як в Anomaly
    // (Monolith збирає його і поза редактором): дії окремого файлу .pe і дані редактора з particles.xr
    // компілюються в m_Actions (ParticleEffectActions.h).
    // Адаптовано: DEFINE_VECTOR -> using (у нашому xrCore немає DEFINE_VECTOR).
public:
    using EPAVec = xr_vector<EParticleAction*>;
    using EPAVecIt = EPAVec::iterator;
    EPAVec m_EActionList;

    void Compile(EPAVec& v);
#endif
#ifdef _EDITOR
public:
    DEFINE_VECTOR(EParticleAction*, EPAVec, EPAVecIt);
    EPAVec m_EActionList;

public:
    void __stdcall FindActionByName(LPCSTR new_name, bool& res);
    bool __stdcall NameOnAfterEdit(PropValue* sender, shared_str& edit_val);
    bool __stdcall CollisionFrictionOnAfterEdit(PropValue* sender, float& edit_val);
    void __stdcall CollisionFrictionOnBeforeEdit(PropValue* sender, float& edit_val);
    void __stdcall CollisionFrictionOnDraw(PropValue* sender, xr_string& draw_val);
    bool __stdcall CollisionCutoffOnAfterEdit(PropValue* sender, float& edit_val);
    void __stdcall CollisionCutoffOnBeforeEdit(PropValue* sender, float& edit_val);
    void __stdcall CollisionCutoffOnDraw(PropValue* sender, xr_string& draw_val);
    void __stdcall OnActionEditClick(ButtonValue* sender, bool& bDataModified, bool& bSafe);
    void __stdcall OnFrameResize(PropValue* sender);
    void __stdcall OnShaderChange(PropValue* sender);
    void __stdcall OnFlagChange(PropValue* sender);
    void __stdcall OnControlClick(ButtonValue* sender, bool& bDataModified, bool& bSafe);
    void __stdcall OnActionsClick(ButtonValue* sender, bool& bDataModified, bool& bSafe);
    bool __stdcall OnAfterActionNameEdit(PropValue* sender, shared_str& edit_val);
    void FillProp(LPCSTR pref, ::PropItemVec& items, ::ListItem* owner);
    void Copy(const CPEDef& src);
    BOOL Equal(const CPEDef* pe);
    void Render(const Fmatrix& parent);
    static PFunction* FindCommandPrototype(LPCSTR src, LPCSTR& dest);
    void __stdcall FillActionList(ChooseItemVec& items, void* param);
    bool Validate(bool bMsg);
    void Compile(EPAVec& v);
#endif
};
}; // namespace PS

#define PED_VERSION 0x0001
#define PED_CHUNK_VERSION 0x0001
#define PED_CHUNK_NAME 0x0002
#define PED_CHUNK_EFFECTDATA 0x0003
#define PED_CHUNK_ACTIONLIST 0x0004
#define PED_CHUNK_FLAGS 0x0005
#define PED_CHUNK_FRAME 0x0006
#define PED_CHUNK_SPRITE 0x0007
#define PED_CHUNK_TIMELIMIT 0x0008
#define PED_CHUNK_TIMELIMIT2 0x0009
#define PED_CHUNK_SOURCETEXT_ 0x0020 // obsolete
#define PED_CHUNK_COLLISION 0x0021
#define PED_CHUNK_VEL_SCALE 0x0022
#define PED_CHUNK_EDATA 0x0024
#define PED_CHUNK_ALIGN_TO_PATH 0x0025
//---------------------------------------------------------------------------
#endif