#include "AllocationGuard.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool tracking = false;
std::atomic<long> allocations{0};

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
AllocationGuard::AllocationGuard() noexcept : start(allocations.load()) { tracking = true; }
AllocationGuard::~AllocationGuard() { tracking = false; }
long AllocationGuard::count() const noexcept { return allocations.load() - start; }
} // namespace composertest
