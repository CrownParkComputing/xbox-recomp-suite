#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "xdk/hle.h"

X86_EXTERN_FUNC(sub_00279D50);
X86_FUNC(sub_001B2594) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "clock smoke failed: %s\n", message); std::exit(1); }
    };
    const uint32_t object = mem::AllocSystem(512), out = object + 384;
    auto read = [&] {
        const uint32_t stack = ctx.esp;
        ctx.esp -= 8;
        W32(ctx.esp, 0xFFFFFFF0); W32(ctx.esp + 4, out);
        ctx.ecx = object;
        ctx.ebx = 0x12345678; ctx.esi = 0x23456789; ctx.edi = 0x3456789A; ctx.ebp = 0x456789AB;
        sub_00279D50(ctx, base);
        check(ctx.esp == stack, "thiscall pops its output argument");
        check(ctx.ebx == 0x12345678 && ctx.esi == 0x23456789 && ctx.edi == 0x3456789A && ctx.ebp == 0x456789AB,
              "clock preserves callee-saved registers");
        const uint64_t expected = (R64(object + 0x148) * 10000 / 48000) * 1000;
        check(R64(out) == expected, "sample accumulator converts to 100 ns reference time");
        return R64(out);
    };
    const uint64_t first = read();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const uint64_t second = read();
    check(second > first, "audio clock advances");
    check(second - first >= 100000 && second - first < 10000000, "reference time follows elapsed wall time");
    // A previous counter near UINT32_MAX wraps into the new low counter.
    const uint64_t samples = R64(object + 0x148);
    W32(object + 0x144, 0xFFFFFFF0);
    read();
    check(R64(object + 0x148) > samples && R64(object + 0x148) - samples < 48000, "32-bit counter wrap keeps sample time monotonic");
    std::fprintf(stderr, "clock smoke: %u checks passed\n", checks);
    std::exit(0);
}
