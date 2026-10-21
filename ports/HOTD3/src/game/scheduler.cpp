#include "cpu/x86_ops.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(GUEST_ORIG(HOTD3_Scheduler));
X86_EXTERN_FUNC(GUEST_ORIG(HOTD3_SchedulerResume));

// The guest CRT restores its saved registers, stack and SEH state before the
// indirect jump at 0x125961. Unwind intervening C++ calls to the owning guest
// scheduler frame instead of recursively running its continuation inside the
// task that just exited. Nested scheduler calls have distinct saved EBP values.
void HOTD3_ResumeScheduler(X86Context& ctx, uint8_t*) {
    const uint32_t target = kernel::R32(ctx.edx + 0x14);
    if (target == SYM(HOTD3_SchedulerResume)) throw X86Resume{target, ctx.esp};
}

X86_FUNC(GUEST(HOTD3_Scheduler)) {
    const uint32_t frame = ctx.esp - 4; // Original prologue: push ebp; mov ebp, esp.
    bool resume = false;
    for (;;) {
        try {
            if (resume) GUEST_ORIG(HOTD3_SchedulerResume)(ctx, base);
            else GUEST_ORIG(HOTD3_Scheduler)(ctx, base);
            return;
        } catch (const X86Resume& jump) {
            if (jump.target != SYM(HOTD3_SchedulerResume) || ctx.ebp != frame) throw;
            resume = true;
        }
    }
}
