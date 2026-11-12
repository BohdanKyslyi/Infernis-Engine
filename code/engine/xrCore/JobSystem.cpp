#include "stdafx.h"
#include "JobSystem.h"
#include "xrTasks.h"

#include <algorithm>
#include <functional>

// Item 9 stage B ( multithreading ): the old job system had six threads of its own, and nothing in
// the engine used it. Its interface stays and the work goes to the X-Ray task system
// ( xrTasks.cpp ): one group for everything handed out through here, so Wait() waits for all of
// it, as it did.
namespace
{
    xrTasks::Group g_jobs;
} // namespace


namespace Jobs
{
    void Initialize(u32 workerCount)
    {
        // The task system sizes itself ( xrTasks.cpp ), and Core._initialize starts it.
        (void)workerCount;
        xrTasks::Initialize();
    }


    void Shutdown()
    {
        // Core._destroy stops the task system ( xrCore.cpp ).
        Wait();
    }


    void Execute(Job job)
    {
        if (!job)
            return;

        xrTasks::Run(g_jobs, std::move(job));
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
        xrTasks::Wait(g_jobs);
    }


    u32 WorkerCount()
    {
        return xrTasks::ActiveWorkers();
    }
} // namespace Jobs