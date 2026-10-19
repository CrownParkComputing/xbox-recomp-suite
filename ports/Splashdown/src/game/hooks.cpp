// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#include <cstdlib>
#include <mutex>
#include "gpu/texture.h"
#include "xdk/hle.h"
#include "xdk/d3d8.h"

// BinkCopyToBuffer (0x1B98A0) writes into a texture, not the display.
// Let the title draw it: the course preview positions and masks this texture
// inside its menu. Keep the display-copy fallback only for full-screen movies;
// copying a smaller preview would overwrite the map and text.
X86_EXTERN_FUNC(__imp__sub_001B98A0);
X86_FUNC(sub_001B98A0) {
    const uint32_t bink = kernel::R32(ctx.esp + 4);
    const uint32_t destination = kernel::R32(ctx.esp + 8);
    const uint32_t pitch = kernel::R32(ctx.esp + 12);
    const uint32_t x = kernel::R32(ctx.esp + 20), y = kernel::R32(ctx.esp + 24);
    __imp__sub_001B98A0(ctx, base);
    if (!bink || !destination || x || y) return;
    std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
    uint32_t screen_width, screen_height;
    gpu::SurfaceSize(d3d::State().rt, screen_width, screen_height);
    if (kernel::R32(bink) == screen_width && kernel::R32(bink + 4) == screen_height)
        d3d::SetMovieFrame(destination, screen_width, screen_height, pitch, 0, 0);
}

// Test-only fast-forward: setting SPLASH_SKIP_MOVIES=1 makes the title take its own movie-finished path.
X86_EXTERN_FUNC(__imp__sub_001B98E0);
X86_FUNC(sub_001B98E0) {
    const uint32_t bink = kernel::R32(ctx.esp + 4);
    __imp__sub_001B98E0(ctx, base);
    const char* skip = std::getenv("SPLASH_SKIP_MOVIES");
    if (skip && *skip && bink) {
        const uint32_t total = kernel::R32(bink + 8);
        if (total > 1) kernel::W32(bink + 0xC, total - 1);
    }
}

// Diagnostic workaround: the rider-select audio worker reaches this helper with a null object.
// Only bypass the dereference when explicitly requested; the original returns EAX=0 and RET 4.
X86_EXTERN_FUNC(__imp__sub_00186F16);
X86_FUNC(sub_00186F16) {
    const char* guard = std::getenv("SPLASH_GUARD_NULL_186F16");
    if (ctx.ecx == 0 && guard && *guard) {
        ctx.eax = 0;
        ctx.esp += 8;  // return address plus the single stack argument (RET 4)
        return;
    }
    __imp__sub_00186F16(ctx, base);
}

// Diagnostic workaround for scene setup: a missing scene resource leaves the nested target null.
// Return E_FAIL so the game's caller can take its existing error path instead of dereferencing 0xB4.
X86_EXTERN_FUNC(__imp__sub_001804E8);
X86_FUNC(sub_001804E8) {
    const char* guard = std::getenv("SPLASH_GUARD_NULL_1804E8");
    if (guard && *guard) {
        const uint32_t object = kernel::R32(ctx.esp + 4);
        const uint32_t nested = object ? kernel::R32(object + 0x10) : 0;
        if (!nested) {
            ctx.eax = 0x80004005;
            ctx.esp += 16;  // return address plus the three stack arguments (RET 0x0C)
            return;
        }
    }
    __imp__sub_001804E8(ctx, base);
}

