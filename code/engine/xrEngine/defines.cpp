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
// Edited by Ivan Danko, 2026-11-17: FPS plan step 2b - mtParticles is on by default ( "mt_particles":
// the DX11 render updates the particles in one batch on all cores ).
// «м≥нено (Ivan Danko, 2026-11-17): крок 2б плану FPS - mtParticles ув≥мкнено за замовчуванн€м
// ( "mt_particles": рендер DX11 оновлюЇ частинки одн≥Їю пачкою на вс≥х €драх ).
Flags32 psDeviceFlags = { rsFullscreen | rsDetails | mtSound | mtNetwork |
                          rsDrawStatic | rsDrawDynamic | rsRefresh60hz | mtParticles };

// textures
int psTextureLOD = 1;
