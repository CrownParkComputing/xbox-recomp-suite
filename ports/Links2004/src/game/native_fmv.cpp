#ifndef _WIN32
// Links' movie object adapter. Keep the game's scheduling, skip input, texture
// consumers and end-of-movie transitions; replace only decoding and its clock.
#include "native_movie.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include "xdk/hle.h"
#include "xdk/d3d8.h"
#include "gpu/texture.h"

using namespace kernel;
namespace {
using Clock = std::chrono::steady_clock;
struct Playback {
    NativeMovie movie;
    Clock::time_point start;
    bool started = false, pending = false;
    uint32_t texture[2] = {}, slot = 0, frames = 0;
};
std::unordered_map<uint32_t, std::unique_ptr<Playback>> movies;
}
X86_EXTERN_FUNC(__imp__sub_001CF280);
X86_EXTERN_FUNC(__imp__sub_001CF400);
X86_EXTERN_FUNC(__imp__sub_001CF0E0);

// thiscall Open(filename, format, device, allocate_textures, flags).
X86_FUNC(sub_001CF280) {
    const uint32_t self = ctx.ecx;
    const char* name = static_cast<const char*>(HostPtr(R32(ctx.esp + 4)));
    auto playback = std::make_unique<Playback>();
    std::string path = ResolvePath(name, 0);
    if (path.empty() || !playback->movie.Open(path)) {
        LOGE("native FMV: %s: %s; using guest decoder", name, playback->movie.Error().c_str());
        __imp__sub_001CF280(ctx, base);
        return;
    }
    // The native decoder owns no guest XMV object or file handles. Keep the
    // texture ownership fields compatible with the original Close routine.
    movies.erase(self);
    std::memset(HostPtr(self), 0, 0x48);
    std::memset(HostPtr(self + 0x50), 0, 0x40);
    W32(self + 0x60, 0xFFFFFFFF);
    W32(self + 4, playback->movie.Width());
    W32(self + 8, playback->movie.Height());
    W32(self + 0x40, 0xFFFFFFFF);
    W32(self + 0x44, 1);
    for (int i = 0; i < 2; ++i) {
        // XDK CreateTexture2(width,height,depth,levels,usage,format,type).
        playback->texture[i] = CallGuest(ctx, 0x2E7A10,
            {uint32_t(playback->movie.Width()), uint32_t(playback->movie.Height()), 1, 1, 0, 0x12, 3});
        W32(self + 0x28 + i * 4, playback->texture[i]);
        if (!playback->texture[i]) {
            __imp__sub_001CF0E0(ctx, base);
            // Close is a no-argument thiscall; restore Open's original stack.
            ctx.esp -= 4;
            ctx.eax = 0x8007000E;
            ctx.esp += 24;
            return;
        }
    }
    W32(self + 0x34, playback->texture[0]);
    W32(self + 0x38, playback->texture[1]);
    LOGI("native FMV: %s, %dx%d, %.3f fps", name, playback->movie.Width(), playback->movie.Height(),
         1.0 / playback->movie.FrameDuration());
    movies[self] = std::move(playback);
    ctx.eax = 0;
    ctx.esp += 24;
}

// thiscall Next(device): return a borrowed texture only when a new frame is due.
X86_FUNC(sub_001CF400) {
    uint32_t self = ctx.ecx;
    auto found = movies.find(self);
    if (found == movies.end()) { __imp__sub_001CF400(ctx, base); return; }
    Playback& p = *found->second;
    ctx.eax = 0;
    ctx.esp += 8;
    if (!p.pending) {
        if (!p.movie.Decode()) {
            if (!p.movie.Error().empty()) LOGE("native FMV: %s", p.movie.Error().c_str());
            W32(self + 0x44, 0);
            return;
        }
        p.pending = true;
    }
    if (!p.started) {
        p.start = Clock::now();
        p.started = true;
        p.movie.StartAudio();
    }
    double elapsed = std::chrono::duration<double>(Clock::now() - p.start).count();
    if (elapsed < p.movie.Pts()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
    uint32_t texture = p.texture[p.slot++ & 1];
    {
        std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
        gpu::TextureDesc desc;
        if (!gpu::Describe(texture, desc) || !desc.linear || desc.format != 0x12) {
            LOGE("native FMV: unexpected output texture format");
            W32(self + 0x44, 0);
            return;
        }
        auto* dst = static_cast<uint8_t*>(HostPtr(desc.data));
        for (int y = 0; y < p.movie.Height(); ++y)
            std::memcpy(dst + y * desc.pitch, p.movie.Pixels().data() + size_t(y) * p.movie.Width() * 4,
                        size_t(p.movie.Width()) * 4);
        gpu::MarkDirty(texture);
    }
    p.pending = false;
    W32(self + 0x40, p.frames++);
    W32(self + 0x3C, texture);
    ctx.eax = texture;
}

X86_FUNC(sub_001CF0E0) {
    auto it = movies.find(ctx.ecx);
    if (it != movies.end()) {
        LOGI("native FMV: closed after %u frames, %llu audio samples", it->second->frames,
             static_cast<unsigned long long>(it->second->movie.AudioSamples()));
        movies.erase(it); // stops and discards queued movie audio on skip, before freeing textures
    }
    __imp__sub_001CF0E0(ctx, base);
}

#endif
