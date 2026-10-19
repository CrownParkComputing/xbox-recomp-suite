// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#include "xdk/hle.h"

// XAPI OutputDebugStringA (int 2Dh service 1 followed by its debugger marker).
// Route diagnostic text to the host log instead of executing the marker int3.
X86_FUNC(sub_000D2771) {
    const std::string text = kernel::ReadCString(kernel::R32(ctx.esp + 4), 4096);
    LOGI("PGR1: %s", text.c_str());
    kernel::Return(ctx, 0, 1);
}
