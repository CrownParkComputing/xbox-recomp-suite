#include <cstring>

#include "xdk/hle.h"

namespace {
// Validate the runtime's opaque controller handle through its own input API.
// The original XPP helpers expect USB device records instead of host handles.
uint32_t PollPad(X86Context& ctx, uint32_t handle) {
    const uint32_t saved_esp = ctx.esp;
    ctx.esp -= 24;  // temporary XINPUT_STATE below the caller's stack
    const uint32_t status = GUEST_CALL(XInputGetState, ctx, {handle, ctx.esp});
    ctx.esp = saved_esp;
    return status;
}
}

// XInputGetCapabilities(handle, caps), called at 0x0029D9D8 immediately after
// XInputOpen. XDK 4928 packs this structure into 25 bytes, with In at +3.
X86_FUNC(sub_0030C5BF) {
    const uint32_t handle = kernel::R32(ctx.esp + 4);
    const uint32_t caps = kernel::R32(ctx.esp + 8);
    const uint32_t status = caps ? PollPad(ctx, handle) : 87;  // ERROR_INVALID_PARAMETER
    if (!status) {
        auto* out = static_cast<uint8_t*>(kernel::HostPtr(caps));
        std::memset(out, 0, 25);
        out[0] = 1;  // XINPUT_DEVSUBTYPE_GC_GAMEPAD
        out[3] = 0xFF;  // supported digital buttons
        std::memset(out + 5, 0xFF, 20);  // analog buttons, sticks and rumble
    }
    kernel::Return(ctx, status, 2);
}

// XInputPoll(handle), called at 0x0029DF2D before XInputGetState. The host
// refreshes state on demand; the original would submit a USB request.
X86_FUNC(sub_0030C83C) {
    const uint32_t status = PollPad(ctx, kernel::R32(ctx.esp + 4));
    kernel::Return(ctx, status, 1);
}
