#ifndef dxRainRender_included
#define dxRainRender_included
#pragma once

#include "xrRender\RainRender.h"

class dxRainRender : public IRainRender {
public:
    dxRainRender();
    virtual ~dxRainRender() override;
    
    virtual void Copy(IRainRender& _in) override;
    virtual void Render(CEffect_Rain& owner) override;

    [[nodiscard]] virtual const Fsphere& GetDropBounds() const override;

private:
    ref_shader SH_Rain;
    // Screen Space Shaders draw the splash on the ground with a shader of their own. Ported from
    // Monolith dxRainRender.h / dxRainRender.cpp:42. Left empty unless the addon is installed.
    ref_shader SH_Splash;
    ref_geom hGeom_Rain;

    IRender_DetailModel* DM_Drop{ nullptr };
    ref_geom hGeom_Drops;
};

#endif // dxRainRender_included