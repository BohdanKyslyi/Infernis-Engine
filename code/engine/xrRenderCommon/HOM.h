// HOM.h: interface for the CHOM class.
#pragma once

#ifndef HOM_H
#define HOM_H

#include "xrEngine/IGame_Persistent.h"
#include <atomic>
#include <mutex>

class occTri;

class CHOM
#ifdef DEBUG
    : public pureRender
#endif
{
private:
    xrXRC xrc;
    CDB::MODEL* m_pModel{ nullptr };
    occTri* m_pTris{ nullptr };
    BOOL bEnabled{ FALSE };
    Fmatrix m_xform;
    Fmatrix m_xform_01;

#ifdef DEBUG
    u32 tris_in_frame_visible{ 0 };
    u32 tris_in_frame{ 0 };
#endif

    std::recursive_mutex MT;
    std::atomic<u32> MT_frame_rendered{ 0 }; // C++11 Атомарність замість небезпечного volatile

    void Render_DB(CFrustum& base);

public:
    CHOM();
    ~CHOM();

    void Load();
    void Unload();
    void Render(CFrustum& base);
    void Render_ZB();

    void occlude(Fbox2& space) {}
    void Disable();
    void Enable();

    void __stdcall MT_RENDER();
    
    inline void MT_SYNC() {
        if (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive())
            return;

        MT_RENDER();
    }

    BOOL visible(vis_data& vis);
    BOOL visible(const Fbox3& B);
    BOOL visible(const sPoly& P);
    BOOL visible(const Fbox2& B, float depth); // viewport-space (0..1)
#if RENDER == R_R4
    // Written by Ivan Danko, 2026-11-15: FPS fix step 1 - the frame's buffer is made for the main
    // view. The 3D scope lens view ( the same eye, a narrower view ) tests against it without
    // changing the main view's state, and the transparent pass uses it once more. EnableFrame:
    // wait for this frame's buffer and switch the test on when it is ready. visible_frame: test a
    // world box against this frame's buffer whether the test is on or off ( a box reaching past
    // the buffer's edge counts as visible ). SetLensView: this thread draws the lens view ( one
    // flag per thread: the grass thread keeps its own, always off ).
    // Створено (Ivan Danko, 2026-11-15): крок 1 виправлення FPS - буфер кадру будується для
    // основного виду. Вид лінзи 3D-прицілу ( те саме око, вужчий вид ) перевіряється по ньому, не
    // змінюючи стану основного виду, а прохід прозорого використовує його ще раз. EnableFrame:
    // дочекатися буфера цього кадру й увімкнути перевірку, коли він готовий. visible_frame:
    // перевірити коробку у світі по буферу цього кадру незалежно від того, увімкнено перевірку чи
    // ні ( коробка, що виходить за край буфера, вважається видимою ). SetLensView: цей потік малює
    // вид лінзи ( окремий прапорець для кожного потоку: потік трави має свій, завжди вимкнений ).
    void EnableFrame();
    BOOL visible_frame(const Fbox3& B);
    static void SetLensView(bool on);
#endif

#ifdef DEBUG
    void OnRender() override;
    void stats();
#endif
};

#endif // HOM_H