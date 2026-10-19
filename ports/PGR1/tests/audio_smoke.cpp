// Check the actual public sound APIs and mixer used by this XDK 3925 title.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "audio/audio.h"
#include "xdk/hle.h"

namespace { std::function<void(float*, uint32_t)> render_audio; }
namespace audio {
void StartOutput(std::function<void(float*, uint32_t)> render) { render_audio = std::move(render); }
}

X86_EXTERN_FUNC(GUEST(DirectSoundCreateBuffer));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetBufferData));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_Play));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetVolume));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetHeadroom));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetPosition));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetMode));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetMaxDistance));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetOutputBuffer));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_Release));
#define CALL(name, ...) invoke(GUEST(name), __VA_ARGS__)

X86_FUNC(sub_000D2B6F) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "PGR audio regression: %s\n", message); std::exit(1); }
    };
    auto invoke = [&](X86Func* function, std::initializer_list<uint32_t> args) {
        const auto stack = ctx.esp;
        for (auto i = args.end(); i != args.begin();) { ctx.esp -= 4; W32(ctx.esp, *--i); }
        ctx.esp -= 4; W32(ctx.esp, 0xFFFFFFF0);
        function(ctx, base);
        check(ctx.esp == stack, "public API restores the guest stack");
        return ctx.eax;
    };
    const auto memory = mem::AllocSystem(4096);
    const auto format = memory, desc = memory + 32, out = memory + 64, pcm = memory + 256;
    W16(format, 1); W16(format + 2, 1); W32(format + 4, 48000); W32(format + 8, 96000);
    W16(format + 12, 2); W16(format + 14, 16);
    for (unsigned i = 0; i < 256; ++i) W16(pcm + i * 2, 8192);
    auto create = [&](uint32_t flags, bool samples) {
        W32(desc, 20); W32(desc + 4, flags); W32(desc + 8, 0);
        W32(desc + 12, samples ? format : 0); W32(desc + 16, 0);
        check(CALL(DirectSoundCreateBuffer, {desc, out}) == 0, "create voice or mix-in bus");
        const auto voice = R32(out);
        CALL(IDirectSoundBuffer_SetHeadroom, {voice, 0});
        if (samples) {
            CALL(IDirectSoundBuffer_SetBufferData, {voice, pcm, 512});
            CALL(IDirectSoundBuffer_Play, {voice, 0, 0, 1});
        }
        return voice;
    };
    auto levels = [&] {
        float result[16] = {};
        render_audio(result, 8);
        return std::pair(result[0], result[1]);
    };
    auto close = [&](float a, float b) { return std::abs(a - b) < 1e-5f; };
    const auto source = create(0, true), bus = create(0x2010, false), master = create(0x2000, false);
    check(close(levels().first, .25f), "unrouted PCM retains its original level");
    check(CALL(IDirectSoundBuffer_SetOutputBuffer, {source, bus}) == 0, "route engine to its 3D bus");
    CALL(IDirectSoundBuffer_SetVolume, {bus, uint32_t(-1200)});
    check(close(levels().first, .25f * std::pow(10.f, -1200.f / 2000.f)), "bus attenuation reaches engine samples");
    CALL(IDirectSoundBuffer_SetVolume, {bus, uint32_t(-10000)});
    check(close(levels().first, 0) && close(levels().second, 0), "muting the bus silences its engine");
    CALL(IDirectSoundBuffer_SetVolume, {bus, 0});
    uint32_t ten; const float distance = 10; std::memcpy(&ten, &distance, 4);
    CALL(IDirectSoundBuffer_SetPosition, {bus, ten, 0, 0, 0});
    check(close(levels().first, 0) && close(levels().second, .025f), "opponent bus applies distance and right panning");
    check(CALL(IDirectSoundBuffer_SetMode, {bus, 2, 0}) == 0, "set disabled 3D mode at its actual guest address");
    check(close(levels().first, .25f) && close(levels().second, .25f), "mode value is not interpreted as a maximum distance");
    check(CALL(IDirectSoundBuffer_SetMode, {bus, 9, 0}) != 0, "invalid sound mode is rejected");
    check(CALL(IDirectSoundBuffer_SetMaxDistance, {bus, 0, 0}) != 0, "zero maximum distance cannot produce infinite gains");
    check(CALL(IDirectSoundBuffer_SetMaxDistance, {bus, 0x7FC00000, 0}) != 0, "non-finite distance is rejected");
    CALL(IDirectSoundBuffer_SetMode, {bus, 0, 0});
    CALL(IDirectSoundBuffer_SetPosition, {bus, 0, 0, 0, 0});
    CALL(IDirectSoundBuffer_SetVolume, {master, uint32_t(-600)});
    check(CALL(IDirectSoundBuffer_SetOutputBuffer, {bus, master}) == 0, "route bus through master");
    check(close(levels().first, .25f * std::pow(10.f, -.3f)), "nested master gain reaches engines");
    check(CALL(IDirectSoundBuffer_SetOutputBuffer, {master, source}) != 0, "cyclic routing is rejected");
    check(CALL(IDirectSoundBuffer_SetOutputBuffer, {source, 0xFFFFFFFF}) != 0, "invalid routing is rejected");
    CALL(IDirectSoundBuffer_SetOutputBuffer, {bus, 0});
    CALL(IDirectSoundBuffer_SetVolume, {bus, uint32_t(-1200)});
    uint32_t engines[4] = {source};
    for (unsigned i = 1; i < 4; ++i) {
        engines[i] = create(0, true);
        CALL(IDirectSoundBuffer_SetOutputBuffer, {engines[i], bus});
    }
    check(close(levels().first, std::pow(10.f, -.6f)), "four cars retain their bus attenuation when summed");
    for (auto engine : engines) CALL(IDirectSoundBuffer_Release, {engine});
    CALL(IDirectSoundBuffer_Release, {bus});
    CALL(IDirectSoundBuffer_Release, {master});
    std::fprintf(stderr, "PGR audio regression: %u checks passed\n", checks);
    std::exit(0);
}
