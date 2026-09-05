#pragma once

#include <functional>

namespace Jobs
{
    using Job = std::function<void()>;

    using DispatchJob =
        std::function<void(u32 begin, u32 end)>;

    XRCORE_API void Initialize(u32 workerCount = 0);

    XRCORE_API void Shutdown();

    XRCORE_API void Execute(Job job);

    XRCORE_API void Dispatch(
        u32 itemCount,
        u32 groupSize,
        DispatchJob job
    );

    XRCORE_API void Wait();

    XRCORE_API u32 WorkerCount();
}