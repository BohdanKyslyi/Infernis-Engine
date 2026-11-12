#include "stdafx.h"
#include "xrTasks.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Item 9 stage B ( multithreading ): the X-Ray task system - what it does is in xrTasks.h. Our own
// code.

void debug_on_thread_spawn(); // xrDebugNew.cpp: the engine's crash handlers for a new thread

namespace {
using task_clock = std::chrono::steady_clock;

// How long a worker that ran out of tasks, or a thread waiting for its group, keeps looking before
// it sleeps. A frame hands its tasks out in bursts a few microseconds apart, and waking a sleeping
// thread takes about as long as such a task runs, so a short look first saves most of the wake-ups.
// At most two workers look at a time, the others sleep at once: no CPU time burnt for nothing.
const std::chrono::microseconds look_time(20);
const u32 max_lookers = 2;

const u32 max_workers = 63; // an affinity mask holds 64 CPUs, one of them is the calling thread's

struct task_item {
    xrTasks::Task fn;
    xrTasks::Group* group = nullptr;
};

struct task_state {
    std::mutex lock;
    std::condition_variable work; // a worker that takes tasks waits here for one
    std::condition_variable park; // a worker beyond the active ones waits here
    std::condition_variable done; // a thread waits here for its group

    std::deque<task_item> frame;      // the frame's tasks, taken first
    std::deque<task_item> background; // long tasks, never on all the workers at once

    std::vector<std::thread*> threads;
    std::vector<u32> cpu_order; // logical CPUs, the first thread of every physical core first
    std::vector<xrTasks::Group*> thread_groups; // the calling threads' own groups

    u32 created = 0;
    u32 generation = 0;         // a thread's group of this start, not of an earlier one
    u32 background_running = 0; // background tasks on the workers now
    bool stop = false;
    bool broken = false; // the self-test failed: no worker takes tasks

    std::atomic<u32> active{ 0 };  // the workers that take tasks
    std::atomic<u32> queued{ 0 };  // tasks in both queues, for a look without the lock
    std::atomic<u32> started{ 0 }; // workers that reached their loop ( the self-test )
    std::atomic<u32> lookers{ 0 }; // workers looking for tasks without the lock
};

struct task_counters {
    std::atomic<u32> on_workers{ 0 }; // tasks a worker ran
    std::atomic<u32> background{ 0 }; // of those, background tasks
    std::atomic<u32> helped{ 0 };     // tasks the waiting thread ran itself
    std::atomic<u32> inline_run{ 0 }; // tasks run at once on the thread that handed them out
    std::atomic<u32> waits{ 0 };      // waits that found their group not done
    std::atomic<u32> slept{ 0 };      // times a waiting thread went to sleep
};

std::atomic<task_state*> g_state{ nullptr }; // none before Initialize and after Shutdown
std::atomic<bool> g_enabled{ true };          // console cpu_tasks
u32 g_generation = 0;
task_counters g_count;
task_clock::time_point g_count_since;

thread_local xrTasks::Group* tls_group = nullptr; // the calling thread's own group
thread_local u32 tls_group_generation = 0;

u32 mask_count(DWORD_PTR mask) {
    u32 n = 0;
    for (; mask; mask &= mask - 1)
        ++n;
    return n;
}

void reset_counters() {
    g_count.on_workers = 0;
    g_count.background = 0;
    g_count.helped = 0;
    g_count.inline_run = 0;
    g_count.waits = 0;
    g_count.slept = 0;
    g_count_since = task_clock::now();
}

// Every logical CPU of the start mask: the first thread of each physical core first, then the
// second ones, so the first workers get a core each.
void build_cpu_order(task_state& s, DWORD_PTR start_mask) {
    std::vector<DWORD_PTR> cores;
    DWORD size = 0;
    GetLogicalProcessorInformation(nullptr, &size);
    if (size) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(
            size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(info.data(), &size))
            for (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& it : info)
                if (it.Relationship == RelationProcessorCore && (it.ProcessorMask & start_mask))
                    cores.push_back(it.ProcessorMask & start_mask);
    }
    // a core's lowest CPU decides its place
    std::sort(cores.begin(), cores.end(),
              [](DWORD_PTR a, DWORD_PTR b) { return (a & (0 - a)) < (b & (0 - b)); });
    for (u32 pass = 0; pass < 64; ++pass) {
        bool any = false;
        for (DWORD_PTR core : cores) {
            u32 seen = 0;
            for (u32 cpu = 0; cpu < 64; ++cpu) {
                if (!(core & (DWORD_PTR(1) << cpu)))
                    continue;
                if (seen++ == pass) {
                    s.cpu_order.push_back(cpu);
                    any = true;
                    break;
                }
            }
        }
        if (!any)
            break;
    }
    if (s.cpu_order.empty()) // no core information: the CPUs in their own order
        for (u32 cpu = 0; cpu < 64; ++cpu)
            if (start_mask & (DWORD_PTR(1) << cpu))
                s.cpu_order.push_back(cpu);
}

// The workers that take tasks get a CPU each from those in use, in the order above. Only a hint:
// Windows still moves a thread off a busy CPU.
void place_workers(const task_state& s, DWORD_PTR mask, u32 active) {
    std::vector<u32> cpus;
    for (u32 cpu : s.cpu_order)
        if (mask & (DWORD_PTR(1) << cpu))
            cpus.push_back(cpu);
    if (cpus.empty())
        return;
    for (u32 i = 0; i < active && i < u32(s.threads.size()); ++i)
        SetThreadIdealProcessor(HANDLE(s.threads[i]->native_handle()), cpus[i % cpus.size()]);
}

// Background tasks never take more than all the workers that work but one.
u32 background_cap(const task_state& s) {
    const u32 active = s.active.load(std::memory_order_relaxed);
    return active > 1 ? active - 1 : 1;
}

// A worker ( under the lock ): the first frame task, else a background task while the cap allows.
bool take_for_worker(task_state& s, task_item& item, bool& background) {
    if (!s.frame.empty()) {
        item = std::move(s.frame.front());
        s.frame.pop_front();
        background = false;
    } else if (!s.background.empty() && s.background_running < background_cap(s)) {
        item = std::move(s.background.front());
        s.background.pop_front();
        s.background_running++;
        background = true;
    } else
        return false;
    s.queued.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

// A waiting thread ( under the lock ): a task of its own group, from either queue.
bool take_own(task_state& s, const xrTasks::Group* group, task_item& item) {
    std::deque<task_item>* queues[] = { &s.frame, &s.background };
    for (std::deque<task_item>* q : queues)
        for (auto it = q->begin(); it != q->end(); ++it)
            if (it->group == group) {
                item = std::move(*it);
                q->erase(it);
                s.queued.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
    return false;
}

// Runs a task outside the lock. The task and what it holds are gone before its group hears of it.
void run_task(task_item& item) {
    xrTasks::Task fn = std::move(item.fn);
    item.fn = nullptr;
    fn();
}

// A task is done ( under the lock ). Once the counter drops, nothing here touches the group: the
// thread that waited for it may have gone on already.
void finish(task_state& s, xrTasks::Group* group, bool background) {
    if (background) {
        s.background_running--;
        if (!s.background.empty())
            s.work.notify_one();
    }
    if (group->pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
        s.done.notify_all();
}

// The calling thread's own group, made at its first task.
xrTasks::Group& thread_group(task_state& s) {
    if (!tls_group || tls_group_generation != s.generation) {
        xrTasks::Group* group = new xrTasks::Group;
        {
            std::lock_guard<std::mutex> lock(s.lock);
            s.thread_groups.push_back(group);
        }
        tls_group = group;
        tls_group_generation = s.generation;
    }
    return *tls_group;
}

void worker_loop(task_state* s, u32 index) {
    FPU::m24r(); // the engine's rounding, as on every engine thread
    string64 name;
    xr_sprintf(name, "X-Ray Task Worker %u", index + 1);
    set_current_thread_name(name);
    debug_on_thread_spawn();
    s->started.fetch_add(1, std::memory_order_release);

    std::unique_lock<std::mutex> lock(s->lock);
    bool looked = false; // looked for tasks without the lock since the last sleep
    for (;;) {
        if (index >= s->active.load(std::memory_order_relaxed)) {
            if (s->stop)
                return;
            // a wake-up meant for a working worker may have come here: pass it on
            if (s->queued.load(std::memory_order_relaxed))
                s->work.notify_one();
            s->park.wait(lock);
            looked = false;
            continue;
        }
        task_item item;
        bool background = false;
        if (take_for_worker(*s, item, background)) {
            lock.unlock();
            run_task(item);
            g_count.on_workers.fetch_add(1, std::memory_order_relaxed);
            if (background)
                g_count.background.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
            finish(*s, item.group, background);
            looked = false;
            continue;
        }
        if (s->stop)
            return;
        if (!looked) {
            looked = true;
            if (s->lookers.fetch_add(1, std::memory_order_relaxed) < max_lookers) {
                lock.unlock();
                const task_clock::time_point until = task_clock::now() + look_time;
                while (!s->queued.load(std::memory_order_relaxed) && task_clock::now() < until)
                    YieldProcessor();
                s->lookers.fetch_sub(1, std::memory_order_relaxed);
                lock.lock();
                continue;
            }
            s->lookers.fetch_sub(1, std::memory_order_relaxed);
        }
        s->work.wait(lock);
        looked = false;
    }
}

void wait_group(task_state& s, xrTasks::Group& group) {
    g_count.waits.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(s.lock);
    bool looked = false;
    for (;;) {
        if (!group.pending.load(std::memory_order_acquire))
            return;
        task_item item;
        if (take_own(s, &group, item)) {
            lock.unlock();
            run_task(item);
            g_count.helped.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
            finish(s, item.group, false);
            continue;
        }
        if (!looked) {
            looked = true;
            lock.unlock();
            const task_clock::time_point until = task_clock::now() + look_time;
            while (group.pending.load(std::memory_order_acquire) && task_clock::now() < until)
                YieldProcessor();
            lock.lock();
            continue;
        }
        g_count.slept.fetch_add(1, std::memory_order_relaxed);
        s.done.wait(lock);
    }
}

// The workers have to be up, and sums worked out in pieces on the workers have to match the same
// sums worked out in one go, in both queues. Whole numbers: a piece lost, run twice or run over
// another one shows.
bool self_test(task_state& s, u32& on_workers, u32& total) {
    const task_clock::time_point until = task_clock::now() + std::chrono::seconds(2);
    while (s.started.load(std::memory_order_acquire) < s.created) {
        if (task_clock::now() > until)
            return false;
        Sleep(1);
    }
    if (!s.created)
        return true;

    const u32 count = 1 << 16, grain = 256, pieces = count / grain;
    u64 expected = 0;
    for (u32 i = 0; i < count; ++i)
        expected += u64(i) * i % 7919;

    const u32 before = g_count.on_workers.load(std::memory_order_relaxed);
    for (u32 pass = 0; pass < 2; ++pass) {
        std::vector<u64> part(pieces, 0);
        auto piece = [&part, grain](u32 begin, u32 end) {
            u64 sum = 0;
            for (u32 i = begin; i < end; ++i)
                sum += u64(i) * i % 7919;
            part[begin / grain] = sum;
        };
        if (pass == 0)
            xrTasks::ParallelFor(count, grain, piece);
        else {
            xrTasks::Group group;
            for (u32 p = 0; p < pieces; ++p)
                xrTasks::Run(group, [&piece, p, grain] { piece(p * grain, (p + 1) * grain); },
                             xrTasks::Priority::Background);
            xrTasks::Wait(group);
        }
        u64 sum = 0;
        for (u64 v : part)
            sum += v;
        if (sum != expected)
            return false;
        total += pieces;
    }
    on_workers = g_count.on_workers.load(std::memory_order_relaxed) - before;
    return true;
}
} // namespace

namespace xrTasks {
void Initialize() {
    if (g_state.load(std::memory_order_acquire))
        return;

    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) || !process_mask)
        process_mask = 1;
    const u32 cpus = mask_count(process_mask);
    u32 count = cpus > 1 ? cpus - 1 : 0;
    if (count > max_workers)
        count = max_workers;

    task_state* s = new task_state;
    s->generation = ++g_generation;
    build_cpu_order(*s, process_mask);
    s->active.store(count, std::memory_order_relaxed);
    g_state.store(s, std::memory_order_release);

    s->threads.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        try {
            s->threads.push_back(new std::thread(worker_loop, s, i));
        } catch (...) {
            break;
        }
    }
    s->created = u32(s->threads.size());
    if (s->created < count)
        s->active.store(s->created, std::memory_order_relaxed);
    place_workers(*s, process_mask, s->created);

    u32 on_workers = 0, total = 0;
    if (self_test(*s, on_workers, total))
        Msg("* Task system: %u worker threads, self-test OK ( %u of %u test tasks ran on the "
            "workers )",
            s->created, on_workers, total);
    else {
        {
            std::lock_guard<std::mutex> lock(s->lock);
            s->broken = true;
            s->active.store(0, std::memory_order_relaxed);
        }
        s->work.notify_all();
        Msg("! Task system: self-test failed, every task runs on the thread that hands it out");
    }
    reset_counters();
}

void Shutdown() {
    task_state* s = g_state.load(std::memory_order_acquire);
    if (!s)
        return;
    {
        std::lock_guard<std::mutex> lock(s->lock);
        s->stop = true;
    }
    s->work.notify_all();
    s->park.notify_all();
    for (std::thread* t : s->threads) {
        if (t->joinable())
            t->join();
        delete t;
    }
    g_state.store(nullptr, std::memory_order_release);
    for (Group* group : s->thread_groups)
        delete group;
    delete s;
}

void OnCoreMaskChanged(DWORD_PTR mask) {
    task_state* s = g_state.load(std::memory_order_acquire);
    if (!s)
        return;
    const u32 cpus = mask_count(mask);
    u32 active = cpus > 1 ? cpus - 1 : 0;
    if (active > s->created)
        active = s->created;
    {
        std::lock_guard<std::mutex> lock(s->lock);
        if (s->broken)
            active = 0;
        s->active.store(active, std::memory_order_relaxed);
    }
    s->park.notify_all();
    s->work.notify_all();
    place_workers(*s, mask, active);
    Msg("* Task system: %u of %u workers active", active, s->created);
}

void Run(Group& group, Task task, Priority priority) {
    if (!task)
        return;
    task_state* s = g_state.load(std::memory_order_acquire);
    if (!s || !g_enabled.load(std::memory_order_relaxed) ||
        !s->active.load(std::memory_order_relaxed)) {
        task();
        g_count.inline_run.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s->lock);
        group.pending.fetch_add(1, std::memory_order_relaxed);
        task_item item;
        item.fn = std::move(task);
        item.group = &group;
        (priority == Priority::Background ? s->background : s->frame).push_back(std::move(item));
        s->queued.fetch_add(1, std::memory_order_relaxed);
    }
    s->work.notify_one();
}

void Wait(Group& group) {
    if (!group.pending.load(std::memory_order_acquire))
        return;
    task_state* s = g_state.load(std::memory_order_acquire);
    if (s)
        wait_group(*s, group);
}

void ParallelFor(u32 count, u32 grain, const std::function<void(u32, u32)>& body) {
    if (!count || !body)
        return;
    if (!grain)
        grain = 1;
    if (count <= grain || Slots() < 2) {
        body(0, count);
        return;
    }
    Group group;
    for (u32 begin = 0; begin < count; begin += grain) {
        const u32 end = count - begin > grain ? begin + grain : count;
        Run(group, [&body, begin, end] { body(begin, end); });
    }
    Wait(group);
}

void RunInThreadGroup(Task task) {
    task_state* s = g_state.load(std::memory_order_acquire);
    if (!s || !g_enabled.load(std::memory_order_relaxed) ||
        !s->active.load(std::memory_order_relaxed)) {
        if (task) {
            task();
            g_count.inline_run.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    Run(thread_group(*s), std::move(task), Priority::Frame);
}

void WaitThreadGroup() {
    task_state* s = g_state.load(std::memory_order_acquire);
    if (s && tls_group && tls_group_generation == s->generation)
        Wait(*tls_group);
}

u32 Slots() {
    task_state* s = g_state.load(std::memory_order_acquire);
    if (!s || !g_enabled.load(std::memory_order_relaxed))
        return 1;
    return s->active.load(std::memory_order_relaxed) + 1;
}

u32 ActiveWorkers() {
    task_state* s = g_state.load(std::memory_order_acquire);
    return s ? s->active.load(std::memory_order_relaxed) : 0;
}

u32 CreatedWorkers() {
    task_state* s = g_state.load(std::memory_order_acquire);
    return s ? s->created : 0;
}

void SetEnabled(bool on) {
    if (g_enabled.exchange(on) == on)
        return;
    Msg(on ? "* Task system: cpu_tasks 1, the workers take the tasks"
           : "* Task system: cpu_tasks 0, every task runs on the thread that hands it out");
}

void PrintStats() {
    const double seconds =
        std::chrono::duration<double>(task_clock::now() - g_count_since).count();
    const u32 on_workers = g_count.on_workers.exchange(0);
    const u32 background = g_count.background.exchange(0);
    const u32 helped = g_count.helped.exchange(0);
    const u32 inline_run = g_count.inline_run.exchange(0);
    const u32 waits = g_count.waits.exchange(0);
    const u32 slept = g_count.slept.exchange(0);
    g_count_since = task_clock::now();
    Msg("* Task stats [%.1f s]: %u of %u workers active%s | tasks: %u on the workers ( %u "
        "background ), %u by the waiting thread, %u inline | waits %u, slept %u",
        seconds, ActiveWorkers(), CreatedWorkers(), g_enabled.load() ? "" : ", cpu_tasks 0",
        on_workers, background, helped, inline_run, waits, slept);
}
} // namespace xrTasks
