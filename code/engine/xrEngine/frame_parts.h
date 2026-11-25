#pragma once
// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" - the named parts of the frame update
// and of the secondary thread for "cpu_frame_stats 1" ( xrEngine\device.cpp prints them ). A part
// takes its time from the performance counter and adds it to the slot of the thread that ran it - the
// main one or the secondary one; the main thread reads and clears the sums once a frame while the
// secondary thread waits. Nothing is measured while the command is off ( one check a part ).
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - іменовані частини оновлення кадру і
// другого потоку для "cpu_frame_stats 1" ( їх виводить xrEngine\device.cpp ). Частина бере свій час
// із лічильника продуктивності й додає його до комірки потоку, що її виконав, - головного або
// другого; головний потік читає й обнуляє суми раз на кадр, поки другий потік чекає. Поки команду
// вимкнено, нічого не вимірюється ( одна перевірка на частину ).

enum frame_part : u32 {
    FP_SCHEDULER,      // the scheduler ( xrSheduler.cpp )
    FP_OBJECTS,        // the objects' update ( xr_object_list.cpp )
    FP_EVENTS,         // spawns and events ( xrGame\Level.cpp )
    FP_TASKS,          // the tasks ( xrGame\Level.cpp )
    FP_LEVEL_SCRIPTS,  // the level scripts ( xrGame\Level.cpp )
    FP_BULLETS,        // the bullets ( xrGame\Level.cpp )
    FP_NET_RECEIVE,    // the network receive ( xrGame\Level.cpp )
    FP_SOUND,          // the sound ( x_ray.cpp )
    FP_NET_UPDATE,     // the network update ( xrGame\Level_network.cpp )
    FP_ALIFE,          // A-Life ( xrGame\alife_update_manager.cpp )
    FP_OBJECT_SCRIPTS, // the objects' scripts, inside the scheduler ( xrGame\script_binder.cpp )
    FP_NPC_MOVE,       // the NPC movement, inside other parts ( xrGame\movement_manager_physic.cpp )
    FP_PARTICLES,      // the particles, inside other parts and the render ( xrGame\ParticlesObject.cpp )
    FP_LUA_GC,         // the Lua collector ( device.cpp, xrGame\Level.cpp )
    // Written by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" - the DX11 render's grass
    // on the main thread, all its passes ( the main view, the 3D scope lens, the shadow maps )
    // Створено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" - трава рендера DX11
    // в основному потоці, усі її проходи ( основний вид, лінза 3D-прицілу, карти тіней )
    FP_GRASS_WAIT, // the wait for the grass calculation ( xrRenderCommon\DetailManager.cpp )
    FP_GRASS_PREP, // the blades worked out on all cores; calls = the blades drawn ( dx10DetailManager_VS.cpp )
    FP_GRASS_DRAW, // the drawing; calls = the draw calls ( xrRenderDX10\dx10DetailManager_VS.cpp )
    FP_COUNT
};

extern ENGINE_API int ps_cpu_frame_stats;
ENGINE_API void frame_part_add(u32 part, u64 ticks);
// Written by Ivan Danko, 2026-11-17: FPS plan step 2b - a part measured once for many calls ( the DX11
// render's batch of particle updates: its whole time and the number of effects it updated )
// Створено (Ivan Danko, 2026-11-17): крок 2б плану FPS - частина, заміряна один раз за багато викликів
// ( пачка оновлень частинок рендера DX11: увесь її час і число оновлених ефектів )
ENGINE_API void frame_part_add_n(u32 part, u64 ticks, u32 calls);
// the slowest object of the frame: kind 0 = the scheduler, 1 = the objects' update
ENGINE_API void frame_stats_object(u32 kind, u64 ticks, const char* name);
// the main menu opened ( xrGame\MainMenu.cpp ): all of it, the UI build, the memory report
ENGINE_API void frame_stats_menu_open(u64 ticks, u64 ui_ticks, u64 memory_ticks);

inline u64 frame_part_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return u64(t.QuadPart);
}

// 0 while the measurement is off
inline u64 frame_part_begin() { return ps_cpu_frame_stats ? frame_part_now() : 0; }

inline void frame_part_end(u32 part, u64 start) {
    if (start)
        frame_part_add(part, frame_part_now() - start);
}

// the part from here to the end of the block
struct frame_part_scope {
    u64 start;
    u32 part;
    explicit frame_part_scope(u32 p) : start(frame_part_begin()), part(p) {}
    ~frame_part_scope() { frame_part_end(part, start); }
    frame_part_scope(const frame_part_scope&) = delete;
    frame_part_scope& operator=(const frame_part_scope&) = delete;
};
