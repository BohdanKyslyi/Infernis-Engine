#ifndef _CPS_Instance_H_
#define _CPS_Instance_H_

#include "../xrcdb/ispatial.h"
#include "isheduled.h"
#include "irenderable.h"

class ENGINE_API CPS_Instance : public ISpatial, public ISheduled, public IRenderable {
    friend class IGame_Persistent;

    template <bool _is_pm, typename T>
    friend struct xr_special_free;

private:
    bool m_destroy_on_game_load;

protected:
    int m_iLifeTime;
    BOOL m_bAutoRemove;
    BOOL m_bDead;

protected:
    virtual ~CPS_Instance();
    virtual void PSI_internal_delete();

public:
    CPS_Instance(bool destroy_on_game_load);

    IC const bool& destroy_on_game_load() const { return m_destroy_on_game_load; }
    virtual void PSI_destroy();
    IC BOOL PSI_alive() { return m_iLifeTime > 0; }
    IC BOOL PSI_IsAutomatic() { return m_bAutoRemove; }
    IC void PSI_SetLifeTime(float life_time) { m_iLifeTime = iFloor(life_time * 1000); }

    virtual void Play(bool bHudMode) = 0;
    virtual BOOL Locked() { return FALSE; }

    virtual shared_str shedule_Name() const { return shared_str("particle_instance"); };

    virtual void shedule_Update(u32 dt);
    virtual IRenderable* dcast_Renderable() { return this; }
};

// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the particles' update on all cores. The DX11
// render ( xrRenderPC_R4\r4_R_render.cpp ) collects the particle objects it is going to draw and runs
// their update in one batch through this entry, which the game sets ( xrGame\ParticlesObject.cpp ).
// true = the object's update ran. nullptr = not set: every object updates itself, as before.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - оновлення частинок на всіх ядрах. Рендер DX11
// ( xrRenderPC_R4\r4_R_render.cpp ) збирає об'єкти частинок, які має намалювати, і виконує їхнє
// оновлення однією пачкою через цю точку входу, яку задає гра ( xrGame\ParticlesObject.cpp ).
// true = оновлення об'єкта виконано. nullptr = не задано: кожен об'єкт оновлюється сам, як раніше.
typedef bool ps_batch_update_fn(IRenderable* renderable);
ENGINE_API extern ps_batch_update_fn* g_ps_batch_update;

#endif
