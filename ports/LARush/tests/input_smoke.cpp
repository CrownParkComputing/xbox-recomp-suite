// Verify controller discovery and reports through the real mapped XAPI calls.
// The headless input host supplies one controller on port 0.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kernel/memory.h"
#include "xdk/hle.h"

X86_FUNC(sub_001B2594) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "input smoke failed: %s\n", what); std::exit(1); }
    };
    const uint32_t type = SYM(XPP_GamepadType);
    const uint32_t state = mem::AllocSystem(32);
    CallGuest(ctx, SYM(XInitDevices), {0, 0});
    for (uint32_t port = 1; port < 4; ++port)
        check(CallGuest(ctx, SYM(XInputOpen), {type, port, 0, 0}) == 0,
              "an unplugged port has no handle and cannot repeat player input");
    check(CallGuest(ctx, SYM(XInputOpen), {type, 4, 0, 0}) == 0, "invalid port is refused");
    const uint32_t handle = CallGuest(ctx, SYM(XInputOpen), {type, 0, 0, 0});
    check(handle != 0, "connected controller opens");
    if (FILE* command = std::fopen("cmd", "w")) { std::fputs("UP+A 12\n", command); std::fclose(command); }
    std::memset(HostPtr(state), 0xCD, 32);
    check(CallGuest(ctx, SYM(XInputGetState), {handle, state}) == 0, "read connected controller");
    check((R16(state + 4) & 1) != 0, "up reaches the digital report");
    check((R16(state + 6) & 0xFF) == 255, "A reaches the analog button report");
    check(R32(state + 24) == 0xCDCDCDCDu, "report leaves following memory intact");
    const uint32_t packet = R32(state);
    for (unsigned read = 0; read < 32; ++read) {
        check(CallGuest(ctx, SYM(XInputGetState), {handle, state}) == 0, "repeat stationary report");
        check(R32(state) == packet, "unchanged values preserve packet number");
    }
    if (FILE* command = std::fopen("cmd", "w")) { std::fputs("NONE 0\n", command); std::fclose(command); }
    check(CallGuest(ctx, SYM(XInputGetState), {handle, state}) == 0, "release report");
    check(R32(state) != packet && R16(state + 4) == 0 && G<uint8_t>(state + 6) == 0, "release changes packet and values");
    CallGuest(ctx, SYM(XInputClose), {handle});
    check(CallGuest(ctx, SYM(XInputGetState), {handle, state}) == 6, "closed handle is invalid");
    std::fprintf(stderr, "input smoke: %u checks passed\n", checks);
    std::exit(0);
}
