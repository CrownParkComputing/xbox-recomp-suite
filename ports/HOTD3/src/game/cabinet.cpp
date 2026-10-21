#include "xdk/hle.h"
#include "kernel/memory.h"
#include "core/input.h"
#include "core/log.h"
#include "game/mouse.h"
#include "app/app.h"
#include <cstring>

namespace {
using namespace kernel;
uint32_t reports = 0;
bool coin_down = false;
uint16_t coins = 0;
bool HostActive() {
#ifdef HOTD3_SDL_INPUT
    return !app::Unfocused() && !app::GraphicsMenuOpen();
#else
    return true;
#endif
}
void Byte(uint32_t address, uint8_t value) { *static_cast<uint8_t*>(HostPtr(address)) = value; }
void BigWord(uint32_t address, uint16_t value) { Byte(address, value >> 8); Byte(address + 1, value); }

void DescribeBoard() {
    const uint32_t node = SYM(HOTD3_JvsNodes) + 0xE0; // Public APIs index nodes starting at one.
    Byte(SYM(HOTD3_JvsCount), 1);
    // 0x11CDC0/0x11CEE0/0x11D160 read these report pointers.
    W32(node + 0xAC, reports);
    W32(node + 0xB0, reports + 8);
    W32(node + 0xB4, reports + 16);
    // Capability readers at 0x11CD20, 0x11CE70, 0x11D0C0, 0x11D260.
    Byte(node + 0xCC, 2);   // Players.
    Byte(node + 0xCD, 13);  // Switches per player.
    Byte(node + 0xCE, 2);   // Coin slots.
    Byte(node + 0xCF, 4);   // Analog channels: two guns, XY each.
    Byte(node + 0xD0, 16);  // Analog precision.
    Byte(node + 0xDA, 6);   // Cabinet outputs.
}
}

HLE_STDCALL(HOTD3_InitCabinet, 3) {
    if (!reports) reports = mem::AllocSystem(64);
    if (!reports) return 5;
    std::memset(HostPtr(reports), 0, 64);
    for (int channel = 0; channel < 4; ++channel) BigWord(reports + 16 + channel * 2, 0x8000);
    DescribeBoard();
    // Initialize the real public-API lock and credit manager; skipping the
    // physical USB setup alone leaves their critical sections uninitialized.
    CallGuest(ctx, R32(SYM(HOTD3_RtlInitCSThunk)), {SYM(HOTD3_JvsLock)});
    GUEST_CALL(HOTD3_CoinInit, ctx, {0, 0, 0});
    coin_down = false;
    coins = 0;
    LOGI("hotd3: virtual cabinet: two guns, two coin slots, original credit manager");
    return 0;
}

void HOTD3_UpdateCabinet(X86Context& ctx, uint8_t*) {
    if (!reports) return;
    DescribeBoard();
    hotd3::BeginGuestMousePoll();
    const auto pad = input::Poll(0);
    hotd3::EndGuestMousePoll();
    const bool enabled = HostActive();
    const bool coin = enabled && (pad.buttons & 0x20);
    if (coin && !coin_down) {
        coins = uint16_t((coins + 1) & 0x3FFF);
        GUEST_CALL(HOTD3_AddCredits, ctx, {0, 1});
        LOGI("hotd3: coin inserted");
    }
    coin_down = coin;
    BigWord(reports + 8, coins);
    Byte(reports, 0); // System/test switch.
    Byte(reports + 1, enabled ? uint8_t(((pad.buttons & 0x10) ? 0x80 : 0) |
         (pad.analog[0] ? 2 : 0)) : 0);
    // 0x119EB converts switch byte 2 bit 7 to game bit 3. The Chihiro
    // gun path at 0x79F6F consumes that edge as the shotgun pump; game
    // bit 1 is instead the gun's invalid-position signal at 0x79F6D.
    Byte(reports + 2, enabled && pad.analog[1] ? 0x80 : 0);
    Byte(reports + 3, 0);
    Byte(reports + 4, 0);
    const auto mouse = hotd3::ReadMouse();
    if (mouse.active && enabled) {
        const auto aim = hotd3::MapMouse(mouse.x, mouse.y, mouse.width, mouse.height, app::Widescreen());
        BigWord(reports + 16, uint16_t((aim.x + 320) * 65535u / 639u));
        BigWord(reports + 18, uint16_t((240 - aim.y) * 65535u / 479u));
    }
}
