// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#include <chrono>
#include "cpu/x86_context.h"

namespace {
// Xbox APU global sample counter, running at 48 kHz.
uint32_t ApuSampleCounter() {
    static const auto start = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    return static_cast<uint32_t>(static_cast<uint64_t>(us) * 48 / 1000);
}
}

bool LinksReadApuClockEax(X86Context& ctx, uint8_t*) {
    ctx.eax = ApuSampleCounter();
    return true;
}
bool LinksReadApuClockEbx(X86Context& ctx, uint8_t*) {
    ctx.ebx = ApuSampleCounter();
    return true;
}

#include "cpu/x86_ops.h"

// IDirectSoundBuffer::Use3DVoiceData(this, enabled). The host mixer owns
// voices and cannot consume the Xbox APU's precomputed 3D voice data.
// Keep its existing software spatialization; hardware 3D voice data is
// not implemented. Do not enter the guest hardware object's internals.
X86_FUNC(sub_003A01E8) {
    ctx.eax = 0;
    ctx.esp += 12;
}

#include <algorithm>
#include <cstring>
#include "xdk/hle.h"
#include "xdk/d3d8.h"
#include "gpu/texture.h"

// XDK 5558 GetStatus__r2 has the ordinary (this, status*) ABI.
X86_EXTERN_FUNC(GUEST(CDirectSoundStream_GetStatus));
X86_FUNC(sub_0039F454) { GUEST(CDirectSoundStream_GetStatus)(ctx, base); }

// Links' intro uses the Xbox YUV overlay, rather than ordinary D3D swaps.
// Convert its decoded surface through the SDK and present the back buffer.
// Colour-keyed overlays are not supported by this title adapter.
X86_FUNC(sub_002E7C40) {
    const uint32_t surface = kernel::R32(ctx.esp + 4);
    const uint32_t source_rect = kernel::R32(ctx.esp + 8);
    const uint32_t dest_rect = kernel::R32(ctx.esp + 12);
    const uint32_t back = kernel::CallGuest(ctx, SYM(D3DDevice_GetBackBuffer2), {0});
    {
        std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
        gpu::TextureDesc src, dst;
        if (gpu::Describe(surface, src) && gpu::Describe(back, dst) &&
            gpu::Format(src.format).host == gpu::Host::BGRA8 &&
            gpu::Format(dst.format).bits == 32 && dst.linear) {
            std::vector<uint8_t> pixels;
            uint32_t pitch = 0;
            gpu::DecodeLevel(src, 0, 0,
                static_cast<const uint8_t*>(kernel::HostPtr(src.data)), nullptr, pixels, pitch);
            auto rect = [](uint32_t p, int w, int h) {
                std::array<int, 4> r{0, 0, w, h};
                if (p) for (int i = 0; i < 4; ++i) r[i] = int32_t(kernel::R32(p + i * 4));
                return r;
            };
            auto s = rect(source_rect, src.width, src.height);
            auto d = rect(dest_rect, dst.width, dst.height);
            if (d[2] > d[0] && d[3] > d[1] && s[2] > s[0] && s[3] > s[1] && !pixels.empty()) {
                for (int y = std::max(0, d[1]); y < std::min(int(dst.height), d[3]); ++y) {
                    int sy = s[1] + int(int64_t(y - d[1]) * (s[3] - s[1]) / (d[3] - d[1]));
                    if (sy < 0 || sy >= int(src.height)) continue;
                    auto* row = static_cast<uint8_t*>(kernel::HostPtr(dst.data + y * dst.pitch));
                    for (int x = std::max(0, d[0]); x < std::min(int(dst.width), d[2]); ++x) {
                        int sx = s[0] + int(int64_t(x - d[0]) * (s[2] - s[0]) / (d[2] - d[0]));
                        if (sx < 0 || sx >= int(src.width)) continue;
                        std::memcpy(row + x * 4, pixels.data() + sy * pitch + sx * 4, 4);
                    }
                }
                gpu::MarkDirty(back);
            }
        }
        kernel::W32(back, kernel::R32(back) - 1); // release GetBackBuffer2's reference
    }
    kernel::CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    ctx.eax = 0;
    ctx.esp += 24; // return address and five stdcall arguments
}

X86_FUNC(sub_002E7C20) {
    ctx.eax = 1; // conversion and upload are submitted by UpdateOverlay
    ctx.esp += 4;
}

// XOnline wrappers not identified by the external signature database.
// Startup takes one configuration pointer; cleanup takes no arguments.
X86_EXTERN_FUNC(GUEST(XOnlineStartup));
X86_EXTERN_FUNC(GUEST(XOnlineCleanup));
X86_FUNC(sub_0035D808) { GUEST(XOnlineStartup)(ctx, base); }
X86_FUNC(sub_0035D8F3) { GUEST(XOnlineCleanup)(ctx, base); }
