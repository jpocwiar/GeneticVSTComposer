#pragma once

namespace composertest
{

/// Counts allocations on the current thread. operator new is always observed;
/// on macOS the test also observes direct malloc/realloc through malloc_logger.
class AllocationGuard
{
public:
    AllocationGuard() noexcept;
    ~AllocationGuard();

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    long count() const noexcept;
    long cAllocationCount() const noexcept;
    static bool observesCAllocations() noexcept;

private:
    long start;
    long cStart;
};

} // namespace composertest
