#pragma once

// Item 9 stage B ( multithreading ): the X-Ray task system ( xrTasks.cpp ). Our own code - the
// donors only showed where the engine needs one: Monolith runs its parallel work on Microsoft's
// PPL, Infernis had a small third-party pool with one queue per thread ( xrCPU_Pipe\thread_pool.h
// until this stage ).
//
// One set of worker threads for the whole game: as many as the logical CPUs the game started
// with, minus one for the thread that hands the work out. Only as many take tasks as the CPUs in
// use allow ( cpu_core_mode, CPU::ApplyCoreMode in _math.cpp ); the others wait unused.
//
// A task belongs to a group, and whoever handed the tasks out waits for that group. While it
// waits it runs the tasks of its own group no worker has taken yet - never another group's, so a
// long task of somebody else can not hold a frame up. Two queues: the frame's tasks go first;
// background tasks ( long ones - shader builds, loading: later stages ) never take more than all
// the workers but one, so the frame always finds a worker.
//
// Include after xrCore.h ( u32, DWORD_PTR, XRCORE_API ).

#include <atomic>
#include <functional>

namespace xrTasks {
using Task = std::function<void()>;

enum class Priority { Frame, Background };

// A set of tasks somebody waits for; the counter is the task system's. A group has to live until
// Wait returns for it: after that no worker touches it any more.
struct Group {
    std::atomic<u32> pending{ 0 }; // tasks handed out and not done yet

    Group() = default;
    Group(const Group&) = delete;
    Group& operator=(const Group&) = delete;
};

// Core._initialize and Core._destroy ( xrCore.cpp ). Initialize writes a log line.
XRCORE_API void Initialize();
XRCORE_API void Shutdown();

// The CPUs the game runs on ( CPU::ApplyCoreMode, _math.cpp ): as many workers as these CPUs
// minus one take tasks, the others wait. Writes a log line.
XRCORE_API void OnCoreMaskChanged(DWORD_PTR mask);

// Hands a task to a group. It runs at once on the calling thread when no worker would take it:
// before Initialize, after Shutdown, no CPU to spare, "cpu_tasks 0".
XRCORE_API void Run(Group& group, Task task, Priority priority = Priority::Frame);

// Returns when every task of the group is done; meanwhile runs the group's waiting tasks here.
XRCORE_API void Wait(Group& group);

// body( begin, end ) over [ 0, count ) in pieces of grain, on the workers and on this thread.
// Returns when all of it is done.
XRCORE_API void ParallelFor(u32 count, u32 grain, const std::function<void(u32, u32)>& body);

// The calling thread's own group: for the adapter of the old pool ( xrCPU_Pipe\thread_pool.h ).
XRCORE_API void RunInThreadGroup(Task task);
XRCORE_API void WaitThreadGroup();

// Into how many pieces a caller should cut its work: the workers that take tasks and the calling
// thread, or 1 when every task runs on the calling thread anyway.
XRCORE_API u32 Slots();

XRCORE_API u32 ActiveWorkers();  // the workers that take tasks now
XRCORE_API u32 CreatedWorkers(); // all the worker threads

// The console ( xrEngine\xr_ioc_cmd.cpp ): "cpu_tasks 0/1" and "cpu_task_stats".
XRCORE_API void SetEnabled(bool on);
XRCORE_API void PrintStats();
} // namespace xrTasks
