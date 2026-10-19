// Exercise the retail GetGammaRamp body used to save/restore loading-screen fades.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "kernel/memory.h"
#include "xdk/hle.h"

X86_FUNC(sub_0013A80B) {
    using namespace kernel;
    (void)base;
    auto check = [](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "gamma smoke failed: %s\n", what);
            std::exit(1);
        }
    };
    const uint32_t memory = mem::AllocSystem(4096), saved = memory + 256, read = memory + 1024;
    const uint32_t fade = memory + 1792;
    std::memset(HostPtr(memory), 0, 4096);
    W32(memory, 64);
    W32(memory + 4, 64);
    W32(memory + 8, 6);
    W32(memory + 12, 1);
    check(CallGuest(ctx, SYM(Direct3D_CreateDevice), {0, 1, 0, 0, memory, memory + 64}) == 0, "create device");
    constexpr uint32_t get_gamma = 0x15E0B0;  // retail 4531 getter, not a host override
    CallGuest(ctx, get_gamma, {read});
    for (uint32_t i = 0; i < 768; ++i)
        check(static_cast<const uint8_t*>(HostPtr(read))[i] == uint8_t(i & 255), "initial guest gamma is identity");

    // Save a non-default brightness curve, fade to black, then restore it exactly
    // as the title does around course loading.
    for (uint32_t i = 0; i < 768; ++i)
        static_cast<uint8_t*>(HostPtr(saved))[i] = uint8_t((i & 255) * 3 / 4);
    CallGuest(ctx, SYM(D3DDevice_SetGammaRamp), {2, saved});
    CallGuest(ctx, get_gamma, {read});
    check(std::memcmp(HostPtr(saved), HostPtr(read), 768) == 0, "guest getter saves brightness curve");
    CallGuest(ctx, SYM(D3DDevice_SetGammaRamp), {2, fade});
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, get_gamma, {fade + 768});
    check(std::memcmp(HostPtr(fade), HostPtr(fade + 768), 768) == 0, "black fade is retained");
    CallGuest(ctx, SYM(D3DDevice_SetGammaRamp), {2, read});
    CallGuest(ctx, get_gamma, {fade});
    check(std::memcmp(HostPtr(saved), HostPtr(fade), 768) == 0, "loading fade restores saved brightness");
    std::fprintf(stderr, "gamma smoke: guest save/fade/restore checks passed\n");
    std::exit(0);
}
