#ifndef M3_FAILING_ALLOCATOR_H
#define M3_FAILING_ALLOCATOR_H
#include "m3/m3_types.h"
// Controls are exported only by the uninstalled fault-injection test library.
struct M3AllocationStats { size_t attempts, failures; };
extern "C" {
M3_API void m3_test_fail_after(size_t successful_allocations, bool persistent) noexcept;
M3_API M3AllocationStats m3_test_stop_failing() noexcept;
}
#endif
