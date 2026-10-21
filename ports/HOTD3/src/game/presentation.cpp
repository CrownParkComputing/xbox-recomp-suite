#include "xdk/hle.h"
#include "core/log.h"

// The startup caller resets its newly created device before loading game
// assets. Recreate that initial presentation through the host API; the
// original Reset enters the NV2A miniport and dereferences hardware state.
HLE_STDCALL(HOTD3_ResetStartup, 1) {
    using kernel::R32;
    const uint32_t pp = a[0];
    if (R32(ctx.esp) != 0xB122D || !pp ||
        R32(pp + 8) != 6 || R32(pp + 12) != 1 || R32(pp + 0x20) != 1 || R32(pp + 0x24) != 0x2A) {
        LOGE("hotd3: unsupported presentation reset from %08X: %ux%u fmt %X count %u depth %u/%X",
             R32(ctx.esp), pp ? R32(pp) : 0, pp ? R32(pp + 4) : 0, pp ? R32(pp + 8) : 0,
             pp ? R32(pp + 12) : 0, pp ? R32(pp + 0x20) : 0, pp ? R32(pp + 0x24) : 0);
        return 0x80004001; // E_NOTIMPL: later resets need their own implementation.
    }
    // The cabinet display helper chose 1024x768 after the game first requested
    // 640x480. Keep the original game canvas for UI and gun coordinates.
    kernel::W32(pp, 640);
    kernel::W32(pp + 4, 480);
    LOGI("hotd3: recreating initial 640x480 presentation targets");
    return GUEST_CALL(Direct3D_CreateDevice, ctx, {0, 1, 0, 0, pp, 0});
}
