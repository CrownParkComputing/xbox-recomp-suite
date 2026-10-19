#include <filesystem>
#include <cstdlib>
#include <csignal>
#include <execinfo.h>
#include <ucontext.h>
#include "xdk/hle.h"
#include "core/log.h"
using namespace kernel;

namespace {
struct CrashTrace {
    CrashTrace() {
        struct sigaction action{};
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = [](int signal, siginfo_t* info, void* machine) {
            std::fprintf(stderr, "probe crash: signal %d address %p guest %08X\n", signal, info->si_addr,
                         uint32_t(reinterpret_cast<uintptr_t>(info->si_addr) - reinterpret_cast<uintptr_t>(g_base)));
#if defined(__x86_64__)
            auto* context = static_cast<ucontext_t*>(machine);
            std::fprintf(stderr, "probe crash: host RIP %llX\n", (unsigned long long)context->uc_mcontext.gregs[REG_RIP]);
#endif
            if (auto* thread = Current(); thread && thread->ctx) LogGuestStack(*thread->ctx, 128);
            void* frames[48];
            const int count = backtrace(frames, 48);
            backtrace_symbols_fd(frames, count, 2);
            std::_Exit(128 + signal);
        };
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, nullptr);
        sigaction(SIGBUS, &action, nullptr);
    }
} crash_trace;
}

X86_EXTERN_FUNC(__real_sub_001B0105);
X86_FUNC(__wrap_sub_001B0105) {
    const auto path = ReadCString(R32(ctx.esp + 4));
    if (path.ends_with(".xmv") || path.ends_with(".XMV")) {
        static bool traced = false;
        if (!traced) { traced = true; LogGuestStack(ctx, 128); }
    }
    __real_sub_001B0105(ctx, base);
}
X86_EXTERN_FUNC(__real_sub_0018AE70);
X86_FUNC(__wrap_sub_0018AE70) {
    const uint32_t root = ctx.esi, out = ctx.edi, logical = ctx.eax;
    __real_sub_0018AE70(ctx, base);
    static uint32_t last[4] = {~0u, ~0u, ~0u, ~0u};
    const uint32_t now = R32(out + 0x20);
    if (logical < 4 && last[logical] != now) {
        last[logical] = now;
        LOGI("processed probe: logical=%u root=%08X selection=%u flags=%02X raw=%04X start=%u A=%u active=%u result=%u",
             logical, root, G<uint8_t>(root+0x14), G<uint8_t>(root+0x10), R16(root+0x164), G<uint8_t>(out+0x22), G<uint8_t>(out+0x1a), G<uint8_t>(out+0x24), ctx.eax);
    }
}

X86_EXTERN_FUNC(__real_sub_0029AC5F);
X86_FUNC(__wrap_sub_0029AC5F) {
    const uint32_t type = R32(ctx.esp + 4), port = R32(ctx.esp + 8);
    __real_sub_0029AC5F(ctx, base);
    LOGI("controller probe: open type=%08X expected=%08X port=%u result=%08X", type, SYM(XPP_GamepadType), port, ctx.eax);
}
X86_EXTERN_FUNC(__real_sub_0029AE99);
X86_FUNC(__wrap_sub_0029AE99) {
    const uint32_t state = R32(ctx.esp + 8);
    const uint32_t caller = R32(ctx.esp);
    __real_sub_0029AE99(ctx, base);
    static uint32_t packet = ~0u;
    if (ctx.eax) LOG_FIRST(5, "controller probe: read result=%u", ctx.eax);
    else if (packet != R32(state)) {
        packet = R32(state);
        if (R16(state+4) || G<uint8_t>(state+6))
            LOGI("controller probe: caller=%08X state=%08X packet=%u buttons=%04X A=%u B=%u X=%u", caller, state, packet, R16(state+4), G<uint8_t>(state+6), G<uint8_t>(state+7), G<uint8_t>(state+8));
        if (std::filesystem::exists("inspect")) {
            std::filesystem::remove("inspect");
            LogGuestStack(ctx, 128);
        }
    }
}

X86_EXTERN_FUNC(__real_sub_00263560);
X86_FUNC(__wrap_sub_00263560) {
    const uint32_t stream = R32(ctx.esp + 4);
    const uint32_t in = R32(stream), avail = R32(stream + 4), out = R32(stream + 12);
    __real_sub_00263560(ctx, base);
    LOGI("inflate probe: result %d in=%08X avail=%u -> %u total=%u out=%08X total=%u msg=%s",
         int32_t(ctx.eax), in, avail, R32(stream + 4), R32(stream + 8), out, R32(stream + 20),
         ReadCString(R32(stream + 24)).c_str());
}
X86_EXTERN_FUNC(__real_sub_00188A90);
X86_FUNC(__wrap_sub_00188A90) {
    const uint32_t job = ctx.eax;
    const uint32_t before = R32(job);
    __real_sub_00188A90(ctx, base);
    if (R32(job) != before)
        LOGI("resource probe: %s state %d -> %d read=%u/%u block=%u input=%08X avail=%u",
             ReadCString(job + 12).c_str(), int32_t(before), int32_t(R32(job)),
             R32(job + 0x22c), R32(job + 0x214), R32(job + 0x37c), R32(job + 0x384), R32(job + 0x388));
}
