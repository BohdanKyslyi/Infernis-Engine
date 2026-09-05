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

#ifdef DEBUG
    void OnRender() override;
    void stats();
#endif
};

#endif // HOM_H