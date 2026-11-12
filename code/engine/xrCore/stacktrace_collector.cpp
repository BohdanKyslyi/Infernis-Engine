#include "stdafx.h"
#include "stacktrace_collector.h"

#include <dbghelp.h>

thread_local static void* stack[MaxStackTraceDepth];
thread_local static char symbol[sizeof(SYMBOL_INFO) + (MaxFrameLength + 1) * sizeof(char)];

void BuildStackTrace(StackTraceInfo& stackTrace) {
    static std::mutex dbghelpMutex;
    std::lock_guard<std::mutex> lock(dbghelpMutex);

    const auto processHandle = GetCurrentProcess();
    // logging part C ( 2026-11-10 ): the stack is now taken more than once per run ( each logged C++
    // exception ), so dbghelp is set up once; later calls only add the modules loaded since then
    static bool symInitialized = false;
    if (!symInitialized)
        symInitialized = SymInitialize(processHandle, nullptr, TRUE) != FALSE;
    else
        SymRefreshModuleList(processHandle);

    const auto framesCount = CaptureStackBackTrace(1, MaxStackTraceDepth, stack, nullptr);
    SYMBOL_INFO* symbolInfo = reinterpret_cast<SYMBOL_INFO*>(&symbol);
    symbolInfo->MaxNameLen = MaxFrameLength;
    symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);

    IMAGEHLP_LINE64 lineInfo = { 0 };
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (size_t i = 0; i < framesCount; i++) {
        const auto addr = reinterpret_cast<DWORD64>(stack[i]);
        auto* dst = stackTrace.frames + (MaxFrameLength + 1) * i;
        // logging part C: a frame without a symbol or without a line no longer repeats the name / the file
        // of the frame before it
        if (!SymFromAddr(processHandle, addr, nullptr, symbolInfo)) {
            std::snprintf(dst, MaxFrameLength + 1, "%zi: 0x%016llx", framesCount - i, static_cast<unsigned long long>(addr));
            continue;
        }
        DWORD displacement = 0;
        if (SymGetLineFromAddr64(processHandle, addr, &displacement, &lineInfo))
            std::snprintf(dst, MaxFrameLength + 1, "%zi: %s at %s:%u", framesCount - i, symbolInfo->Name, lineInfo.FileName, lineInfo.LineNumber);
        else
            std::snprintf(dst, MaxFrameLength + 1, "%zi: %s", framesCount - i, symbolInfo->Name);
    }

    stackTrace.count = framesCount;
}
