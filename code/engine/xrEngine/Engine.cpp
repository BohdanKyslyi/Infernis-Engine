// Engine.cpp: implementation of the CEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Engine.h"
#include "dedicated_server_only.h"

CEngine Engine;
xrDispatchTable PSGP;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEngine::CEngine() {}

CEngine::~CEngine() {}

extern void msCreate(LPCSTR name);

PROTECT_API void CEngine::Initialize(void) {
    // Bind PSGP
    hPSGP = LoadLibrary("xrCPU_Pipe.dll");
    R_ASSERT(hPSGP);
    xrBinder* bindCPU = (xrBinder*)GetProcAddress(hPSGP, "xrBind_PSGP");
    R_ASSERT(bindCPU);
    bindCPU(&PSGP, &CPU::ID);

    // Other stuff
    Engine.Sheduler.Initialize();
//
#ifdef DEBUG
    msCreate("game");
#endif
}

void CEngine::Destroy() {
    CTimer shutdown_timer;
    shutdown_timer.Start();
    Engine.Sheduler.Destroy();
    Msg("* Shutdown engine: scheduler %u ms", shutdown_timer.GetElapsed_ms());
    Engine.External.Destroy();
    Msg("* Shutdown engine: external modules %u ms", shutdown_timer.GetElapsed_ms());

    if (hPSGP) {
        FreeLibrary(hPSGP);
        Msg("* Shutdown engine: CPU pipe unloaded %u ms", shutdown_timer.GetElapsed_ms());
        hPSGP = 0;
        std::memset(&PSGP, 0, sizeof(PSGP));
    }
}
