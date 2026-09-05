#include "stdafx.h"
#include "xrsharedmem.h"
#include <malloc.h> // Для _aligned_malloc та _aligned_free

MemoryManager GlobalMemory;
bool g_sharedStrInitialized = false;

// Стара назва: g_allow_heap_min
XRCORE_API bool g_AllowHeapMin = true;

void MemoryManager::Initialize() {
    // Тут в ідеалі g_pStringContainer та g_pSharedMemoryContainer 
    // мають стати std::unique_ptr, щоб уникнути витоків на рівні архітектури.
    g_pStringContainer = EngineNew<str_container>();
    g_sharedStrInitialized = true;
    g_pSharedMemoryContainer = EngineNew<smem_container>();
}

void MemoryManager::Destroy() {
    EngineDelete(g_pSharedMemoryContainer);
    g_sharedStrInitialized = false;
    EngineDelete(g_pStringContainer);
}

void MemoryManager::CompactMemory() {
    RegFlushKey(HKEY_CLASSES_ROOT);
    RegFlushKey(HKEY_CURRENT_USER);
    if (g_AllowHeapMin)
        _heapmin();
    HeapCompact(GetProcessHeap(), 0);
    
    if (g_pStringContainer) g_pStringContainer->clean();
    if (g_pSharedMemoryContainer) g_pSharedMemoryContainer->clean();
}

void* MemoryManager::Allocate(size_t size, size_t alignment) {
    // Використання _aligned_malloc для правильної роботи SIMD/AVX
    return _aligned_malloc(size, alignment);
}

void* MemoryManager::Reallocate(void* ptr, size_t size, size_t alignment) {
    return _aligned_realloc(ptr, size, alignment);
}

void MemoryManager::Free(void* ptr) {
    _aligned_free(ptr);
}