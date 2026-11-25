//---------------------------------------------------------------------------
#ifndef ParticleCustomH
#define ParticleCustomH

#include "xrRender/ParticleCustom.h"
#include "FBasicVisual.h"
//---------------------------------------------------------------------------
class dxParticleCustom : public dxRender_Visual, public IParticleCustom {
public:
    // geometry-format
    ref_geom geom;
#if RENDER == R_R4
    // Written by Ivan Danko, 2026-11-17: FPS plan step 2b - the frame the DX11 render's update batch last
    // took this effect ( xrRenderPC_R4\r4_R_render.cpp ): each effect is updated at most once a frame.
    // Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - кадр, у якому пачка оновлення рендера DX11
    // востаннє взяла цей ефект ( xrRenderPC_R4\r4_R_render.cpp ): кожен ефект оновлюється щонайбільше
    // раз на кадр.
    u32 ps_batch_frame = 0;
    // Written by Ivan Danko, 2026-11-18: FPS plan step 2b part B - where this effect's vertices lie in the
    // DX11 render's particle vertex buffer ( ParticleEffect.cpp, r4_ps_geom_* ): it is filled once a
    // frame for all the effects at once on all cores, the 3D scope's lens and the main view draw from it.
    // ps_geom_stamp - the buffer's stamp at the fill, 0 = not filled ( every update and move clear it );
    // ps_geom_offset / ps_geom_count - the first vertex and the number of vertices; ps_geom_queued - the
    // frame in which the effect waits in the fill queue.
    // Створено (Ivan Danko, 2026-11-18): крок 2б плану FPS, частина B - де лежать вершини цього ефекту
    // в буфері вершин частинок рендера DX11 ( ParticleEffect.cpp, r4_ps_geom_* ): він заповнюється раз
    // на кадр для всіх ефектів разом на всіх ядрах, лінза 3D-прицілу й основний вид малюють з нього.
    // ps_geom_stamp - позначка буфера під час заповнення, 0 = не заповнено ( кожне оновлення й
    // переміщення скидають її ); ps_geom_offset / ps_geom_count - перша вершина й кількість вершин;
    // ps_geom_queued - кадр, у якому ефект чекає в черзі на заповнення.
    u32 ps_geom_stamp = 0;
    u32 ps_geom_offset = 0;
    u32 ps_geom_count = 0;
    u32 ps_geom_queued = 0;
#endif

public:
    virtual ~dxParticleCustom() { ; }

    virtual IParticleCustom* dcast_ParticleCustom() { return this; }
};

//---------------------------------------------------------------------------
#endif // ParticleCustomH
