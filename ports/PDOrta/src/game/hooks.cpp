// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <cstring>
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
// Orta's own QueryInterface-style getter for the DirectSound object the title keeps
// at holder + 0x10. The original AddRefs through the object's COM vtable; the host's
// DirectSound object has no guest vtable, so the guest thunk at 0x2C127D reads
// [interface - 8] as a vtable pointer and dies calling [vtable + 4]. The interface the
// title stores is the aggregated one: obj + 8, with the real vtable at obj + 0.
// Return a fabricated interface instead: an object whose +0 points at a vtable of
// S_OK stubs, so interface thunk calls succeed without touching host internals.
uint32_t FakeObject() {
    static uint32_t vtbl = 0, obj = 0;
    if (!vtbl) {
        vtbl = mem::AllocSystem(0x40);
        obj = mem::AllocSystem(0x40);
        std::memset(kernel::HostPtr(obj), 0, 0x40);
        // entries: [0] QueryInterface, [1] AddRef, [2] Release, rest unused stubs.
        // 0x2C1273 is xor eax,eax; ret 0xC and 0x2C1278 is xor eax,eax; ret 4,
        // the XDK's own null methods.
        kernel::W32(vtbl + 0, 0x2C1273);
        kernel::W32(vtbl + 4, 0x2C1278);
        kernel::W32(vtbl + 8, 0x2C1278);
        for (uint32_t o = 0xC; o < 0x40; o += 4) kernel::W32(vtbl + o, 0x2C1278);
        kernel::W32(obj, vtbl);
    }
    return obj;
}
uint32_t FakeInterface() { return FakeObject() + 8; }
}

// The title stores what DirectSoundCreate writes at holder + 0x14 and later AddRefs
// and Releases it through the aggregated-interface thunk; give it the same layout
// the real create produces: an interface pointer 8 bytes past the object.
void FixDsStore(X86Context& ctx, uint8_t* base) {
    const uint32_t holder = ctx.edi;
    if (kernel::R32(holder + 0x14)) kernel::W32(holder + 0x14, FakeInterface());
}

// IDirectSound_SetAllParameters is a delegating thunk the shared HLE does not
// implement; the real CDirectSound method writes through a parameter block the
// fabricated object does not have. Batch 3D settings fall back to defaults.
X86_FUNC(sub_002C43BD) { kernel::Return(ctx, 0, 3); }

X86_FUNC(sub_0021E430) {
    const uint32_t holder = kernel::R32(ctx.esp + 4);
    const uint32_t iid = kernel::R32(ctx.esp + 8);
    const uint32_t out = kernel::R32(ctx.esp + 12);
    const uint32_t obj = kernel::R32(holder + 0x10);
    uint32_t hr;
    if (!obj) {
        hr = 0x800401F0;
    } else if (!iid) {
        hr = 0x80070057;
    } else if (!std::memcmp(kernel::HostPtr(iid), kernel::HostPtr(0x32D3F8), 16)) {
        kernel::W32(out, FakeInterface());
        hr = 0;
    } else {
        kernel::W32(out, 0);
        hr = 0x80004002;
    }
    kernel::Return(ctx, hr, 3);
}
