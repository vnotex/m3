#include "failing_allocator.h"
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace {
thread_local bool armed = false, persistent_failure = false;
thread_local size_t budget = 0;
thread_local M3AllocationStats stats{};
void checkpoint() {
    if (!armed) return;
    ++stats.attempts;
    if (budget) { --budget; return; }
    ++stats.failures;
    if (!persistent_failure) armed = false;
    throw std::bad_alloc();
}
void *allocate(size_t size) {
    checkpoint();
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void *allocate_aligned(size_t size, size_t alignment) {
    checkpoint();
#ifdef _WIN32
    if (void *p = _aligned_malloc(size ? size : 1, alignment)) return p;
#else
    void *p = nullptr;
    if (posix_memalign(&p, alignment, size ? size : 1) == 0) return p;
#endif
    throw std::bad_alloc();
}
void release_aligned(void *p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}
extern "C" void m3_test_fail_after(size_t successful_allocations, bool persistent) noexcept {
    budget = successful_allocations;
    persistent_failure = persistent;
    stats = {};
    armed = true;
}
extern "C" M3AllocationStats m3_test_stop_failing() noexcept {
    armed = false;
    return stats;
}
void *operator new(size_t n) { return allocate(n); }
void *operator new[](size_t n) { return allocate(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a) { return allocate_aligned(n, static_cast<size_t>(a)); }
void *operator new[](size_t n, std::align_val_t a) { return allocate_aligned(n, static_cast<size_t>(a)); }
void operator delete(void *p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { release_aligned(p); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    try { return allocate_aligned(n, static_cast<size_t>(a)); } catch (...) { return nullptr; }
}
void *operator new[](size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    try { return allocate_aligned(n, static_cast<size_t>(a)); } catch (...) { return nullptr; }
}
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { release_aligned(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { release_aligned(p); }
