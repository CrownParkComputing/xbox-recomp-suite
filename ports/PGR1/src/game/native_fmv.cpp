#ifndef _WIN32
#include "native_movie.h"
#include "xdk/hle.h"
#include "xdk/d3d8.h"
#include "gpu/texture.h"
#include <chrono>
#include <thread>
using namespace kernel;
X86_EXTERN_FUNC(__imp__sub_000E66C0);
// PGR's cdecl fullscreen movie player. Retain its input update and movie-mode
// transitions, using native decoding and a timestamp clock for the original Bink.
X86_FUNC(sub_000E66C0) {
    const std::string name = ReadCString(R32(ctx.esp + 4), 4096);
    NativeMovie movie;
    if (!movie.Open(ResolvePath(name, 0))) {
        LOGE("native FMV: %s: %s", name.c_str(), movie.Error().c_str());
        __imp__sub_000E66C0(ctx, base);
        return;
    }
    LOGI("native FMV: %s, %dx%d", name.c_str(), movie.Width(), movie.Height());
    CallGuest(ctx, 0xDA9F0, {1});
    using Clock = std::chrono::steady_clock;
    auto start = host::GameNow();
    bool started = false, skip = false;
    unsigned frames = 0;
    while (movie.Decode()) {
        if (!started) { start = host::GameNow(); movie.StartAudio(); started = true; }
        do {
            CallGuest(ctx, 0xC3940, {});
            uint32_t pads = R32(0x15980C);
            for (unsigned i = 0; i < 4 && pads; ++i) {
                uint32_t index = R32(pads + i * 4);
                if (index >= 4) continue;
                uint32_t pad = 0x2AE2B4 + index * 0xB4;
                for (unsigned off : {0u,4u,8u,12u,16u,20u,32u,36u})
                    skip |= R32(pad + off) != 0;
            }
            if (skip) break;
            if (std::chrono::duration<double>(host::GameNow()-start).count() >= movie.Pts()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        if (skip) break;
        {
            std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
            const uint32_t back = R32(0xF9310) + 0x2150;
            gpu::TextureDesc dst;
            if (!gpu::Describe(back, dst) || !dst.linear || gpu::Format(dst.format).bits != 32) {
                LOGE("native FMV: unsupported back buffer"); break;
            }
            // Fit the source aspect ratio, with black letterboxing.
            double scale = std::min(double(dst.width)/movie.Width(), double(dst.height)/movie.Height());
            int w = std::max(1, int(movie.Width()*scale)), h = std::max(1, int(movie.Height()*scale));
            int left = (int(dst.width)-w)/2, top = (int(dst.height)-h)/2;
            for (unsigned y=0; y<dst.height; ++y) {
                auto* row = static_cast<uint8_t*>(HostPtr(dst.data + y*dst.pitch));
                std::memset(row, 0, dst.width*4);
                if (int(y)<top || int(y)>=top+h) continue;
                unsigned sy = (int(y)-top)*movie.Height()/h;
                for (int x=0; x<w; ++x) {
                    unsigned sx = x*movie.Width()/w;
                    std::memcpy(row+(left+x)*4, movie.Pixels().data()+(sy*movie.Width()+sx)*4,4);
                }
            }
            gpu::MarkDirty(back);
        }
        CallGuest(ctx, SYM(D3DDevice_Present), {0,0,0,0});
        ++frames;
    }
    if (!movie.Error().empty()) LOGE("native FMV: %s", movie.Error().c_str());
    CallGuest(ctx, 0xDA9F0, {0});
    LOGI("native FMV: closed after %u frames%s", frames, skip ? " (skipped)" : "");
    ctx.eax = 0;
    ctx.esp += 4;
}
#endif
