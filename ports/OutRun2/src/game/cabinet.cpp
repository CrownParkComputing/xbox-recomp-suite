#include "kernel/kernel.h"
#include "core/input.h"
#include "app/app.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <mutex>

namespace {
std::mutex storage_lock;
template <size_t N> struct Storage {
    std::array<uint8_t, N> bytes;
    Storage() { bytes.fill(0xFF); }
};
Storage<0x2000> eeprom; // The game stores its system block at offset 0x1F00.
Storage<0x20000> backup;
Storage<0x10000> controller_firmware;
std::mutex board_lock;
uint32_t reports = 0;
uint16_t coin_counter = 0;
bool coin_down = false;
std::mutex motor_lock;
uint8_t motor_status = 0;
bool motor_reply = false;

void Byte(uint32_t address, uint8_t value) { *static_cast<uint8_t*>(kernel::HostPtr(address)) = value; }
void BigWord(uint32_t address, uint16_t value) { Byte(address, value >> 8); Byte(address + 1, value); }

void UpdateBoard() {
    using kernel::W32;
    if (!reports) {
        reports = mem::AllocSystem(64);
        if (!reports) return;
        std::memset(kernel::HostPtr(reports), 0, 64);
        LOGI("OutRun2: virtual JVS driving board: steering, accelerator, brake");
    }
    // 0x10EF30 returns this node array; public readers index from one with
    // stride 0xE0 (0xFF3D5). Switch report/capabilities: 0xFF7AD..0xFF7CD.
    const uint32_t node = 0x550EAC + 0xE0;
    Byte(0x550AA9, 1); // Node count returned by 0x10EFB0/0xFF370.
    // 0xFF380/0xFF390 read PINSA and the game inverts its low byte at
    // 0x5CB55. All high means the physical test/service inputs are released.
    W32(0x1B6AE0, 0xFF);
    W32(node + 0xAC, reports);
    W32(node + 0xB0, reports + 8);
    W32(node + 0xB4, reports + 16);
    // Capability requirements at 0x5C35B..0x5C384: 2 players, at least
    // 12 switches, 8 analog channels and 6 output bits.
    Byte(node + 0xCC, 2);
    Byte(node + 0xCD, 13);
    Byte(node + 0xCE, 2);
    Byte(node + 0xCF, 8);
    Byte(node + 0xD0, 16);
    Byte(node + 0xDA, 8);
    const auto pad = input::Poll(0);
#ifdef OUTRUN2_SDL_INPUT
    const bool enabled = !app::Unfocused() && !app::GraphicsMenuOpen();
#else
    const bool enabled = true;
#endif
    const bool coin = enabled && (pad.buttons & 0x20);
    if (coin && !coin_down) {
        coin_counter = (coin_counter + 1) & 0x3FFF;
        LOGI("OutRun2: coin inserted");
    }
    coin_down = coin;
    BigWord(reports + 8, coin_counter);
    Byte(reports, 0);
    Byte(reports + 1, enabled ? uint8_t(((pad.buttons & 0x10) ? 0x80 : 0) |
         (pad.analog[0] ? 2 : 0) | (pad.analog[1] ? 1 : 0)) : 0);
    Byte(reports + 2, enabled && pad.analog[2] ? 0x80 : 0);
    const int steer = enabled ? pad.lx : 0;
    const int accelerator = enabled ? std::max(int(pad.analog[7]) * 257, std::max(0, int(pad.ly)) * 2) : 0;
    const int brake = enabled ? std::max(int(pad.analog[6]) * 257, std::max(0, -int(pad.ly)) * 2) : 0;
    BigWord(reports + 16, uint16_t(steer + 32768));
    BigWord(reports + 18, uint16_t(std::min(accelerator, 65535)));
    BigWord(reports + 20, uint16_t(std::min(brake, 65535)));
}

template <size_t N>
void Transfer(X86Context& ctx, Storage<N>& storage, bool write, const char* name) {
    const kernel::Args args{ctx};
    const uint32_t offset = args[0], bytes = args[1], buffer = args[2];
    if (!buffer || offset > N || bytes > N - offset) {
        kernel::Return(ctx, kernel::kInvalidParameter, 4);
        return;
    }
    std::lock_guard guard(storage_lock);
    if (write) std::memcpy(storage.bytes.data() + offset, kernel::HostPtr(buffer), bytes);
    else std::memcpy(kernel::HostPtr(buffer), storage.bytes.data() + offset, bytes);
    LOG_FIRST(12, "OutRun2: virtual %s %s at %X, %u bytes", name, write ? "write" : "read", offset, bytes);
    kernel::Return(ctx, 0, 4);
}
}

// Public JVS storage APIs: offset, byte count, buffer, wait flag; stdcall
// ret 0x10. Keep the original acLib initialization, checksums and credit
// manager active while supplying the storage that was on the arcade board.
// These initial buffers are volatile; cabinet persistence is not implemented.
X86_FUNC(sub_001C1540) { Transfer(ctx, eeprom, false, "EEPROM"); }
X86_FUNC(sub_001C1720) { Transfer(ctx, eeprom, true, "EEPROM"); }
X86_FUNC(sub_001C1900) { Transfer(ctx, backup, false, "backup SRAM"); }
X86_FUNC(sub_001C1A50) { Transfer(ctx, backup, true, "backup SRAM"); }
X86_FUNC(sub_001C0980) { Transfer(ctx, controller_firmware, false, "controller firmware"); }
X86_FUNC(sub_001C0BC0) { Transfer(ctx, controller_firmware, true, "controller firmware"); }

// JvsRTC_Read(offset, bytes, buffer, wait): 0x10EB80 requests 16 bytes,
// then decodes BCD fields from offsets 0,1,6,7,8,9,10,11.
X86_FUNC(sub_001C21B0) {
    const kernel::Args args{ctx};
    const uint32_t offset = args[0], bytes = args[1], buffer = args[2];
    if (!buffer || offset > 16 || bytes > 16 - offset) {
        kernel::Return(ctx, kernel::kInvalidParameter, 4);
        return;
    }
    const auto now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    const auto bcd = [](int value) { return uint8_t((value / 10) * 16 + value % 10); };
    std::array<uint8_t, 16> data{};
    data[0] = bcd(utc.tm_sec); data[1] = bcd(utc.tm_min);
    data[6] = bcd(utc.tm_hour); data[7] = bcd(utc.tm_wday);
    data[8] = bcd(utc.tm_mday); data[9] = bcd(utc.tm_mon + 1);
    data[10] = bcd((utc.tm_year + 1900) % 100); data[11] = bcd((utc.tm_year + 1900) / 100);
    std::memcpy(kernel::HostPtr(buffer), data.data() + offset, bytes);
    LOG_FIRST(1, "OutRun2: virtual cabinet RTC uses host UTC");
    kernel::Return(ctx, 0, 4);
}

// Refresh the virtual board before the original game checks its node count.
// Original public API 0xFF370: no arguments, returns the byte at 0x550AA9.
X86_FUNC(sub_000FF370) {
    std::lock_guard guard(board_lock);
    UpdateBoard();
    kernel::Return(ctx, reports ? 1 : 0, 0);
}

X86_EXTERN_FUNC(__imp__sub_000FF7F0);
// The game caches the node count at startup, then fetches switch reports
// through this four-argument public API each frame (call at 0x5BF78).
X86_FUNC(sub_000FF7F0) {
    {
        std::lock_guard guard(board_lock);
        UpdateBoard();
    }
    __imp__sub_000FF7F0(ctx, base);
}

// Drive-board packet boundary: 0x1D080 sends AL (command), BL and a
// caller-cleaned stack argument through acLib's serial port. 0x1D100
// reads a one-byte response and writes its low three status bits to EDI.
// Keep the original startup checks at 0x1D160: reset 0x7F acknowledges,
// command 0x01 reports readiness (1), centering 0x7C completes (0), and
// status query 0x7D reports no mechanical error (0). No motor is driven.
X86_FUNC(sub_0001D080) {
    std::lock_guard guard(motor_lock);
    const uint8_t command = uint8_t(ctx.eax) & 0x7F;
    motor_status = command == 0x01 || command == 0x7F ? 1 : 0;
    motor_reply = true;
    LOG_FIRST(8, "OutRun2: virtual steering motor command %02X", command);
    kernel::Return(ctx, 1, 0);
}

X86_FUNC(sub_0001D100) {
    std::lock_guard guard(motor_lock);
    if (motor_reply && ctx.edi) Byte(ctx.edi, motor_status);
    const bool available = motor_reply;
    motor_reply = false;
    kernel::Return(ctx, available ? 1 : 0, 0);
}
