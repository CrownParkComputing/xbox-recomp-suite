// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include <SDL3/SDL_scancode.h>

#include <chrono>

#include "core/keyboard_pad.h"
#include "xdk/hle.h"

using namespace kernel;

// Tiles. The miniport programs the NV2A's frame buffer and graphics registers for a tile and its Z compression, through
// a register base this machine does not have. D3DDevice_SetTileNoWait (0x34E0E0) keeps its own copy of the tile in the
// device, then calls these; the game's surface setup (0x3D500) inlines D3DDevice_SetTile and calls the second directly.
// Both become no-ops, as the SDK's D3DDevice_SetTile is.

// sets a tile: stdcall, 7 arguments (miniport, ...), returns 1
X86_FUNC(sub_003566DA) {
    (void)base;
    Return(ctx, 1, 7);
}

// clears a tile: stdcall (miniport, z compress), tile index in ebx, returns 1
X86_FUNC(sub_003568C5) {
    (void)base;
    Return(ctx, 1, 2);
}

// Films (XMV). The player at 0x1C943A asks the decoder for the next frame, status in its third argument: 1 a new
// frame, 2 or 3 the film is over. Tab ends the film playing, so the race training films and the intros can be
// skipped; a key of its own, as the menus loop films behind them and read A and Start themselves.
X86_EXTERN_FUNC(__imp__sub_002CCB1C);
X86_FUNC(sub_002CCB1C) {
    uint32_t status = R32(ctx.esp + 12);
    __imp__sub_002CCB1C(ctx, base);
    // a press counts from the film's first frame: one between films does not skip the next
    static uint32_t seen = 0;
    static auto last = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    uint32_t presses = input::keyboard::Presses(SDL_SCANCODE_TAB);
    if (now - last > std::chrono::milliseconds(500)) seen = presses;
    last = now;
    if (presses != seen && status) W32(status, 2);
    seen = presses;
}
