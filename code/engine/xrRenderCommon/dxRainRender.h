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
    ref_geom hGeom_Rain;

    IRender_DetailModel* DM_Drop{ nullptr };
    ref_geom hGeom_Drops;
};

#endif // dxRainRender_included