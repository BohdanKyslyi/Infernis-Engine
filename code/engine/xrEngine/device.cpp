#include "stdafx.h"
#include "../xrCDB/frustum.h"

#pragma warning(disable : 4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
// d3dx9.h
#include <d3dx9.h>
#pragma warning(default : 4995)

#include "x_ray.h"
#include "render.h"

#ifdef INGAME_EDITOR
#include "editor/ide.hpp"
#include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "igame_persistent.h"
#include "shader_bus.h"
#include "ssfx_console_vars.h" // ps_ssfx_grass_interactive - previous-frame grass benders
#include <atomic>              // item 9 stage C: g_frame_drawing, frame_stat_thread
// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the frame parts, the spatial timers, the
// process memory
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини кадру, таймери простору, пам'ять
// процесу
#include "frame_parts.h"
#include "../xrcdb/ispatial.h"
#include <psapi.h>
// Edited by Ivan Danko, 2026-11-18: item 9 stage F - the task system's line for "Test mode"
// Змінено (Ivan Danko, 2026-11-18): пункт 9 етап F - рядок системи задач для "Режиму тесту"
#include "../xrCore/xrTasks.h"

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;

ENGINE_API float IE_VIEWPORT_NEAR; // 0.05f - для юзу сучасних збройових паків, 0.2f - для ванільних

ENGINE_API BOOL g_bRendering = FALSE;

BOOL g_bLoaded = FALSE;
ref_light precache_light = 0;

// Item 9 stage C1 ( multithreading ): the frame limiter ( frame_limit_start, on_idle ).
// "r__framelimit N" holds the game at N frames a second ( 0 = no limit ); "r__framelimit_menu 1"
// holds the main menu and the pause at the refresh rate of the monitor the game window is on while
// "r__framelimit" is 0. The wait comes before the input and the update, so the frame shown is the
// freshest one. A high resolution waitable timer sleeps for most of the wait ( Windows 10 1803+,
// else the plain one ), the last 1 ms ( 2 ms ) is a spin. Loading and the precache are never held.
// Our own code; the menu limit follows the idea of the donor's ECO_RENDER ( device.cpp:494-517 ).
int ps_r_framelimit = 0;
int ps_r_framelimit_menu = 1;

// Item 9 stage C3 ( multithreading ): the Lua garbage collector works on the secondary thread while
// the main thread draws the frame ( lua_parallel_gc_run, mt_Thread ): "lua_parallel_gcstep" a step,
// the first step always, the next ones while the drawing goes on, "lua_parallel_gc_call_amount" at
// most; the level's own step ( CLevel::script_gc, xrGame\Level.cpp ) stands down while it is on.
// The level gives the step of its Lua state through g_lua_parallel_gc ( 1 = a cycle ended ).
// Our own code; the donor's example: EngineThreading.cpp:171-189, xrGame\Level.cpp:1223-1265.
ENGINE_API BOOL ps_lua_parallel_gc = TRUE;
int ps_lua_parallel_gc_step = 75;
int ps_lua_parallel_gc_call_amount = 25;
ENGINE_API int (*g_lua_parallel_gc)(int step) = nullptr;
static std::atomic<bool> g_frame_drawing{false}; // the main thread draws ( Begin .. End )

// Item 9 stage A ( multithreading ): the frame measurement, "cpu_frame_stats 1" ( xr_ioc_cmd.cpp ).
// Once every 5 s one line in the log: frames a second, the frame time ( average and the longest ),
// the main thread's own work - the game update ( FrameMove ) and the render submission ( Begin ..
// End, without Present ) - the wait in Present, the wait for the secondary thread, and the GPU
// time when the renderer measures it ( DX11 only: xrRenderCommon\dxRenderDeviceRender.cpp, through
// frame_stats_add_gpu ). Nothing is measured while it is off. Our own design, the donor has none.
extern ENGINE_API int ps_cpu_frame_stats;
// Written by Ivan Danko, 2026-11-18: item 9 stage F - "Test mode" ( "cpu_test_mode 1", xr_ioc_cmd.cpp )
// Створено (Ivan Danko, 2026-11-18): пункт 9 етап F - "Режим тесту" ( "cpu_test_mode 1", xr_ioc_cmd.cpp )
extern ENGINE_API int ps_cpu_test_mode;

// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" ( "cpu_frame_stats 1" ) - where the
// frame's time goes, once every 5 s, the average and the longest frame: "* Frame update" ( the parts
// of the game update, frame_parts.h; the physics world and the AI think from the stock timers, which
// run while the measurement is on; the slowest object ), "* Frame render CPU" and "* Frame render
// GPU" ( the render parts, DX11 only: xrRenderCommon\dxRenderDeviceRender.cpp ), "* Second thread"
// ( its busy time and its parts ), "* Memory" ( the game's RAM, the free system RAM, the video memory
// - DX11 on Windows 10+ -, the Lua memory, the allocations a frame, the growth since the measurement
// started ). A long frame gets its parts as well. Nothing is measured while it is off.
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" ( "cpu_frame_stats 1" ) - куди йде час
// кадру, раз на 5 с, середній і найдовший кадр: "* Frame update" ( частини оновлення гри,
// frame_parts.h; світ фізики й мислення ШІ - зі штатних таймерів, які працюють, поки замір
// увімкнено; найповільніший об'єкт ), "* Frame render CPU" і "* Frame render GPU" ( частини рендеру,
// тільки DX11: xrRenderCommon\dxRenderDeviceRender.cpp ), "* Second thread" ( його зайнятість і
// частини ), "* Memory" ( оперативна пам'ять гри, вільна пам'ять системи, відеопам'ять - DX11 на
// Windows 10+ -, пам'ять Lua, виділення пам'яті за кадр, приріст від початку заміру ). Довгий кадр
// теж отримує свої частини. Поки замір вимкнено, нічого не вимірюється.
XRCORE_API void mem_alloc_stats_enable(bool on);
XRCORE_API void mem_alloc_stats_read(u64& self_count, u64& self_bytes, u64& other_count,
                                     u64& other_bytes);
ENGINE_API int (*g_lua_memory_kb)() = nullptr; // the level's Lua memory, KB ( xrGame\Level.cpp )

namespace {
const u32 frame_render_parts_max = 16;
thread_local bool frame_part_second = false; // this thread does the secondary thread's work

// a value over the window: the sum of its frames and the longest frame
struct frame_value {
    double sum = 0, max = 0;
    void add(double v) {
        sum += v;
        if (v > max)
            max = v;
    }
};

struct frame_full_data {
    // this frame: written by the threads ( frame_part_add ), read and cleared on the main thread
    // while the secondary thread waits
    std::atomic<u64> ticks[2][FP_COUNT];
    std::atomic<u32> calls[2][FP_COUNT];
    std::atomic<u64> second_busy;
    // this frame, the main thread only
    double physics_ms = 0, ai_think_ms = 0;
    double part_ms[2][FP_COUNT] = {};
    double render_cpu[frame_render_parts_max] = {};
    bool render_frame = false;
    u64 object_ticks[2] = {};
    string64 object_name[2] = {};
    double alloc_count[2] = {}, alloc_mb[2] = {};
    // the window
    frame_value part[2][FP_COUNT], physics, ai_think, update, update_other;
    double particles_calls = 0;
    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - the grass' blades drawn and draw calls ( DX11 )
    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - намальовані травинки й виклики малювання
    // трави ( DX11 )
    double grass_blades = 0, grass_draws = 0;
    frame_value second_busy_v, second_other;
    frame_value render_cpu_v[frame_render_parts_max], render_total, render_other;
    u32 render_frames = 0, render_parts = 0;
    string64 render_names[frame_render_parts_max] = {};
    frame_value render_gpu_v[frame_render_parts_max], gpu_total, gpu_other;
    u32 gpu_frames = 0, gpu_parts = 0;
    double window_object_ms[2] = {};
    string64 window_object_name[2] = {};
    frame_value alloc_count_v[2];
    double alloc_mb_sum[2] = {};
    u32 alloc_frames = 0;
    // the memory
    bool alloc_on = false;
    u64 alloc_begin[4] = {}; // the allocation counters at the frame's start
    bool vram_known = false, base_known = false, base_vram_known = false, base_lua_known = false;
    u64 vram_used = 0, vram_budget = 0, vram_nonlocal = 0;
    u64 base_private = 0, base_vram = 0;
    int base_lua_kb = 0;
};
frame_full_data frame_full;

double frame_full_ms_per_tick() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return 1000.0 / double(freq.QuadPart);
}

double frame_full_timer_ms(const CStatTimer& t) {
    return std::chrono::duration<double, std::milli>(t.accum).count();
}

// The stock timers ( Stats.cpp ) run while the measurement is on; without "rs_stats" nothing clears
// them, so each frame starts them from zero here ( the list of CStats::Show ).
void frame_full_clear_stock_timers() {
    CStats& s = *Device.Statistic;
    s.EngineTOTAL.FrameStart();
    s.Sheduler.FrameStart();
    s.UpdateClient.FrameStart();
    s.Physics.FrameStart();
    s.ph_collision.FrameStart();
    s.ph_core.FrameStart();
    s.Animation.FrameStart();
    s.AI_Think.FrameStart();
    s.AI_Range.FrameStart();
    s.AI_Path.FrameStart();
    s.AI_Node.FrameStart();
    s.AI_Vis.FrameStart();
    s.AI_Vis_Query.FrameStart();
    s.AI_Vis_RayTests.FrameStart();
    s.RenderTOTAL.FrameStart();
    s.RenderCALC.FrameStart();
    s.RenderCALC_HOM.FrameStart();
    s.RenderDUMP.FrameStart();
    s.RenderDUMP_RT.FrameStart();
    s.RenderDUMP_SKIN.FrameStart();
    s.RenderDUMP_Wait.FrameStart();
    s.RenderDUMP_Wait_S.FrameStart();
    s.RenderDUMP_HUD.FrameStart();
    s.RenderDUMP_Glows.FrameStart();
    s.RenderDUMP_Lights.FrameStart();
    s.RenderDUMP_WM.FrameStart();
    s.RenderDUMP_DT_VIS.FrameStart();
    s.RenderDUMP_DT_Render.FrameStart();
    s.RenderDUMP_DT_Cache.FrameStart();
    s.RenderDUMP_Pcalc.FrameStart();
    s.RenderDUMP_Scalc.FrameStart();
    s.RenderDUMP_Srender.FrameStart();
    s.Sound.FrameStart();
    s.Input.FrameStart();
    s.clRAY.FrameStart();
    s.clBOX.FrameStart();
    s.clFRUSTUM.FrameStart();
    s.netClient1.FrameStart();
    s.netClient2.FrameStart();
    s.netServer.FrameStart();
    s.netClientCompressor.FrameStart();
    s.netServerCompressor.FrameStart();
    s.TEST0.FrameStart();
    s.TEST1.FrameStart();
    s.TEST2.FrameStart();
    s.TEST3.FrameStart();
    if (g_SpatialSpace) {
        g_SpatialSpace->stat_insert.FrameStart();
        g_SpatialSpace->stat_remove.FrameStart();
    }
    if (g_SpatialSpacePhysic) {
        g_SpatialSpacePhysic->stat_insert.FrameStart();
        g_SpatialSpacePhysic->stat_remove.FrameStart();
    }
}

// the game's own memory ( Windows 7+ )
bool frame_full_process_memory(PROCESS_MEMORY_COUNTERS_EX& pmc) {
    using get_info_fn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    static const get_info_fn get_info = reinterpret_cast<get_info_fn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo"));
    pmc = {};
    pmc.cb = sizeof(pmc);
    return get_info &&
           get_info(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&pmc),
                    sizeof(pmc));
}

// the level's Lua memory, while there is a level
bool frame_full_lua_kb(int& kb) {
    int (*const lua_kb)() = g_lua_memory_kb;
    if (!lua_kb)
        return false;
    kb = lua_kb();
    return true;
}

// A frame starts ( frame_stat_begin_frame, the secondary thread waits ): this frame's values from
// zero.
void frame_full_begin_frame() {
    frame_full_data& f = frame_full;
    for (u32 slot = 0; slot < 2; ++slot)
        for (u32 p = 0; p < FP_COUNT; ++p) {
            f.ticks[slot][p].store(0, std::memory_order_relaxed);
            f.calls[slot][p].store(0, std::memory_order_relaxed);
        }
    f.second_busy.store(0, std::memory_order_relaxed);
    f.physics_ms = f.ai_think_ms = 0;
    f.render_frame = false;
    f.object_ticks[0] = f.object_ticks[1] = 0;
    f.object_name[0][0] = f.object_name[1][0] = 0;
    if (!psDeviceFlags.test(rsStatistic))
        frame_full_clear_stock_timers();
    if (!f.alloc_on) {
        mem_alloc_stats_enable(true);
        f.alloc_on = true;
    }
    mem_alloc_stats_read(f.alloc_begin[0], f.alloc_begin[1], f.alloc_begin[2], f.alloc_begin[3]);
    if (!f.base_known) { // the measurement starts: the memory the growth is counted from
        f.base_known = true;
        PROCESS_MEMORY_COUNTERS_EX pmc;
        f.base_private = frame_full_process_memory(pmc) ? u64(pmc.PrivateUsage) : 0;
        f.base_lua_known = frame_full_lua_kb(f.base_lua_kb);
    }
}

// The measurement is off ( frame_stat_begin_frame ).
void frame_full_off() {
    frame_full_data& f = frame_full;
    if (f.alloc_on) {
        mem_alloc_stats_enable(false);
        f.alloc_on = false;
    }
    f.base_known = f.base_vram_known = f.base_lua_known = f.vram_known = false;
}

// The update is done ( on_idle, after FrameMove ): this frame's physics and AI think from the stock
// timers.
void frame_full_after_update() {
    frame_full.physics_ms = frame_full_timer_ms(Device.Statistic->Physics);
    frame_full.ai_think_ms = frame_full_timer_ms(Device.Statistic->AI_Think);
}

// The window starts again ( frame_stat_window ).
void frame_full_window_reset() {
    frame_full_data& f = frame_full;
    for (u32 slot = 0; slot < 2; ++slot)
        for (u32 p = 0; p < FP_COUNT; ++p)
            f.part[slot][p] = frame_value();
    f.physics = f.ai_think = f.update = f.update_other = frame_value();
    f.particles_calls = 0;
    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - the grass counts from zero
    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - лічильники трави з нуля
    f.grass_blades = f.grass_draws = 0;
    f.second_busy_v = f.second_other = frame_value();
    for (u32 i = 0; i < frame_render_parts_max; ++i)
        f.render_cpu_v[i] = f.render_gpu_v[i] = frame_value();
    f.render_total = f.render_other = f.gpu_total = f.gpu_other = frame_value();
    f.render_frames = f.gpu_frames = 0;
    f.window_object_ms[0] = f.window_object_ms[1] = 0;
    f.window_object_name[0][0] = f.window_object_name[1][0] = 0;
    f.alloc_count_v[0] = f.alloc_count_v[1] = frame_value();
    f.alloc_mb_sum[0] = f.alloc_mb_sum[1] = 0;
    f.alloc_frames = 0;
}

// The frame is done ( frame_stat_end_frame, the secondary thread waits ): its values into the window.
void frame_full_collect(double update_ms, double render_ms) {
    frame_full_data& f = frame_full;
    const double k = frame_full_ms_per_tick();
    for (u32 slot = 0; slot < 2; ++slot)
        for (u32 p = 0; p < FP_COUNT; ++p) {
            f.part_ms[slot][p] = double(f.ticks[slot][p].load(std::memory_order_relaxed)) * k;
            f.part[slot][p].add(f.part_ms[slot][p]);
        }
    f.particles_calls += double(f.calls[0][FP_PARTICLES].load(std::memory_order_relaxed)) +
                         double(f.calls[1][FP_PARTICLES].load(std::memory_order_relaxed));
    // Edited by Ivan Danko, 2026-11-18: FPS plan step 3 - the grass: the blades drawn ( the calls of
    // FP_GRASS_PREP ) and the draw calls ( the calls of FP_GRASS_DRAW )
    // Змінено (Ivan Danko, 2026-11-18): крок 3 плану FPS - трава: намальовані травинки ( виклики
    // FP_GRASS_PREP ) і виклики малювання ( виклики FP_GRASS_DRAW )
    f.grass_blades += double(f.calls[0][FP_GRASS_PREP].load(std::memory_order_relaxed)) +
                      double(f.calls[1][FP_GRASS_PREP].load(std::memory_order_relaxed));
    f.grass_draws += double(f.calls[0][FP_GRASS_DRAW].load(std::memory_order_relaxed)) +
                     double(f.calls[1][FP_GRASS_DRAW].load(std::memory_order_relaxed));
    // the update; the physics world is the stock "Physics" without the NPC movement in it
    const double* m = f.part_ms[0];
    const double world = std::max(0.0, f.physics_ms - m[FP_NPC_MOVE]);
    f.physics.add(world);
    f.ai_think.add(f.ai_think_ms);
    f.update.add(update_ms);
    const double top = m[FP_SCHEDULER] + m[FP_OBJECTS] + m[FP_EVENTS] + m[FP_TASKS] +
                       m[FP_LEVEL_SCRIPTS] + m[FP_BULLETS] + m[FP_NET_RECEIVE] + m[FP_SOUND] +
                       m[FP_NET_UPDATE] + m[FP_LUA_GC] + world;
    f.update_other.add(std::max(0.0, update_ms - top));
    // the secondary thread: its busy time without its parts
    const double* s2 = f.part_ms[1];
    const double busy = double(f.second_busy.load(std::memory_order_relaxed)) * k;
    f.second_busy_v.add(busy);
    f.second_other.add(
        std::max(0.0, busy - (s2[FP_ALIFE] + s2[FP_NET_UPDATE] + s2[FP_SOUND] + s2[FP_LUA_GC])));
    // the render parts ( DX11 )
    if (f.render_frame) {
        double sum = 0;
        for (u32 i = 0; i < f.render_parts; ++i) {
            f.render_cpu_v[i].add(f.render_cpu[i]);
            sum += f.render_cpu[i];
        }
        f.render_total.add(render_ms);
        f.render_other.add(std::max(0.0, render_ms - sum));
        f.render_frames++;
    }
    // the slowest objects of the window
    for (u32 kind = 0; kind < 2; ++kind) {
        const double ms = double(f.object_ticks[kind]) * k;
        if (ms > f.window_object_ms[kind]) {
            f.window_object_ms[kind] = ms;
            xr_strcpy(f.window_object_name[kind], f.object_name[kind]);
        }
    }
    // the allocations of this frame ( since frame_full_begin_frame, so a loading screen between two
    // measured frames is not in them ): the main thread's and the other threads'
    u64 a[4];
    mem_alloc_stats_read(a[0], a[1], a[2], a[3]);
    for (u32 i = 0; i < 2; ++i) {
        f.alloc_count[i] = double(a[i * 2] - f.alloc_begin[i * 2]);
        f.alloc_mb[i] = double(a[i * 2 + 1] - f.alloc_begin[i * 2 + 1]) / (1024.0 * 1024.0);
        f.alloc_count_v[i].add(f.alloc_count[i]);
        f.alloc_mb_sum[i] += f.alloc_mb[i];
    }
    f.alloc_frames++;
}

// A long frame ( frame_stat_end_frame ): its own parts.
void frame_full_long_frame() {
    const frame_full_data& f = frame_full;
    const double* m = f.part_ms[0];
    const double* s2 = f.part_ms[1];
    const double k = frame_full_ms_per_tick();
    Msg("~   update: scheduler %.2f (AI think %.2f, object scripts %.2f) | objects %.2f | "
        "spawns+events %.2f | level scripts %.2f | tasks %.2f | physics world %.2f | NPC movement "
        "%.2f | particles %.2f | bullets %.2f | net receive %.2f | second thread: busy %.2f "
        "(A-Life %.2f, net %.2f, sound %.2f, Lua GC %.2f)",
        m[FP_SCHEDULER], f.ai_think_ms, m[FP_OBJECT_SCRIPTS], m[FP_OBJECTS], m[FP_EVENTS],
        m[FP_LEVEL_SCRIPTS], m[FP_TASKS], std::max(0.0, f.physics_ms - m[FP_NPC_MOVE]),
        m[FP_NPC_MOVE], m[FP_PARTICLES], m[FP_BULLETS], m[FP_NET_RECEIVE],
        double(f.second_busy.load(std::memory_order_relaxed)) * k, s2[FP_ALIFE], s2[FP_NET_UPDATE],
        s2[FP_SOUND], s2[FP_LUA_GC]);
    Msg("~   slowest: scheduler %s %.2f ms, objects %s %.2f ms | allocations: main %.0f (%.2f MB), "
        "other threads %.0f (%.2f MB)",
        f.object_name[0][0] ? f.object_name[0] : "-", double(f.object_ticks[0]) * k,
        f.object_name[1][0] ? f.object_name[1] : "-", double(f.object_ticks[1]) * k,
        f.alloc_count[0], f.alloc_mb[0], f.alloc_count[1], f.alloc_mb[1]);
    if (f.render_frame) {
        xr_string line = "~   render CPU:";
        for (u32 i = 0; i < f.render_parts; ++i) {
            string128 text;
            xr_sprintf(text, "%s %s %.2f", i ? " |" : "", f.render_names[i], f.render_cpu[i]);
            line += text;
        }
        Msg("%s", line.c_str());
    }
}

void frame_full_text(xr_string& line, const char* name, const frame_value& v, double n) {
    string256 text;
    xr_sprintf(text, "%s %.2f (%.2f)", name, v.sum / n, v.max);
    line += text;
}

// The memory line ( frame_full_print ).
void frame_full_memory_line() {
    frame_full_data& f = frame_full;
    const double mb = 1.0 / (1024.0 * 1024.0);
    string256 ram = "game RAM n/a", sys = "free system RAM n/a", vram = "VRAM n/a", lua = "Lua n/a";
    PROCESS_MEMORY_COUNTERS_EX pmc;
    const bool ram_known = frame_full_process_memory(pmc);
    if (ram_known)
        xr_sprintf(ram, "game RAM: private %.0f MB (peak %.0f), working set %.0f MB (peak %.0f)",
                   double(pmc.PrivateUsage) * mb, double(pmc.PeakPagefileUsage) * mb,
                   double(pmc.WorkingSetSize) * mb, double(pmc.PeakWorkingSetSize) * mb);
    MEMORYSTATUSEX system = {};
    system.dwLength = sizeof(system);
    if (GlobalMemoryStatusEx(&system))
        xr_sprintf(sys, "free system RAM %.0f of %.0f MB", double(system.ullAvailPhys) * mb,
                   double(system.ullTotalPhys) * mb);
    if (f.vram_known)
        xr_sprintf(vram, "VRAM %.0f of %.0f MB (budget), non-local %.0f MB",
                   double(f.vram_used) * mb, double(f.vram_budget) * mb,
                   double(f.vram_nonlocal) * mb);
    int lua_kb = 0;
    const bool lua_known = frame_full_lua_kb(lua_kb);
    if (lua_known) {
        xr_sprintf(lua, "Lua %.1f MB", double(lua_kb) / 1024.0);
        if (!f.base_lua_known) { // the level came after the measurement started
            f.base_lua_known = true;
            f.base_lua_kb = lua_kb;
        }
    }
    string64 grow_ram = "n/a", grow_lua = "n/a", grow_vram = "n/a";
    if (ram_known && f.base_private)
        xr_sprintf(grow_ram, "%+.0f MB", (double(pmc.PrivateUsage) - double(f.base_private)) * mb);
    if (lua_known && f.base_lua_known)
        xr_sprintf(grow_lua, "%+.1f MB", double(lua_kb - f.base_lua_kb) / 1024.0);
    if (f.vram_known && f.base_vram_known)
        xr_sprintf(grow_vram, "%+.0f MB", (double(f.vram_used) - double(f.base_vram)) * mb);
    const double a = f.alloc_frames ? double(f.alloc_frames) : 1.0;
    Msg("* Memory: %s | %s | %s | %s | allocations a frame: main %.0f (max %.0f), %.2f MB, other "
        "threads %.0f (max %.0f), %.2f MB | growth since the start: RAM %s, Lua %s, VRAM %s",
        ram, sys, vram, lua, f.alloc_count_v[0].sum / a, f.alloc_count_v[0].max,
        f.alloc_mb_sum[0] / a, f.alloc_count_v[1].sum / a, f.alloc_count_v[1].max,
        f.alloc_mb_sum[1] / a, grow_ram, grow_lua, grow_vram);
}

// The window's lines ( frame_stat_end_frame, every 5 s ).
void frame_full_print(double n) {
    frame_full_data& f = frame_full;
    if (n < 1.0)
        return;
    const frame_value* m = f.part[0];
    string256 text;
    xr_string line = "* Frame update [avg (max) ms]: ";
    frame_full_text(line, "total", f.update, n);
    frame_full_text(line, " | scheduler", m[FP_SCHEDULER], n);
    frame_full_text(line, ", AI think in it", f.ai_think, n);
    frame_full_text(line, " | objects", m[FP_OBJECTS], n);
    frame_full_text(line, " | spawns+events", m[FP_EVENTS], n);
    frame_full_text(line, " | scripts: objects", m[FP_OBJECT_SCRIPTS], n);
    frame_full_text(line, ", level", m[FP_LEVEL_SCRIPTS], n);
    frame_full_text(line, ", tasks", m[FP_TASKS], n);
    frame_full_text(line, " | physics world", f.physics, n);
    frame_full_text(line, " | NPC movement", m[FP_NPC_MOVE], n);
    frame_full_text(line, " | particles", m[FP_PARTICLES], n);
    xr_sprintf(text, ", %.0f a frame", f.particles_calls / n);
    line += text;
    frame_full_text(line, " | bullets", m[FP_BULLETS], n);
    frame_full_text(line, " | net receive", m[FP_NET_RECEIVE], n);
    // the parts that run on the main thread only when the secondary thread does not take them
    if (m[FP_SOUND].max > 0)
        frame_full_text(line, " | sound", m[FP_SOUND], n);
    if (m[FP_NET_UPDATE].max > 0)
        frame_full_text(line, " | net update", m[FP_NET_UPDATE], n);
    if (m[FP_ALIFE].max > 0)
        frame_full_text(line, " | A-Life", m[FP_ALIFE], n);
    if (m[FP_LUA_GC].max > 0)
        frame_full_text(line, " | Lua GC", m[FP_LUA_GC], n);
    frame_full_text(line, " | other", f.update_other, n);
    xr_sprintf(text, " | slowest: scheduler %s %.2f ms, objects %s %.2f ms",
               f.window_object_name[0][0] ? f.window_object_name[0] : "-", f.window_object_ms[0],
               f.window_object_name[1][0] ? f.window_object_name[1] : "-", f.window_object_ms[1]);
    line += text;
    Msg("%s", line.c_str());
    if (f.render_frames) { // DX11
        const double r = double(f.render_frames);
        line = "* Frame render CPU [avg (max) ms]: ";
        frame_full_text(line, "total", f.render_total, r);
        for (u32 i = 0; i < f.render_parts; ++i) {
            xr_sprintf(text, " | %s", f.render_names[i]);
            frame_full_text(line, text, f.render_cpu_v[i], r);
        }
        frame_full_text(line, " | UI + other", f.render_other, r);
        Msg("%s", line.c_str());
    }
    if (f.gpu_frames) { // DX11
        const double g = double(f.gpu_frames);
        line = "* Frame render GPU [avg (max) ms]: ";
        frame_full_text(line, "total", f.gpu_total, g);
        for (u32 i = 0; i < f.gpu_parts; ++i) {
            xr_sprintf(text, " | %s", f.render_names[i]);
            frame_full_text(line, text, f.render_gpu_v[i], g);
        }
        frame_full_text(line, " | UI + other", f.gpu_other, g);
        Msg("%s", line.c_str());
    }
    // Written by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" - the grass line ( DX11:
    // xrRenderCommon\DetailManager.cpp, xrRenderDX10\dx10DetailManager_VS.cpp ): the main thread's
    // grass time a frame, all its passes ( the main view, the 3D scope lens, the shadow maps ) - the
    // wait for the grass calculation, the blades worked out on all cores ( with the blades drawn ),
    // the drawing ( with the draw calls ). With "mt_grass 0" the blades are worked out inside the
    // drawing.
    // Створено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" - рядок трави ( DX11:
    // xrRenderCommon\DetailManager.cpp, xrRenderDX10\dx10DetailManager_VS.cpp ): час трави основного
    // потоку за кадр, усі її проходи ( основний вид, лінза 3D-прицілу, карти тіней ) - очікування
    // розрахунку трави, травинки, пораховані на всіх ядрах ( з числом намальованих травинок ),
    // малювання ( з числом викликів малювання ). З "mt_grass 0" травинки рахуються всередині
    // малювання.
    if (m[FP_GRASS_WAIT].max > 0 || m[FP_GRASS_DRAW].max > 0) {
        line = "* Grass, main thread, all passes [avg (max) ms]: ";
        frame_full_text(line, "calculation wait", m[FP_GRASS_WAIT], n);
        frame_full_text(line, " | blades on all cores", m[FP_GRASS_PREP], n);
        xr_sprintf(text, ", %.0f blades drawn a frame", f.grass_blades / n);
        line += text;
        frame_full_text(line, " | drawing", m[FP_GRASS_DRAW], n);
        xr_sprintf(text, ", %.0f draw calls a frame", f.grass_draws / n);
        line += text;
        Msg("%s", line.c_str());
    }
    const frame_value* s2 = f.part[1];
    line = "* Second thread [avg (max) ms]: ";
    frame_full_text(line, "busy", f.second_busy_v, n);
    frame_full_text(line, " | A-Life", s2[FP_ALIFE], n);
    frame_full_text(line, " | net", s2[FP_NET_UPDATE], n);
    frame_full_text(line, " | sound", s2[FP_SOUND], n);
    frame_full_text(line, " | Lua GC", s2[FP_LUA_GC], n);
    frame_full_text(line, " | other", f.second_other, n);
    Msg("%s", line.c_str());
    frame_full_memory_line();
}
} // namespace

namespace {
using frame_stat_clock = std::chrono::steady_clock;

enum {
    frame_stat_begin,
    frame_stat_updated,
    frame_stat_render,
    frame_stat_rendered,
    frame_stat_present,
    frame_stat_presented,
    frame_stat_waited,
    frame_stat_marks
};

struct frame_stat_data {
    frame_stat_clock::time_point mark[frame_stat_marks];
    frame_stat_clock::time_point window_start, last_begin;
    bool on = false;        // this frame is measured
    bool have_last = false; // last_begin is the start of the frame before
    bool precache = false;  // a precache frame ( the level start ): never a long frame ( stage C4 )
    u32 frames = 0, intervals = 0, gpu_frames = 0;
    u32 long_frames = 0, bones_calls = 0; // item 9 stage C: long frames, measured bone calls
    double frame_ms = 0, frame_max = 0, update_ms = 0, render_ms = 0, present_ms = 0, wait_ms = 0,
           gpu_ms = 0;
    double bones_ms = 0, bones_render_ms = 0; // main thread's CalculateBones ( SkeletonRigid.cpp )
};
frame_stat_data frame_stat;
std::atomic<u32> frame_stat_thread{0}; // the main thread: only its bone calls are measured

double frame_stat_ms(const frame_stat_clock::time_point& from,
                     const frame_stat_clock::time_point& to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

void frame_stat_mark(int m) {
    if (frame_stat.on)
        frame_stat.mark[m] = frame_stat_clock::now();
}

void frame_stat_window(const frame_stat_clock::time_point& now) {
    frame_stat.window_start = now;
    frame_stat.frames = frame_stat.intervals = frame_stat.gpu_frames = 0;
    frame_stat.frame_ms = frame_stat.frame_max = frame_stat.update_ms = frame_stat.render_ms = 0;
    frame_stat.present_ms = frame_stat.wait_ms = frame_stat.gpu_ms = 0;
    frame_stat.long_frames = frame_stat.bones_calls = 0; // item 9 stage C
    frame_stat.bones_ms = frame_stat.bones_render_ms = 0;
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - its window starts again too
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - його вікно теж починається знову
    frame_full_window_reset();
}

// A frame starts its own work ( on_idle, before FrameMove ).
void frame_stat_begin_frame() {
    frame_stat.on = ps_cpu_frame_stats != 0;
    if (!frame_stat.on) {
        frame_stat.have_last = false;
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - off as well
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - теж вимикається
        frame_full_off();
        return;
    }
    const frame_stat_clock::time_point now = frame_stat_clock::now();
    if (!frame_stat.have_last)
        frame_stat_window(now);
    // a frame that renders nothing ( the window not active ) keeps its render and Present at zero
    for (frame_stat_clock::time_point& m : frame_stat.mark)
        m = now;
    frame_stat.precache = Device.dwPrecacheFrame != 0; // item 9 stage C4
    frame_stat_thread.store(GetCurrentThreadId(), std::memory_order_relaxed); // the bone measurement
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this frame's parts from zero
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини цього кадру з нуля
    frame_full_begin_frame();
}

// A loading frame ( on_idle, g_loading_events; the load screen still draws through End ): nothing
// of it is measured, and the next frame starts a new window.
void frame_stat_skip_frame() {
    frame_stat.on = false;
    frame_stat.have_last = false;
}

// The frame is done ( on_idle, the secondary thread is back ).
void frame_stat_end_frame() {
    if (!frame_stat.on)
        return;
    frame_stat_data& s = frame_stat;
    const double present = frame_stat_ms(s.mark[frame_stat_present], s.mark[frame_stat_presented]);
    s.update_ms += frame_stat_ms(s.mark[frame_stat_begin], s.mark[frame_stat_updated]);
    s.render_ms += frame_stat_ms(s.mark[frame_stat_render], s.mark[frame_stat_rendered]) - present;
    s.present_ms += present;
    s.wait_ms += frame_stat_ms(s.mark[frame_stat_rendered], s.mark[frame_stat_waited]);
    s.frames++;
    if (s.have_last) {
        const double frame = frame_stat_ms(s.last_begin, s.mark[frame_stat_begin]);
        s.frame_ms += frame;
        s.intervals++;
        if (frame > s.frame_max)
            s.frame_max = frame;
    }
    s.last_begin = s.mark[frame_stat_begin];
    s.have_last = true;
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this frame's parts into the window
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини цього кадру до вікна
    frame_full_collect(frame_stat_ms(s.mark[frame_stat_begin], s.mark[frame_stat_updated]),
                       frame_stat_ms(s.mark[frame_stat_render], s.mark[frame_stat_rendered]) -
                           present);

    const frame_stat_clock::time_point now = frame_stat_clock::now();

    // Item 9 stage C4: a frame whose own work ( begin .. end, the frame limiter's wait is not in it )
    // takes more than 33 ms ( below 30 frames a second ) gets a line of its own: where the time went.
    // The precache frames of a level start are not counted.
    const double own = frame_stat_ms(s.mark[frame_stat_begin], now);
    if (own > 33.0 && !s.precache) {
        const double update = frame_stat_ms(s.mark[frame_stat_begin], s.mark[frame_stat_updated]);
        const double render =
            frame_stat_ms(s.mark[frame_stat_render], s.mark[frame_stat_rendered]) - present;
        const double wait = frame_stat_ms(s.mark[frame_stat_rendered], s.mark[frame_stat_waited]);
        s.long_frames++;
        Msg("~ Long frame %.1f ms: update %.2f | render %.2f | present %.2f | 2nd thread wait %.2f "
            "| other %.2f",
            own, update, render, present, wait, own - update - render - present - wait);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the long frame's own parts
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - власні частини довгого кадру
        frame_full_long_frame();
    }

    const double window = frame_stat_ms(s.window_start, now);
    if (window < 5000.0)
        return;
    const double n = double(s.frames);
    string64 gpu = "n/a";
    if (s.gpu_frames)
        xr_sprintf(gpu, "%.2f ms", s.gpu_ms / double(s.gpu_frames));
    string64 bones = "n/a"; // item 9 stage C: the main thread's bone time a frame ( DX11 only )
    if (s.bones_calls)
        xr_sprintf(bones, "%.2f ms (render %.2f)", s.bones_ms / n, s.bones_render_ms / n);
    Msg("* Frame stats [%.1f s]: FPS %.1f | frame %.2f ms (max %.2f) | main thread %.2f ms "
        "(update %.2f + render %.2f) | bones %s | present %.2f | 2nd thread wait %.2f | GPU %s | "
        "long frames %u",
        window / 1000.0, n * 1000.0 / window, s.intervals ? s.frame_ms / s.intervals : 0.0,
        s.frame_max, (s.update_ms + s.render_ms) / n, s.update_ms / n, s.render_ms / n, bones,
        s.present_ms / n, s.wait_ms / n, gpu, s.long_frames);
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the window's lines of the parts
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - рядки частин за вікно
    frame_full_print(n);
    // Edited by Ivan Danko, 2026-11-18: item 9 stage F - "Test mode" adds the task system's line ( its
    // counters since the last line )
    // Змінено (Ivan Danko, 2026-11-18): пункт 9 етап F - "Режим тесту" додає рядок системи задач ( її
    // лічильники від попереднього рядка )
    if (ps_cpu_test_mode)
        xrTasks::PrintStats();
    frame_stat_window(now);
}
} // namespace

// The GPU time of one frame, from a renderer that measures it ( on the main thread, in End ).
ENGINE_API void frame_stats_add_gpu(float ms) {
    if (!ps_cpu_frame_stats)
        return;
    frame_stat.gpu_ms += ms;
    frame_stat.gpu_frames++;
}

// Item 9 stage C ( measure the bones before moving them, 2026-11-12 ): the main thread's
// CalculateBones time ( xrRenderCommon\SkeletonRigid.cpp, DX11 only ). On only while a frame is
// measured and only on the main thread: the secondary thread's bones are not in its time.
ENGINE_API bool frame_stats_bones_on() {
    return GetCurrentThreadId() == frame_stat_thread.load(std::memory_order_relaxed) &&
           frame_stat.on;
}

ENGINE_API void frame_stats_add_bones(float ms) {
    frame_stat.bones_ms += ms;
    frame_stat.bones_calls++;
    if (g_frame_drawing.load(std::memory_order_relaxed))
        frame_stat.bones_render_ms += ms; // asked for by the render while the frame is drawn
}

// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" - the entry points of the parts
// ( frame_parts.h ), of the render parts and the video memory ( DX11:
// xrRenderCommon\dxRenderDeviceRender.cpp ) and of the menu open time ( xrGame\MainMenu.cpp ).
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - точки входу частин ( frame_parts.h ),
// частин рендеру й відеопам'яті ( DX11: xrRenderCommon\dxRenderDeviceRender.cpp ) і часу відкриття
// меню ( xrGame\MainMenu.cpp ).
ENGINE_API void frame_part_add(u32 part, u64 ticks) {
    if (part >= FP_COUNT)
        return;
    const u32 slot = frame_part_second ? 1 : 0;
    frame_full.ticks[slot][part].fetch_add(ticks, std::memory_order_relaxed);
    frame_full.calls[slot][part].fetch_add(1, std::memory_order_relaxed);
}

// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - a part measured once for many calls
// ( frame_parts.h ): the DX11 render's batch of particle updates adds its whole time on the thread that
// ran the batch and the number of effects it updated.
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - частина, заміряна один раз за багато
// викликів ( frame_parts.h ): пачка оновлень частинок рендера DX11 додає весь свій час у потоці, що її
// запустив, і число оновлених ефектів.
ENGINE_API void frame_part_add_n(u32 part, u64 ticks, u32 calls) {
    if (part >= FP_COUNT)
        return;
    const u32 slot = frame_part_second ? 1 : 0;
    frame_full.ticks[slot][part].fetch_add(ticks, std::memory_order_relaxed);
    frame_full.calls[slot][part].fetch_add(calls, std::memory_order_relaxed);
}

// on the main thread ( the scheduler, the objects' update )
ENGINE_API void frame_stats_object(u32 kind, u64 ticks, const char* name) {
    if (kind > 1 || ticks <= frame_full.object_ticks[kind])
        return;
    frame_full.object_ticks[kind] = ticks;
    strncpy_s(frame_full.object_name[kind], name ? name : "?", _TRUNCATE);
}

// this frame's render parts on the CPU ( in End, before Present )
ENGINE_API void frame_stats_add_render_parts(const float* cpu_ms, const char* const* names,
                                             u32 count) {
    if (!frame_stat.on)
        return;
    frame_full_data& f = frame_full;
    if (count > frame_render_parts_max)
        count = frame_render_parts_max;
    for (u32 i = 0; i < count; ++i) {
        f.render_cpu[i] = double(cpu_ms[i]);
        strncpy_s(f.render_names[i], names[i] ? names[i] : "?", _TRUNCATE);
    }
    f.render_parts = count;
    f.render_frame = true;
}

// an earlier frame's render parts on the GPU, when its queries are read back
ENGINE_API void frame_stats_add_gpu_parts(const float* gpu_ms, u32 count, float frame_ms) {
    if (!ps_cpu_frame_stats)
        return;
    frame_full_data& f = frame_full;
    if (count > frame_render_parts_max)
        count = frame_render_parts_max;
    double sum = 0;
    for (u32 i = 0; i < count; ++i) {
        f.render_gpu_v[i].add(double(gpu_ms[i]));
        sum += double(gpu_ms[i]);
    }
    f.gpu_total.add(double(frame_ms));
    f.gpu_other.add(std::max(0.0, double(frame_ms) - sum));
    f.gpu_frames++;
    f.gpu_parts = count;
}

ENGINE_API void frame_stats_set_vram(u64 used, u64 budget, u64 nonlocal) {
    if (!ps_cpu_frame_stats)
        return;
    frame_full_data& f = frame_full;
    f.vram_used = used;
    f.vram_budget = budget;
    f.vram_nonlocal = nonlocal;
    f.vram_known = true;
    if (!f.base_vram_known) {
        f.base_vram_known = true;
        f.base_vram = used;
    }
}

ENGINE_API void frame_stats_menu_open(u64 ticks, u64 ui_ticks, u64 memory_ticks) {
    const double k = frame_full_ms_per_tick();
    Msg("* Menu open: %.1f ms (UI build %.1f ms, memory report %.1f ms)", double(ticks) * k,
        double(ui_ticks) * k, double(memory_ticks) * k);
}

namespace {
// Item 9 stage C1: the frame limiter ( the comment at ps_r_framelimit ).
struct frame_limit_data {
    LONGLONG freq = 0, next = 0, spin = 0; // QueryPerformanceCounter units; next = the next start
    HANDLE timer = nullptr;
    bool timer_made = false;
    bool monitor_known = false;
    float monitor_hz = 60.f;
    DWORD monitor_read = 0; // timeGetTime of the last read
};
frame_limit_data frame_limit;

// The refresh rate of the monitor the game window is on, read again at most once a second.
float frame_limit_monitor_hz() {
    const DWORD now = timeGetTime();
    if (frame_limit.monitor_known && now - frame_limit.monitor_read < 1000)
        return frame_limit.monitor_hz;
    frame_limit.monitor_known = true;
    frame_limit.monitor_read = now;
    frame_limit.monitor_hz = 60.f; // when Windows can not tell
    MONITORINFOEXA info = {};
    info.cbSize = sizeof(info);
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    const HMONITOR monitor = MonitorFromWindow(Device.m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    if (monitor && GetMonitorInfoA(monitor, &info) &&
        EnumDisplaySettingsA(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1)
        frame_limit.monitor_hz = float(mode.dmDisplayFrequency);
    return frame_limit.monitor_hz;
}

// Holds the frame until 1 / fps after the start of the one before ( fps <= 0: no hold ).
void frame_limit_wait(float fps) {
    if (fps <= 0.f) {
        frame_limit.next = 0;
        return;
    }
    if (!frame_limit.timer_made) {
        frame_limit.timer_made = true;
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        frame_limit.freq = freq.QuadPart;
        // CreateWaitableTimerExW through GetProcAddress: its high resolution flag needs Windows 10
        // 1803+, an older Windows gets the plain timer and a longer spin
        using create_timer_ex = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
        const create_timer_ex create = reinterpret_cast<create_timer_ex>(
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW"));
        if (create)
            frame_limit.timer = create(nullptr, nullptr, 0x00000002 /* HIGH_RESOLUTION */,
                                       TIMER_MODIFY_STATE | SYNCHRONIZE);
        const bool high = frame_limit.timer != nullptr;
        if (!high)
            frame_limit.timer = CreateWaitableTimerA(nullptr, FALSE, nullptr);
        frame_limit.spin = frame_limit.freq / (high ? 1000 : 500); // the last 1 ms ( 2 ms )
        Msg("* Frame limiter: %s waitable timer",
            high ? "high resolution" : (frame_limit.timer ? "plain" : "no"));
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const LONGLONG interval = LONGLONG(double(frame_limit.freq) / double(fps));
    if (!frame_limit.next || now.QuadPart - frame_limit.next > interval) {
        frame_limit.next = now.QuadPart + interval; // the first held frame, or over a frame late
        return;
    }
    const LONGLONG target = frame_limit.next;
    frame_limit.next += interval;
    const LONGLONG left = target - now.QuadPart;
    if (left > frame_limit.spin && frame_limit.timer) {
        LARGE_INTEGER due; // relative, in 100 ns
        due.QuadPart = -((left - frame_limit.spin) * 10000000 / frame_limit.freq);
        if (SetWaitableTimer(frame_limit.timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(frame_limit.timer, INFINITE);
    }
    while (now.QuadPart < target) {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    }
}

// Item 9 stage C3: the Lua collector on the secondary thread ( the comment at ps_lua_parallel_gc ).
void lua_parallel_gc_run() {
    int (*const step)(int) = g_lua_parallel_gc;
    if (!ps_lua_parallel_gc || !step)
        return;
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the collector as a part
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - збирач як частина
    frame_part_scope fp_lua_gc(FP_LUA_GC);
    int calls = 0;
    do {
        if (step(ps_lua_parallel_gc_step) == 1)
            break; // a collection cycle ended
    } while (++calls < ps_lua_parallel_gc_call_amount &&
             g_frame_drawing.load(std::memory_order_relaxed));
}

// The limit of this frame, before its input and update: none while the level is precached, else
// "r__framelimit", else ( while it is 0 ) the monitor's refresh rate in the main menu and the pause
// ( "r__framelimit_menu 1" ).
void frame_limit_start() {
    float fps = 0.f;
    if (!Device.dwPrecacheFrame) {
        if (ps_r_framelimit > 0)
            fps = float(ps_r_framelimit);
        else if (ps_r_framelimit_menu &&
                 (Device.Paused() || (g_pGamePersistent && g_pGamePersistent->m_pMainMenu &&
                                      g_pGamePersistent->m_pMainMenu->IsActive())))
            fps = frame_limit_monitor_hz();
    }
    frame_limit_wait(fps);
}
} // namespace

BOOL CRenderDevice::Begin() {
    /*
    HW.Validate		();
    HRESULT	_hr		= HW.pDevice->TestCooperativeLevel();
if (FAILED(_hr))
    {
            // If the device was lost, do not render until we get it back
            if		(D3DERR_DEVICELOST==_hr)		{
                    Sleep	(33);
                    return	FALSE;
            }

            // Check if the device is ready to be reset
            if		(D3DERR_DEVICENOTRESET==_hr)
            {
                    Reset	();
            }
    }
    */

    switch (m_pRender->GetDeviceState()) {
    case IRenderDeviceRender::dsOK:
        break;

    case IRenderDeviceRender::dsLost:
        // If the device was lost, do not render until we get it back
        Sleep(33);
        return FALSE;

    case IRenderDeviceRender::dsNeedReset:
        // Check if the device is ready to be reset
        Reset();
        break;

    default:
        R_ASSERT(0);
    }

    m_pRender->Begin();

    /*
    CHK_DX					(HW.pDevice->BeginScene());
    RCache.OnFrameBegin		();
    RCache.set_CullMode		(CULL_CW);
    RCache.set_CullMode		(CULL_CCW);
    if (HW.Caps.SceneMode)	overdrawBegin	();
    */

    FPU::m24r();
    g_bRendering = TRUE;
    return TRUE;
}

void CRenderDevice::Clear() { m_pRender->Clear(); }

void CRenderDevice::End(void) {
#ifdef INGAME_EDITOR
    bool load_finished = false;
#endif // #ifdef INGAME_EDITOR
    if (dwPrecacheFrame) {
        ::Sound->set_master_volume(0.f);
        dwPrecacheFrame--;
        //.		pApp->load_draw_internal	();
        if (0 == dwPrecacheFrame) {

#ifdef INGAME_EDITOR
            load_finished = true;
#endif // #ifdef INGAME_EDITOR
            // Gamma.Update		();
            m_pRender->updateGamma();

            if (precache_light)
                precache_light->set_active(false);
            if (precache_light)
                precache_light.destroy();
            ::Sound->set_master_volume(1.f);
            //			pApp->destroy_loading_shaders					();

            m_pRender->ResourcesDestroyNecessaryTextures();
            Memory.mem_compact();
            Msg("* MEMORY USAGE: %d K", Memory.mem_usage() / 1024);
            Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);

            if (g_pGamePersistent->GameType() == 1) // haCk
            {
                WINDOWINFO wi;
                GetWindowInfo(m_hWnd, &wi);
                if (wi.dwWindowStatus != WS_ACTIVECAPTION)
                    Pause(TRUE, TRUE, TRUE, "application start");
            }
        }
    }

    g_bRendering = FALSE;
    // end scene
    //	Present goes here
    frame_stat_mark(frame_stat_present); // item 9 stage A: frame measurement
    m_pRender->End();
    frame_stat_mark(frame_stat_presented);
// RCache.OnFrameEnd	();
// CHK_DX				(HW.pDevice->EndScene());

// HRESULT _hr		= HW.pDevice->Present( NULL, NULL, NULL, NULL );
// if				(D3DERR_DEVICELOST==_hr)	return;			// we will handle this
// later  R_ASSERT2		(SUCCEEDED(_hr),	"Presentation failed. Driver upgrade
// needed?");
#ifdef INGAME_EDITOR
    if (load_finished && m_editor)
        m_editor->on_load_finished();
#endif // #ifdef INGAME_EDITOR
}

volatile u32 mt_Thread_marker = 0x12345678;
void mt_Thread() {
    set_current_thread_name("X-RAY Secondary thread");
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this thread's parts go to the
    // secondary thread's slot
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини цього потоку йдуть до комірки
    // другого потоку
    frame_part_second = true;
    while (true) {
        // waiting for Device permission to execute
        Device.mt_csEnter.lock();

        if (Device.mt_bMustExit) {
            Device.mt_bMustExit = false; // Important!!!
            Device.mt_csEnter.unlock();  // Important!!!
            return;
        }
        // we has granted permission to execute
        mt_Thread_marker = Device.dwFrame;
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the busy time of this frame
        // ( from here to the end of the work )
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - зайнятість у цьому кадрі ( від
        // цього місця до кінця роботи )
        const u64 frame_busy_start = frame_part_begin();

        for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
            Device.seqParallel[pit]();
        Device.seqParallel.clear();
        Device.seqFrameMT.Process(rp_Frame);
        lua_parallel_gc_run(); // item 9 stage C3: the Lua collector while the main thread draws
        if (frame_busy_start)
            frame_full.second_busy.store(frame_part_now() - frame_busy_start,
                                         std::memory_order_relaxed);

        // now we give control to device - signals that we are ended our work
        Device.mt_csEnter.unlock();
        // waits for device signal to continue - to start again
        Device.mt_csLeave.lock();
        // returns sync signal to device
        Device.mt_csLeave.unlock();
    }
}

#include "igame_level.h"
void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input) {
    if (m_pRender->GetForceGPU_REF())
        amount = 0;
    // Msg			("* PCACHE: start for %d...",amount);
    dwPrecacheFrame = dwPrecacheTotal = amount;
    if (amount && !precache_light && g_pGameLevel && g_loading_events.empty()) {
        precache_light = ::Render->light_create();
        precache_light->set_shadow(false);
        precache_light->set_position(vCameraPosition);
        precache_light->set_color(255, 255, 255);
        precache_light->set_range(5.0f);
        precache_light->set_active(true);
    }

    if (amount && b_draw_loadscreen && load_screen_renderer.b_registered == false) {
        load_screen_renderer.start(b_wait_user_input);
    }
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;
void fps_counter_draw(); // the FPS counter ( 2026-11-13, Stats.cpp )

void CRenderDevice::on_idle() {
    if (!b_is_Ready) {
        Sleep(100);
        return;
    }

    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the stock timers also run while
    // "cpu_frame_stats 1" measures ( the physics world and the AI think come from them )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - штатні таймери працюють і тоді, коли
    // вимірює "cpu_frame_stats 1" ( з них беруться світ фізики й мислення ШІ )
    if (psDeviceFlags.test(rsStatistic) || ps_cpu_frame_stats)
        g_bEnableStatGather = TRUE;
    else
        g_bEnableStatGather = FALSE;
    if (g_loading_events.size()) {
        frame_stat_skip_frame(); // item 9 stage A: frame measurement
        frame_limit_wait(0.f);   // item 9 stage C1: loading is never held
        if (g_loading_events.front()())
            g_loading_events.pop_front();
        pApp->LoadDraw();
        return;
    } else {
        frame_limit_start();      // item 9 stage C1: before the input and the update
        frame_stat_begin_frame(); // item 9 stage A: frame measurement
        FrameMove();
        frame_stat_mark(frame_stat_updated);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this frame's physics and AI think
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - фізика й мислення ШІ цього кадру
        if (frame_stat.on)
            frame_full_after_update();
    }

    // Precache
    if (dwPrecacheFrame) {
        float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
        float angle = PI_MUL_2 * factor;
        vCameraDirection.set(std::sin(angle), 0, std::cos(angle));
        vCameraDirection.normalize();
        vCameraTop.set(0, 1, 0);
        vCameraRight.crossproduct(vCameraTop, vCameraDirection);

        mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
    }

    // Matrices
    mFullTransform.mul(mProject, mView);
    m_pRender->SetCacheXform(mView, mProject);

    // SSFX: previous frame data ( Motion vectors )
    mView_prev = mView_saved;
    mProject_prev = mProject_saved;
    m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);

    // Save previous frame grass benders data. The grass shader compares this frame's bend against
    // last frame's to produce motion vectors; without it the previous-frame array stays whatever it
    // was and the blades smear. Slot 0 is the camera. Donor device.cpp:468-478.
    IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;

    GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y,
                          Device.vCameraPosition.z, -1);
    GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);

    for (int pBend = 1; pBend < std::min(16, int(ps_ssfx_grass_interactive.y) + 1); pBend++) {
        GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z,
                                  GData.radius_curr[pBend]);
        GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z,
                                  GData.str[pBend]);
    }

    // Save wind animation position
    wind_anim_prev = wind_anim_saved;
    wind_anim_saved = g_pGamePersistent->Environment().wind_anim;

    // RCache.set_xform_view		( mView				);
    // RCache.set_xform_project	( mProject			);
    D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, nullptr, (D3DXMATRIX*)&mFullTransform);

    vCameraPosition_saved = vCameraPosition;
    mFullTransform_saved = mFullTransform;
    mView_saved = mView;
    mProject_saved = mProject;

    // *** Resume threads
    // Capture end point - thread must run only ONE cycle
    // Release start point - allow thread to run
    g_frame_drawing.store(true, std::memory_order_relaxed); // item 9 stage C3: the drawing starts
    mt_csLeave.lock();
    mt_csEnter.unlock();
    Sleep(0);

    Statistic->RenderTOTAL_Real.FrameStart();
    Statistic->RenderTOTAL_Real.Begin();
    frame_stat_mark(frame_stat_render); // item 9 stage A: frame measurement
    if (b_is_Active) {
        if (Begin()) {

            seqRender.Process(rp_Render);
            if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) ||
                Statistic->errors.size())
                Statistic->Show();
            fps_counter_draw(); // the FPS counter ( 2026-11-13 ): on top of everything
            //	TEST!!!
            // Statistic->RenderTOTAL_Real.End			();
            //	Present goes here
            End();
        }
    }
    g_frame_drawing.store(false, std::memory_order_relaxed); // item 9 stage C3: the drawing ended
    frame_stat_mark(frame_stat_rendered); // item 9 stage A: frame measurement
    Statistic->RenderTOTAL_Real.End();
    Statistic->RenderTOTAL_Real.FrameEnd();
    Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
    // *** Suspend threads
    // Capture startup point
    // Release end point - allow thread to wait for startup point
    mt_csEnter.lock();
    frame_stat_mark(frame_stat_waited); // item 9 stage A: frame measurement
    mt_csLeave.unlock();

    // Ensure, that second thread gets chance to execute anyway
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the secondary thread's work done here
    // goes to its slot ( frame_part_second on and off )
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - робота другого потоку, виконана тут,
    // іде до його комірки ( frame_part_second вмикається й вимикається )
    if (dwFrame != mt_Thread_marker) {
        frame_part_second = true;
        for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
            Device.seqParallel[pit]();
        Device.seqParallel.clear();
        seqFrameMT.Process(rp_Frame);
        lua_parallel_gc_run(); // item 9 stage C3: at least one collector step
        frame_part_second = false;
    }

    frame_stat_end_frame(); // item 9 stage A: frame measurement

    if (!b_is_Active)
        Sleep(1);
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor() {
    m_editor->run();
    m_editor_finalize(m_editor);
    xr_delete(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::message_loop() {
#ifdef INGAME_EDITOR
    if (editor()) {
        message_loop_editor();
        return;
    }
#endif // #ifdef INGAME_EDITOR

    MSG msg;
    PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        on_idle();
    }
}

void CRenderDevice::Run() {
    g_bLoaded = FALSE;
    Log("Starting engine...");
    set_current_thread_name("X-RAY Primary thread");
    // Startup timers and calculate timer delta
    dwTimeGlobal = 0;
    Timer_MM_Delta = 0;
    {
        u32 time_mm = timeGetTime();
        while (timeGetTime() == time_mm)
            ; // wait for next tick
        u32 time_system = timeGetTime();
        u32 time_local = TimerAsync();
        Timer_MM_Delta = time_system - time_local;
    }

    if (pSettings->section_exist("hud_extensions") &&
        pSettings->line_exist("hud_extensions", "viewport_near")) {
        IE_VIEWPORT_NEAR = pSettings->r_float("hud_extensions", "viewport_near");
    }

    // Start all threads
    mt_csEnter.lock();
    mt_bMustExit = false;

    std::thread second_thread(mt_Thread);

    // Message cycle
    seqAppStart.Process(rp_AppStart);

    // CHK_DX(HW.pDevice->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1,0));
    m_pRender->ClearTarget();

    message_loop();

    seqAppEnd.Process(rp_AppEnd);

    // Stop Balance-Thread
    mt_bMustExit = true;
    mt_csEnter.unlock();

    second_thread.join();
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void ProcessLoading(RP_FUNC* f);
void CRenderDevice::FrameMove() {
    dwFrame++;

    Core.dwFrame = dwFrame;

    dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;

    // Keep an unpaused wall-clock delta for frame-driven tools. In particular,
    // debug free flight must remain usable while the simulation timer is paused.
    fTimeDeltaReal = TimerFrame.GetElapsed_sec();
    TimerFrame.Start();
    if (!xr::valid(fTimeDeltaReal) || fTimeDeltaReal < 0.f)
        fTimeDeltaReal = 0.f;
    if (psDeviceFlags.test(rsConstantFPS)) {
        // 20ms = 50fps
        // fTimeDelta		=	0.020f;
        // fTimeGlobal		+=	0.020f;
        // dwTimeDelta		=	20;
        // dwTimeGlobal	+=	20;
        // 33ms = 30fps
        fTimeDelta = 0.033f;
        fTimeGlobal += 0.033f;
        dwTimeDelta = 33;
        dwTimeGlobal += 33;
    } else {
        // Timer
        float fPreviousFrameTime = Timer.GetElapsed_sec();
        Timer.Start(); // previous frame
        fTimeDelta =
            0.1f * fTimeDelta +
            0.9f * fPreviousFrameTime; // smooth random system activity - worst case ~7% error
        // fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime;			// smooth random
        // system activity
		if (fTimeDelta > .1f)
            fTimeDelta = .1f; // limit to 10fps minimum (нижня межа)

        // Never clamp a frame delta up to a positive minimum. Doing so makes
        // simulation time run faster than real time whenever FPS exceeds the
        // corresponding limit. A zero delta is valid: the fixed-step physics
        // accumulator simply waits for measurable wall-clock time.
        if (!xr::valid(fTimeDelta) || fTimeDelta < 0.f)
            fTimeDelta = 0.f;

        if (Paused())
            fTimeDelta = 0.0f;

        //		u64	qTime		= TimerGlobal.GetElapsed_clk();
        fTimeGlobal = TimerGlobal.GetElapsed_sec(); // float(qTime)*CPU::cycles2seconds;
        u32 _old_global = dwTimeGlobal;
        dwTimeGlobal = TimerGlobal.GetElapsed_ms();
        dwTimeDelta = dwTimeGlobal - _old_global;
    }

    // Frame move
    Statistic->EngineTOTAL.Begin();

    ShaderBus::frame_latch();

    //	TODO: HACK to test loading screen.
    // if(!g_bLoaded)
    ProcessLoading(rp_Frame);
    // else
    //	seqFrame.Process			(rp_Frame);
    Statistic->EngineTOTAL.End();
}

void ProcessLoading(RP_FUNC* f) {
    Device.seqFrame.Process(rp_Frame);
    g_bLoaded = TRUE;
}

ENGINE_API BOOL bShowPauseString = TRUE;
#include "IGame_Persistent.h"

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason) {
    static int snd_emitters_ = -1;

    if (g_bBenchmark)
        return;

#ifdef DEBUG
//	Msg("pause [%s] timer=[%s] sound=[%s] reason=%s",bOn?"ON":"OFF", bTimer?"ON":"OFF",
//bSound?"ON":"OFF", reason);
#endif // DEBUG

    if (bOn) {
        if (!Paused())
            bShowPauseString =
#ifdef INGAME_EDITOR
                editor() ? FALSE :
#endif // #ifdef INGAME_EDITOR
#ifdef DEBUG
                         !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
                                                                    TRUE;

        if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused())) {
            g_pauseMngr.Pause(TRUE);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
        }

        if (bSound && ::Sound) {
            snd_emitters_ = ::Sound->pause_emitters(true);
#ifdef DEBUG
//			Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
        }
    } else {
        if (bTimer && /*g_pGamePersistent->CanBePaused() &&*/ g_pauseMngr.Paused()) {
            fTimeDelta = EPS_S + EPS_S;
            g_pauseMngr.Pause(FALSE);
        }

        if (bSound) {
            if (snd_emitters_ > 0) // avoid crash
            {
                snd_emitters_ = ::Sound->pause_emitters(false);
#ifdef DEBUG
//				Log("snd_emitters_[false]",snd_emitters_);
#endif // DEBUG
            } else {
#ifdef DEBUG
                Log("Sound->pause_emitters underflow");
#endif // DEBUG
            }
        }
    }
}

BOOL CRenderDevice::Paused() { return g_pauseMngr.Paused(); };

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam) {
    u16 fActive = LOWORD(wParam);
    BOOL fMinimized = (BOOL)HIWORD(wParam);
    BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;

    if (bActive != Device.b_is_Active) {
        Device.b_is_Active = bActive;

        if (Device.b_is_Active) {
            Device.seqAppActivate.Process(rp_AppActivate);
            app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;

#ifdef INGAME_EDITOR
            if (!editor())
#endif // #ifdef INGAME_EDITOR
                ShowCursor(FALSE);
        } else {
            app_inactive_time_start = TimerMM.GetElapsed_ms();
            Device.seqAppDeactivate.Process(rp_AppDeactivate);
            ShowCursor(TRUE);
        }
    }
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt) {
    if (mt)
        seqFrameMT.Add(f, REG_PRIORITY_HIGH);
    else
        seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f) {
    seqFrameMT.Remove(f);
    seqFrame.Remove(f);
}

CLoadScreenRenderer::CLoadScreenRenderer() : b_registered(false) {}

void CLoadScreenRenderer::start(bool b_user_input) {
    Device.seqRender.Add(this, 0);
    b_registered = true;
    b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop() {
    if (!b_registered)
        return;
    Device.seqRender.Remove(this);
    pApp->destroy_loading_shaders();
    b_registered = false;
    b_need_user_input = false;
}

void CLoadScreenRenderer::OnRender() { pApp->load_draw_internal(); }
