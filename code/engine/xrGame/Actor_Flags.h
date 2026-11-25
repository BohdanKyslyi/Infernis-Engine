#pragma once

enum {
    AF_GODMODE = (1 << 0),
    AF_NO_CLIP = (1 << 1),
    AF_UNLIMITEDAMMO = (1 << 3),
    AF_RUN_BACKWARD = (1 << 4),
    AF_AUTOPICKUP = (1 << 5),
    AF_PSP = (1 << 6),
    AF_DYNAMIC_MUSIC = (1 << 7),
    AF_GODMODE_RT = (1 << 8),
    AF_IMPORTANT_SAVE = (1 << 9),
    AF_CROUCH_TOGGLE = (1 << 10),
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - Monolith's bit: the tracers on / off (g_use_tracers)
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - біт з Monolith: увімкнення трасерів (g_use_tracers)
    AF_USE_TRACERS = (1 << 11),
    AF_LOOKOUT_TOGGLE = (1 << 16), // Monolith's bit: the lean keys toggle ( g_lookout_toggle )
};

extern Flags32 psActorFlags;
extern BOOL GodMode();

extern int psActorSleepTime;
