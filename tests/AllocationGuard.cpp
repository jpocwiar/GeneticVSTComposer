#include "AllocationGuard.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#if defined(__APPLE__)
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
#endif

namespace
{
thread_local bool tracking = false;
std::atomic<long> allocations{0};
std::atomic<long> cAllocations{0};

#if defined(__APPLE__)
// Test-process observation only. No interposed allocator and no changes to the
// plugin binary. Keep a pre-existing logger and forward events to it.
using MallocLogger = void (*)(std::uint32_t, std::uintptr_t, std::uintptr_t,
                             std::uintptr_t, std::uintptr_t, std::uint32_t);
MallocLogger previousLogger = nullptr;
pthread_key_t cTrackingKey;
void observeMalloc(std::uint32_t type, std::uintptr_t a, std::uintptr_t b,
                   std::uintptr_t c, std::uintptr_t result, std::uint32_t skip) {
    // pthread_getspecific does not allocate; a C++ thread_local lookup can
    // lazily allocate its Darwin TLS storage and recursively enter this logger.
    if (pthread_getspecific(cTrackingKey) && (type & 2U) != 0) {
        allocations.fetch_add(1, std::memory_order_relaxed);
        cAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (previousLogger) previousLogger(type, a, b, c, result, skip);
}
struct MallocObserver {
    MallocLogger* slot = static_cast<MallocLogger*>(dlsym(RTLD_DEFAULT, "malloc_logger"));
    MallocObserver() {
        if (slot && pthread_key_create(&cTrackingKey, nullptr) == 0) {
            previousLogger = *slot; *slot = observeMalloc;
        } else slot = nullptr;
    }
    ~MallocObserver() { if (slot && *slot == observeMalloc) *slot = previousLogger; }
};
bool observeCAllocator() noexcept { static MallocObserver observer; return observer.slot != nullptr; }
#else
bool observeCAllocator() noexcept { return false; }
#endif

void note() noexcept
{
    if (tracking)
        allocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t size) noexcept
{
    note();
    return std::malloc(size == 0 ? 1 : size);
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) noexcept
{
    note();
    const auto align = std::max(static_cast<std::size_t>(alignment), sizeof(void*));
#if defined(_WIN32)
    return _aligned_malloc(size == 0 ? 1 : size, align);
#else
    void* p = nullptr;
    return posix_memalign(&p, align, size == 0 ? 1 : size) == 0 ? p : nullptr;
#endif
}

void freeAligned(void* p) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocateOrThrow(std::size_t size)
{
    if (void* p = allocate(size))
        return p;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::align_val_t alignment)
{
    if (void* p = allocateAligned(size, alignment))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t a) { return allocateAlignedOrThrow(size, a); }
void* operator new[](std::size_t size, std::align_val_t a) { return allocateAlignedOrThrow(size, a); }
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, a);
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, a);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(p); }

namespace composertest
{
AllocationGuard::AllocationGuard() noexcept {
    tracking = false;
    observeCAllocator();
#if defined(__APPLE__)
    if (observesCAllocations()) pthread_setspecific(cTrackingKey, reinterpret_cast<void*>(1));
#endif
    start = allocations.load(); cStart = cAllocations.load(); tracking = true;
}
AllocationGuard::~AllocationGuard() {
    tracking = false;
#if defined(__APPLE__)
    if (observesCAllocations()) pthread_setspecific(cTrackingKey, nullptr);
#endif
}
long AllocationGuard::count() const noexcept { return allocations.load() - start; }
long AllocationGuard::cAllocationCount() const noexcept { return cAllocations.load() - cStart; }
bool AllocationGuard::observesCAllocations() noexcept { return observeCAllocator(); }
} // namespace composertest
