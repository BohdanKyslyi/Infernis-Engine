#include "stdafx.h"
#pragma hdrstop
#include "SH_Constant.h"

void CConstant::Calculate() {
    if (dwFrame == RDEVICE.dwFrame)
        return;
        
    dwFrame = RDEVICE.dwFrame;
    
    if (modeProgrammable == dwMode)
        return;

    const float t = RDEVICE.fTimeGlobal;
    set_float(_R.Calculate(t), _G.Calculate(t), _B.Calculate(t), _A.Calculate(t));
}

void CConstant::Load(IReader* fs) {
    dwMode = modeWaveForm;
    fs->r(&_R, sizeof(WaveForm));
    fs->r(&_G, sizeof(WaveForm));
    fs->r(&_B, sizeof(WaveForm));
    fs->r(&_A, sizeof(WaveForm));
}

void CConstant::Save(IWriter* fs) {
    fs->w(&_R, sizeof(WaveForm));
    fs->w(&_G, sizeof(WaveForm));
    fs->w(&_B, sizeof(WaveForm));
    fs->w(&_A, sizeof(WaveForm));
}