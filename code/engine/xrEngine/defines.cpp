#include "stdafx.h"

#ifdef DEBUG
ECORE_API BOOL bDebug = FALSE;

#endif

// Video
//. u32			psCurrentMode		= 1024;
u32 psCurrentVidMode[2] = { 1024, 768 };
u32 psCurrentBPP = 32;
// ODE collision spaces are not safe to mutate from the main/render thread while
// the physics worker is traversing them. Keep physics on the main frame sequence;
// sound and networking can still use the secondary thread.
Flags32 psDeviceFlags = { rsFullscreen | rsDetails | mtSound | mtNetwork |
                          rsDrawStatic | rsDrawDynamic | rsRefresh60hz };

// textures
int psTextureLOD = 1;
