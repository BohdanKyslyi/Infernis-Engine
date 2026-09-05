#include "stdafx.h"
#include "Blender_rain.h"

CBlender_rain_drops::CBlender_rain_drops() {
    description.CLS = 0;
}

CBlender_rain_drops::~CBlender_rain_drops() {}

void CBlender_rain_drops::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    C.r_Pass("rain_drops", "rain_drops", 
        FALSE,                  // bFog
        TRUE,                   // bZtest
        FALSE,                  // bZwrite
        TRUE,                   // bABlend
        D3DBLEND_SRCALPHA,      // SRC Blend
        D3DBLEND_INVSRCALPHA,   // DST Blend
        FALSE,                  // bABlendTest
        0                       // aTestReference
    );

#if (RENDER == R_R3) || (RENDER == R_R4)
    C.r_dx10Texture("s_base", C.L_textures[0]);
    C.r_dx10Sampler("smp_base");
#else
    C.r_Sampler("s_base", C.L_textures[0]);
#endif

    C.r_End();
}