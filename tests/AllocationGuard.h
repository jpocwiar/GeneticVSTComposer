#pragma once

namespace composertest
{

/// Counts operator new calls made on the current thread while alive.
/// The test executable replaces the global operator new to make this possible.
class AllocationGuard
{
public:
    AllocationGuard() noexcept;
    ~AllocationGuard();

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    long count() const noexcept;

private:
    long start;
};

} // namespace composertest
