// VC3's startup uses the largest contiguous allocation as a cabinet
// heuristic. A native host can have 128 MiB and still use the game's Xbox pad
// path. Select that existing path; leave all cabinet protocol checks intact.
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "image_symbols.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"
#include "xdk/d3d8.h"
#include "app/app.h"
#include "game/mouse.h"

void VCop3_SelectControllerInput(X86Context&, uint8_t*) {
    const char* mode = std::getenv("VCOP3_INPUT");
    if (mode && !std::strcmp(mode, "arcade")) {
        LOGI("vcop3: original arcade input detection retained");
        return;
    }
    kernel::W32(SYM(VC3_InputMode), 0);
    LOGI("vcop3: using the game's Xbox controller input path");
}

// The game's original result reader remains responsible for validating the
// report and copying the GPU's sample count/timestamp. Submit its queued work
// first so the backend can produce that report before the polling read.
void VCop3_DrainVisibilityCommands(X86Context&, uint8_t*) {
    d3d::FlushGuestCommands();
}

void VCop3_UseGameCanvas(X86Context&, uint8_t*) {
    // 0x39570 initially requests 640x480; the cabinet mode helper at 0xF87E0
    // can replace it with 1024x768 although the UI/gun space stays 640x480.
    kernel::W32(SYM(VC3_PresentParams), 640);
    kernel::W32(SYM(VC3_PresentParams) + 4, 480);
}

void VCop3_BeginMousePoll(X86Context&, uint8_t*) { vcop3::BeginGuestMousePoll(); }
void VCop3_EndMousePoll(X86Context&, uint8_t*) { vcop3::EndGuestMousePoll(); }

void VCop3_ApplyMouseAim(X86Context& ctx, uint8_t*) {
#ifdef VCOP3_SDL_INPUT
    if (ctx.esi != SYM(VC3_Gun1) || app::Unfocused() || app::GraphicsMenuOpen()) return;
    const auto mouse = vcop3::ReadMouse();
    if (!mouse.active) return;
    const auto aim = vcop3::MapMouse(mouse.x, mouse.y, mouse.width, mouse.height, app::Widescreen());
    static const bool trace = std::getenv("VCOP3_TRACE_INPUT") != nullptr;
    static int32_t last_x = INT32_MIN, last_y = INT32_MIN;
    if (trace && (aim.x != last_x || aim.y != last_y)) {
        LOG_FIRST(16, "vcop3 input: applying gun aim %d,%d", aim.x, aim.y);
        last_x = aim.x; last_y = aim.y;
    }
    // Original reticle drawing adds 320 to X and subtracts Y from 240
    // (0x7A4C7..0x7A4E1). Update both final and uncalibrated coordinates.
    kernel::W32(ctx.esi + 0x0C, uint32_t(aim.x));
    kernel::W32(ctx.esi + 0x10, uint32_t(aim.y));
    kernel::W32(ctx.esi + 0x1C, uint32_t(aim.x));
    kernel::W32(ctx.esi + 0x20, uint32_t(aim.y));
#else
    (void)ctx;
#endif
}
