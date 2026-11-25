#ifndef ParticlesObjectH
#define ParticlesObjectH

#include "../xrEngine/PS_instance.h"

extern const Fvector zero_vel;

class CParticlesObject : public CPS_Instance {
    typedef CPS_Instance inherited;

    u32 dwLastTime;
    // Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the frame the DX11 render's update batch last
    // took this effect ( 0 = never; see BatchUpdate )
    // Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - кадр, у якому пачка оновлення рендера DX11
    // востаннє взяла цей ефект ( 0 = ніколи; див. BatchUpdate )
    u32 m_batch_frame;
    void Init(LPCSTR p_name, IRender_Sector* S, BOOL bAutoRemove);
    void UpdateSpatial();

protected:
    bool m_bLooped;   //флаг, что система зациклена
    bool m_bStopping; //вызвана функция Stop()

protected:
    u32 mt_dt;

protected:
    virtual ~CParticlesObject();

public:
    CParticlesObject(LPCSTR p_name, BOOL bAutoRemove, bool destroy_on_game_load);

    virtual bool shedule_Needed() { return true; };
    virtual float shedule_Scale();
    virtual void shedule_Update(u32 dt);
    virtual void renderable_Render();
    void PerformAllTheWork(u32 dt);
    void __stdcall PerformAllTheWork_mt();
    // Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the update the DX11 render runs for this effect
    // in its batch on all cores ( xrRenderPC_R4\r4_R_render.cpp ); true = it ran.
    // Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - оновлення, яке рендер DX11 запускає для
    // цього ефекту у своїй пачці на всіх ядрах ( xrRenderPC_R4\r4_R_render.cpp ); true = виконано.
    bool BatchUpdate();

    Fvector& Position();
    void SetXFORM(const Fmatrix& m);
    IC Fmatrix& XFORM() { return renderable.xform; }
    void UpdateParent(const Fmatrix& m, const Fvector& vel);

    void play_at_pos(const Fvector& pos, BOOL xform = FALSE);
    virtual void Play(bool bHudMode);
    void Stop(BOOL bDefferedStop = TRUE);
    virtual BOOL Locked() { return mt_dt; }

    bool IsLooped() { return m_bLooped; }
    bool IsAutoRemove();
    bool IsPlaying();
    void SetAutoRemove(bool auto_remove);
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - switches the HUD mode of the effect on or off
    // (Monolith ParticlesObject.h; the Lua function set_hud_mode).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - вмикає або вимикає режим HUD ефекту
    // (Monolith ParticlesObject.h; функція Lua set_hud_mode).
    void SetHudMode(bool bHudMode);

    const shared_str Name();

public:
    static CParticlesObject* Create(LPCSTR p_name, BOOL bAutoRemove = TRUE,
                                    bool remove_on_game_load = true) {
        return xr_new<CParticlesObject>(p_name, bAutoRemove, remove_on_game_load);
    }
    static void Destroy(CParticlesObject*& p) {
        if (p) {
            p->PSI_destroy();
            p = 0;
        }
    }
};

#endif /*ParticlesObjectH*/
