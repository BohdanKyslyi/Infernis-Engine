#pragma once

#include "HUDCrosshair.h"
#include "../xrcdb/xr_collide_defs.h"

class CHUDManager;
class CLAItem;

struct SPickParam {
    collide::rq_result RQ;
    float power;
    u32 pass;
};

class CHUDTarget {
private:
    ui_shader hShader;
    ui_shader m_laser_shader;
    bool m_laser_shader_loaded = false;
    void RenderLaserImpact();
    float fuzzyShowInfo;
    SPickParam PP;

    bool m_bShowCrosshair;
    CHUDCrosshair HUDCrosshair;

private:
    collide::rq_results RQR;

public:
    CHUDTarget();
    ~CHUDTarget();
    void CursorOnFrame();
    static float TraceAimDistance(const Fvector& position, const Fvector& direction,
                                  float range, CObject* ignore);
    void Render();
    void Load();
    collide::rq_result& GetRQ() { return PP.RQ; };
    float GetRQVis() { return PP.power; };
    CHUDCrosshair& GetHUDCrosshair() { return HUDCrosshair; }
    void ShowCrosshair(bool b);
    void net_Relcase(CObject* O);
};
