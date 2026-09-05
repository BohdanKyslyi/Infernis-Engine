#include "stdafx.h"
#include "JobSystem.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace
{
    using Job = Jobs::Job;

    std::vector<std::thread> g_workers;
    std::queue<Job> g_queue;

    std::mutex g_queueMutex;
    std::condition_variable g_workCV;

    std::mutex g_doneMutex;
    std::condition_variable g_doneCV;

    std::atomic<u32> g_pendingJobs{0};
    std::atomic<bool> g_running{false};

    void WorkerLoop()
    {
        // Кожен worker повинен мати коректний FPU state X-Ray.
        FPU::m24r();
        set_current_thread_name("XR Job Worker");

        for (;;)
        {
            Job job;

            {
                std::unique_lock<std::mutex> lock(g_queueMutex);

                g_workCV.wait(lock, []()
                {
                    return !g_running.load(std::memory_order_acquire) ||
                           !g_queue.empty();
                });

                if (!g_running.load(std::memory_order_acquire) &&
                    g_queue.empty())
                {
                    return;
                }

                job = std::move(g_queue.front());
                g_queue.pop();
            }

            job();

            const u32 previous =
                g_pendingJobs.fetch_sub(1, std::memory_order_acq_rel);

            if (previous == 1)
            {
                g_doneCV.notify_all();
            }
        }
    }
} // namespace


namespace Jobs
{
    void Initialize(u32 workerCount)
    {
        if (g_running.load(std::memory_order_acquire))
            return;

		if (workerCount == 0)
		{
			const u32 hardwareThreads =
				std::thread::hardware_concurrency();

			if (hardwareThreads >= 4)
				workerCount = hardwareThreads / 2;
			else
				workerCount = 1;
		}

        g_pendingJobs.store(0, std::memory_order_release);
        g_running.store(true, std::memory_order_release);

        g_workers.reserve(workerCount);

        for (u32 i = 0; i < workerCount; ++i)
        {
            g_workers.emplace_back(WorkerLoop);
        }
    }


    void Shutdown()
    {
        if (!g_running.load(std::memory_order_acquire))
            return;

        Wait();

        g_running.store(false, std::memory_order_release);

        g_workCV.notify_all();

        for (std::thread& worker : g_workers)
        {
            if (worker.joinable())
                worker.join();
        }

        g_workers.clear();

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);

            while (!g_queue.empty())
            {
                g_queue.pop();
            }
        }

        g_pendingJobs.store(0, std::memory_order_release);
    }


    void Execute(Job job)
    {
        if (!job)
            return;

        if (!g_running.load(std::memory_order_acquire))
        {
            job();
            return;
        }

        g_pendingJobs.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);

            g_queue.push(std::move(job));
        }

        g_workCV.notify_one();
    }
	
	void Dispatch(u32 itemCount, u32 groupSize, DispatchJob job)
	{
		if (!job)
			return;

		if (itemCount == 0)
			return;

		// Захист від випадкового Dispatch(..., 0, ...).
		if (groupSize == 0)
			groupSize = 1;

		for (u32 begin = 0; begin < itemCount; begin += groupSize)
		{
			const u32 end =
				std::min(begin + groupSize, itemCount);

			Execute(
				[job, begin, end]()
				{
					job(begin, end);
				}
			);
		}
	}


    void Wait()
    {
        if (g_pendingJobs.load(std::memory_order_acquire) == 0)
            return;

        std::unique_lock<std::mutex> lock(g_doneMutex);

        g_doneCV.wait(lock, []()
        {
            return g_pendingJobs.load(std::memory_order_acquire) == 0;
        });
    }


    u32 WorkerCount()
    {
        return static_cast<u32>(g_workers.size());
    }
} // namespace Jobs