#include "kernel/kernel.h"
#include <chrono>
#include <thread>

X86_EXTERN_FUNC(__imp__sub_0009B98C);

// The disc worker 0xA9D20 checks pending work, then suspends itself at
// 0xA9D95. Its Xbox priority-based handoff can lose a wakeup when the host
// runs the submitting and worker threads in parallel. Keep the original
// worker/reader/decoder active, but poll after a 1 ms idle sleep at this
// one call site. Other SuspendThread callers retain the kernel behavior.
X86_FUNC(sub_0009B98C) {
    if (kernel::R32(ctx.esp) == 0xA9D9A) {
        LOG_FIRST(1, "OutRun2: disc worker uses a timed idle wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        kernel::Return(ctx, 0, 1);
        return;
    }
    __imp__sub_0009B98C(ctx, base);
}
