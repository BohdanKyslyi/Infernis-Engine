#ifndef RainH
#define RainH
#pragma once

#include "../xrcdb/xr_collide_defs.h"
#include "GameMtlLib.h"
#include "WeatherWindMath.h"
#include <immintrin.h>
#include <string_view>

// refs
class ENGINE_API IRender_DetailModel;

#include "xrRender/FactoryPtr.h"
#include "xrRender/RainRender.h"

// === SoA container for raindrops ===
struct RainDropsSoA {
    float* px; float* py; float* pz;
    float* hit_x; float* hit_y; float* hit_z;
    float* dx; float* dy; float* dz;
    float* speed;
    u32* dwTime_Life;
    u32* dwTime_Hit;
    u16* material_idx;
    u32* uv_set;

    u32 active_count{0};
    u32 capacity{0};

    RainDropsSoA(u32 max_items);
    ~RainDropsSoA();

    void invalidate(u32 id) { dwTime_Life[id] = 0; }
    bool is_alive(u32 id, u32 current_time) const { return dwTime_Life[id] >= current_time; }
};

class ENGINE_API CEffect_Rain {
    friend class dxRainRender;

private:
    struct Particle {
        Particle* next{ nullptr };
        Particle* prev{ nullptr };
        Fmatrix mXForm;
        Fsphere bounds;
        float time{ 0.f };
    };
    
    enum States { stIdle = 0, stWorking, stDrippingOut }; 

    // === NOIR ENGINE DYNAMIC RAIN SYSTEM ===
    struct RainHitSound {
        shared_str keyword; 
        ref_sound snd;      
        u32 last_play_time{ 0 };
    };
    xr_vector<RainHitSound> m_HitSounds; 
    
    bool m_bEnableMaterialSounds{ false };        
    bool m_bEnableDynamicWind{ false };

    float m_fWindTiltMultiplier{ 1.f };
    float m_fSavedWindTiltMultiplier{ 1.f };
    bool m_bHasTiltConfig{ false };
    bool m_bHasSpeedConfig{ false };
    bool m_bRefreshTrajectories{ false };
    float m_fSavedWindSpeedMultiplier{ 0.f };
    float m_fWindMaxAngle{ 10.0f };
    float m_fWindSpeedMultiplier{ 0.0f };
    float m_fDripEndTime{ 0.0f };
    float m_fViewRainExposure{ 0.0f };

private:
    FactoryPtr<IRainRender> m_pRender;

    // SoA‑structure
    RainDropsSoA drops; 
    States state{ stIdle };

    xr_vector<Particle> particle_pool;
    Particle* particle_active{ nullptr };
    Particle* particle_idle{ nullptr };

    // === AAA AUDIO SYSTEM ===
    ref_sound snd_Ambient2D;      // Глобальний звук дощу (вулиця)
    ref_sound snd_AmbientPortal;  // Просторове джерело з боку виходу (приміщення)
    
    u32 m_uHitSoundsFrame{ 0 };   // Лічильник відтворених звуків удару за кадр

    void p_create();
    void p_destroy();

    void p_remove(Particle* P, Particle*& LST);
    void p_insert(Particle* P, Particle*& LST);
    int p_size(Particle* LST);
    Particle* p_allocate();
    void p_free(Particle* P);

    void InvalidateDrops();
    // Screen Space Shaders scale the fall speed of a drop - ssfx_rain_1.z, the "Drop speed"
    // slider. Ported from Monolith Rain.cpp:108/139, where it is the third argument of Born and
    // multiplies the random base speed. The default of 1 leaves the vanilla rain exactly as it was.
    void Born(u32 id, float radius, float speed = 1.0f);
    void Hit(Fvector& pos, u16 material_idx); 
    BOOL RayPick(const Fvector& s, const Fvector& d, float& range, collide::rq_target tgt, u16& material_idx); 
    void RenewItem(u32 id, float height, BOOL bHit, u16 material_idx); 

public:
    CEffect_Rain();
    ~CEffect_Rain();

    float GetWindTiltMultiplier() const { return m_fWindTiltMultiplier; }
    void SetWindTiltMultiplier(float value);
    float GetWindSpeedMultiplier() const { return m_fWindSpeedMultiplier; }
    void SetWindSpeedMultiplier(float value);
    void RefreshTrajectories() { m_bRefreshTrajectories = true; }
    bool SaveWindSettings();

    void Render();
    void OnFrame();
    float GetViewRainExposure() const { return m_fViewRainExposure; }
    // Screen Space Shaders: Monolith Rain.h:109-114. Every sound stops when a level unloads
    // ( CGamePersistent::Disconnect ) and the donor's rain sound starts only when the rain does, so
    // a level load and a forced weather change set the rain back to idle ( Rain.cpp ). It does
    // nothing unless SSS runs.
    void InvalidateState();
};

#endif // RainH
