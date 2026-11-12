#include "stdafx.h"

extern xrSkin1W xrSkin1W_x86;
extern xrSkin2W xrSkin2W_x86;
extern xrSkin3W xrSkin3W_x86;
extern xrSkin4W xrSkin4W_x86;

extern xrSkin4W xrSkin4W_thread;

xrSkin4W* skin4W_func = nullptr;

extern xrPLC_calc3 PLC_calc3_x86;
extern xrPLC_calc3 PLC_calc3_SSE;

extern "C" {
__declspec(dllexport) void __cdecl xrBind_PSGP(xrDispatchTable* T, processor_info* ID) {
    // generic
    T->skin1W = xrSkin1W_x86;
    T->skin2W = xrSkin2W_x86;
    T->skin3W = xrSkin3W_x86;
    T->skin4W = xrSkin4W_x86;
    skin4W_func = xrSkin4W_x86;
    T->PLC_calc3 = PLC_calc3_x86;

    // SSE
    if (ID->hasFeature(CpuFeature::Sse)) {
        T->PLC_calc3 = PLC_calc3_SSE;
    }
    
    ttapi.initialize();
    R_ASSERT(ttapi.threads.size());

    // Item 9 stage B ( multithreading ): ttapi has no threads of its own any more, it hands the work
    // to the X-Ray task system ( thread_pool.h, xrCore\xrTasks.cpp ). The pieces follow the workers
    // in use; this is their number at start, before the CPU core mode is applied.
    LogMsg("TTAPI: on the X-Ray task system, {} pieces per job at start", ttapi.threads.size());

    // We can use threading
    T->skin4W = xrSkin4W_thread;
}
};
