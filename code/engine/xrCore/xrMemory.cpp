#include "stdafx.h"

#include "xrsharedmem.h"

#include <malloc.h>

xrMemory Memory;
bool shared_str_initialized = false;

// fake fix of memory corruptions in multiplayer game :(
XRCORE_API bool g_allow_heap_min = true;

void xrMemory::_initialize() {
    g_pStringContainer = xr_new<str_container>();
    shared_str_initialized = true;
    g_pSharedMemoryContainer = xr_new<smem_container>();
}

void xrMemory::_destroy() {
    xr_delete(g_pSharedMemoryContainer);
    shared_str_initialized = false;
    xr_delete(g_pStringContainer);
}

void xrMemory::mem_compact() {
    RegFlushKey(HKEY_CLASSES_ROOT);
    RegFlushKey(HKEY_CURRENT_USER);
    if (g_allow_heap_min)
        _heapmin();
    HeapCompact(GetProcessHeap(), 0);
    if (g_pStringContainer)
        g_pStringContainer->clean();
    if (g_pSharedMemoryContainer)
        g_pSharedMemoryContainer->clean();
    if (strstr(Core.Params, "-swap_on_compact"))
        SetProcessWorkingSetSize(GetCurrentProcess(), size_t(-1), size_t(-1));
}

// xr_strdup
char* xr_strdup(const char* string) {
    VERIFY(string);
    u32 len = u32(xr_strlen(string)) + 1;
    char* memory = (char*)xr_malloc(len);
    std::memcpy(memory, string, len);
    return memory;
}

// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" - the allocations a frame for the frame
// measurement ( "* Memory", xrEngine\device.cpp ): a count and the bytes in a slot of each thread's
// own ( 64 slots; the threads past them share one more ), so the counting takes no lock; the
// measurement reads the calling thread's slot and the sum of the others. While it is off, one relaxed
// load a call.
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - виділення пам'яті за кадр для заміру
// кадру ( "* Memory", xrEngine\device.cpp ): кількість і байти в окремій комірці кожного потоку
// ( 64 комірки; потоки понад них ділять ще одну ), тож лічба обходиться без блокування; замір читає
// комірку потоку, що викликає, і суму решти. Поки замір вимкнено - одне просте читання на виклик.
#include <atomic>

namespace {
const u32 mem_alloc_slot_count = 64;

struct alignas(64) mem_alloc_slot {
    std::atomic<u64> count;
    std::atomic<u64> bytes;
};
mem_alloc_slot mem_alloc_slots[mem_alloc_slot_count + 1]; // the last one: the threads past 64
std::atomic<u32> mem_alloc_slot_next{0};
std::atomic<bool> mem_alloc_counting{false};
thread_local u32 mem_alloc_slot_index = u32(-1);

u32 mem_alloc_slot_of_thread() {
    u32 i = mem_alloc_slot_index;
    if (i == u32(-1)) {
        i = mem_alloc_slot_next.fetch_add(1, std::memory_order_relaxed);
        if (i > mem_alloc_slot_count)
            i = mem_alloc_slot_count;
        mem_alloc_slot_index = i;
    }
    return i;
}

void mem_alloc_count(size_t size) {
    if (!mem_alloc_counting.load(std::memory_order_relaxed))
        return;
    const u32 i = mem_alloc_slot_of_thread();
    mem_alloc_slot& s = mem_alloc_slots[i];
    if (i < mem_alloc_slot_count) { // the thread's own slot: no other thread writes it
        s.count.store(s.count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        s.bytes.store(s.bytes.load(std::memory_order_relaxed) + size, std::memory_order_relaxed);
    } else {
        s.count.fetch_add(1, std::memory_order_relaxed);
        s.bytes.fetch_add(size, std::memory_order_relaxed);
    }
}
} // namespace

XRCORE_API void mem_alloc_stats_enable(bool on) {
    mem_alloc_counting.store(on, std::memory_order_relaxed);
}

XRCORE_API void mem_alloc_stats_read(u64& self_count, u64& self_bytes, u64& other_count,
                                     u64& other_bytes) {
    const u32 self = mem_alloc_slot_of_thread();
    self_count = self_bytes = other_count = other_bytes = 0;
    for (u32 i = 0; i <= mem_alloc_slot_count; ++i) {
        const u64 count = mem_alloc_slots[i].count.load(std::memory_order_relaxed);
        const u64 bytes = mem_alloc_slots[i].bytes.load(std::memory_order_relaxed);
        if (i == self && i < mem_alloc_slot_count) {
            self_count = count;
            self_bytes = bytes;
        } else {
            other_count += count;
            other_bytes += bytes;
        }
    }
}

void* xrMemory::mem_alloc(size_t size) {
    void* ptr = malloc(size);
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the allocation is counted
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - виділення рахується
    mem_alloc_count(size);
    return ptr;
}

void xrMemory::mem_free(void* ptr) {
    free(ptr);
}

void* xrMemory::mem_realloc(void* ptr, size_t size) {
    void* result = realloc(ptr, size);
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the allocation is counted
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - виділення рахується
    mem_alloc_count(size);
    return result;
}