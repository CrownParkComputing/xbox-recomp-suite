#include <chrono>
#include <mutex>
#include <thread>

#include "xdk/hle.h"

namespace {
std::recursive_mutex g_cri;

void EnterCri() {
    if (!g_cri.try_lock()) {
        kernel::GuestYield yield;
        g_cri.lock();
    }
}
}

// CRI's priority-based Xbox lock is not mutual exclusion on a host. Keep
// the original nesting and thread bookkeeping inside a recursive mutex.
// Server passes must use it too, including when a movie is being destroyed.
X86_EXTERN_FUNC(__imp__sub_00233BD0);
X86_EXTERN_FUNC(__imp__sub_00233C10);
X86_EXTERN_FUNC(__imp__sub_00236BC0);
X86_EXTERN_FUNC(__imp__sub_00236080);
X86_EXTERN_FUNC(__imp__sub_00233CB0);

X86_FUNC(sub_00233BD0) {
    EnterCri();
    __imp__sub_00233BD0(ctx, base);
}
X86_FUNC(sub_00233C10) {
    __imp__sub_00233C10(ctx, base);
    g_cri.unlock();
}
X86_FUNC(sub_00236BC0) {
    EnterCri();
    std::lock_guard lock(g_cri, std::adopt_lock);
    __imp__sub_00236BC0(ctx, base);
}
X86_FUNC(sub_00236080) {
    EnterCri();
    std::lock_guard lock(g_cri, std::adopt_lock);
    __imp__sub_00236080(ctx, base);
}

// This helper has no kernel calls, so it needs a cooperative scheduling
// point. The original body handles thread exit after the stop flag is set.
X86_FUNC(sub_00233CB0) {
    while (!kernel::R32(0x485B40)) {
        kernel::W32(0x485B28, kernel::R32(0x485B28) + 1);
        kernel::GuestYield yield;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    __imp__sub_00233CB0(ctx, base);
}
