//---------------------------------------------------------------------------
#ifndef ParticleEffectH
#define ParticleEffectH
#pragma once
//---------------------------------------------------------------------------

#include "ParticleEffectDef.h"

#ifdef _EDITOR
#include "xrRenderCommon/FBasicVisual.h"
#include "xrRenderCommon/dxParticleCustom.h"
#else // _EDITOR
#include "xrRenderCommon/FBasicVisual.h"
#include "xrRenderCommon/dxParticleCustom.h"
#endif // _EDITOR

namespace PS {
class ECORE_API CParticleEffect : public dxParticleCustom {
    friend class CPEDef;

protected:
    float m_fElapsedLimit = 0.0f;
    int m_HandleEffect = -1;
    int m_HandleActionList = -1;
    s32 m_MemDT = 0;
    Fvector m_InitialPosition = {0.0f, 0.0f, 0.0f};

public:
    CPEDef* m_Def = nullptr;
    Fmatrix m_XFORM;

protected:
    DestroyCallback m_DestroyCallback = nullptr;
    CollisionCallback m_CollisionCallback = nullptr;

public:
    enum {
        flRT_Playing = (1 << 0),
        flRT_DefferedStop = (1 << 1),
        flRT_XFORM = (1 << 2),
        flRT_HUDmode = (1 << 3),
    };
    Flags8 m_RT_Flags;

protected:
    BOOL SaveActionList(IWriter& F);
    BOOL LoadActionList(IReader& F);

    void RefreshShader();

public:
    CParticleEffect();
    virtual ~CParticleEffect() override;

    void OnFrame(u32 dt);

    u32 RenderTO();
    virtual void Render(float LOD) override;
    virtual void Copy(dxRender_Visual* pFrom) override;

    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;

    virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) override;

    BOOL Compile(CPEDef* def);

    [[nodiscard]] IC CPEDef* GetDefinition() const { return m_Def; }
    [[nodiscard]] IC int GetHandleEffect() const { return m_HandleEffect; }
    [[nodiscard]] IC int GetHandleActionList() const { return m_HandleActionList; }

    virtual void Play() override;
    virtual void Stop(BOOL bDefferedStop = TRUE) override;
    [[nodiscard]] virtual BOOL IsPlaying() override { return m_RT_Flags.is(flRT_Playing); }

    virtual void SetHudMode(BOOL b) { m_RT_Flags.set(flRT_HUDmode, b); }
    [[nodiscard]] virtual BOOL GetHudMode() { return m_RT_Flags.is(flRT_HUDmode); }

    [[nodiscard]] virtual float GetTimeLimit() override {
        VERIFY(m_Def);
        return m_Def->m_Flags.is(CPEDef::dfTimeLimit) ? m_Def->m_fTimeLimit : -1.0f;
    }

    [[nodiscard]] virtual const shared_str Name() override {
        VERIFY(m_Def);
        return m_Def->m_Name;
    }

    void SetDestroyCB(DestroyCallback destroy_cb) { m_DestroyCallback = destroy_cb; }
    void SetCollisionCB(CollisionCallback collision_cb) { m_CollisionCallback = collision_cb; }
    void SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p);

    [[nodiscard]] virtual u32 ParticlesCount();
};

void OnEffectParticleBirth(void* owner, u32 param, PAPI::Particle& m, u32 idx);
void OnEffectParticleDead(void* owner, u32 param, PAPI::Particle& m, u32 idx);

extern const u32 uDT_STEP;
extern const float fDT_STEP;
} // namespace PS
//---------------------------------------------------------------------------
#endif