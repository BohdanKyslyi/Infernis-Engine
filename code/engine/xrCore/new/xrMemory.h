#pragma once
#include <memory>
#include <utility>
#include <type_traits>

// Стара назва: xrMemory
class XRCORE_API MemoryManager {
public:
    // Стара назва: _initialize
    void Initialize();
    // Стара назва: _destroy
    void Destroy();

    // Стара назва: mem_compact
    void CompactMemory();

    // Стара назва: mem_alloc
    // Додано підтримку вирівнювання (Alignment) для SIMD/AVX інструкцій (за замовчуванням 16 байт під SSE)
    [[nodiscard]] void* Allocate(size_t size, size_t alignment = 16);
    // Стара назва: mem_realloc
    [[nodiscard]] void* Reallocate(void* ptr, size_t size, size_t alignment = 16);
    // Стара назва: mem_free
    void Free(void* ptr);
};

// Стара назва: Memory
extern XRCORE_API MemoryManager GlobalMemory;

// Старі назви: xr_malloc, xr_realloc, xr_free
inline void* EngineMalloc(size_t size) { return GlobalMemory.Allocate(size); }
inline void* EngineRealloc(void* ptr, size_t size) { return GlobalMemory.Reallocate(ptr, size); }
inline void EngineFree(void* ptr) { GlobalMemory.Free(ptr); }

// Сучасна заміна xr_new
template <typename T, typename... Args>
[[nodiscard]] T* EngineNew(Args&&... args) {
    // Вирівнювання пам'яті під конкретний тип для оптимізації кешу
    void* ptr = GlobalMemory.Allocate(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
}

// Сучасна заміна xr_delete
template <typename T>
void EngineDelete(T*& ptr) {
    if (ptr) {
        ptr->~T();
        GlobalMemory.Free(ptr);
        ptr = nullptr;
    }
}