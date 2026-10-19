// Exercise the actual software mixer with XACT's public voice APIs.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "audio/audio.h"
#include "xdk/hle.h"
#include "xdk/dsound_voice.h"

namespace { std::function<void(float*, uint32_t)> render_audio; }
namespace audio {
void StartOutput(std::function<void(float*, uint32_t)> render) { render_audio = render; }
}

X86_EXTERN_FUNC(GUEST(DirectSoundCreateBuffer));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetBufferData));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_Play));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetCurrentPosition));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetVolume));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_Release));
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetFrequency));
#define CALL(name, ...) invoke(GUEST(name), __VA_ARGS__)

X86_FUNC(sub_001B2594) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "audio smoke failed: %s\n", message); std::exit(1); }
    };
    auto invoke = [&](X86Func* function, std::initializer_list<uint32_t> args) {
        const auto stack = ctx.esp;
        for (auto i = args.end(); i != args.begin();) { ctx.esp -= 4; W32(ctx.esp, *--i); }
        ctx.esp -= 4; W32(ctx.esp, 0xFFFFFFF0);
        function(ctx, base);
        check(ctx.esp == stack, "public audio API restores stack");
        return ctx.eax;
    };
    const uint32_t memory = mem::AllocSystem(4096), fmt = memory, desc = memory + 32;
    const uint32_t out = memory + 64, pcm = memory + 256, spatial = memory + 96, copy = memory + 160;
    W16(fmt, 1); W16(fmt + 2, 1); W32(fmt + 4, 48000); W32(fmt + 8, 96000);
    W16(fmt + 12, 2); W16(fmt + 14, 16);
    W32(desc, 20); W32(desc + 4, 0x10); W32(desc + 8, 0); W32(desc + 12, fmt); W32(desc + 16, 0);
    check(CALL(DirectSoundCreateBuffer, {desc, out}) == 0, "create host voice");
    const uint32_t voice = R32(out);
    for (unsigned i = 0; i < 256; ++i) W16(pcm + i * 2, i * 64);
    check(CALL(IDirectSoundBuffer_SetBufferData, {voice, pcm, 512}) == 0, "set PCM ramp");
    check(CALL(IDirectSoundBuffer_Play, {voice, 0, 0, 1}) == 0, "play PCM loop");
    auto sample = [&] {
        CALL(IDirectSoundBuffer_SetCurrentPosition, {voice, 0});
        float samples[16] = {};
        render_audio(samples, 8);
        return std::pair(samples[2], samples[3]);
    };
    auto baseline = sample();
    check(baseline.first > 0 && std::abs(baseline.first - baseline.second) < 1e-6f, "mono renders on both channels");
    check(dsound::Use3DVoiceData(voice, true) == 0, "enable calculated spatial data");
    W32(spatial, 0x41); W32(spatial + 4, uint32_t(-600));
    float angle = 90; std::memcpy(HostPtr(spatial + 0x28), &angle, 4);
    check(dsound::Set3DVoiceData(voice, spatial) == 0, "apply attenuation and azimuth");
    auto placed = sample();
    check(std::abs(placed.first) < 1e-6f && std::abs(placed.second / baseline.second - 0.501187f) < .001f,
          "precomputed attenuation and right panning reach the mixer");
    check(dsound::GetVoiceProperties(voice, copy) == 0 && R32(copy) == 2 && R32(copy + 4) == 0 && R32(copy + 12) == 1 && R32(copy + 0x48) == uint32_t(-600),
          "voice properties report mix bins and spatial attenuation");
    W32(spatial, 1); W32(spatial + 4, 0);
    dsound::Set3DVoiceData(voice, spatial);
    check(std::abs(sample().second / baseline.second - 1) < .001f && std::abs(sample().first) < 1e-6f,
          "partial update preserves azimuth");
    dsound::Use3DVoiceData(voice, false);
    dsound::SetPitch(voice, 4096);
    check(std::abs(sample().first / baseline.first - 2) < .001f, "one octave pitch doubles sample rate");
    CALL(IDirectSoundBuffer_SetFrequency, {voice, 48000});
    check(std::abs(sample().first / baseline.first - 1) < .001f, "explicit frequency replaces previous pitch");
    dsound::SetPitch(voice, 0);
    CALL(DirectSoundCreateBuffer, {desc, out});
    const uint32_t bus = R32(out);
    CALL(IDirectSoundBuffer_SetVolume, {bus, uint32_t(-600)});
    check(dsound::SetOutputBuffer(voice, bus) == 0, "route voice through mix-in bus");
    check(std::abs(sample().first / baseline.first - 0.501187f) < .001f, "bus volume affects routed PCM");
    check(dsound::SetOutputBuffer(bus, voice) != 0, "reject cyclic routing");
    check(dsound::SetOutputBuffer(voice, 0) == 0 && std::abs(sample().first / baseline.first - 1) < .001f,
          "disconnect bus restores direct playback");
    check(dsound::Set3DVoiceData(0, spatial) != 0 && dsound::SetPitch(0, 0) != 0, "invalid voices return errors");
    CALL(IDirectSoundBuffer_Release, {bus});
    CALL(IDirectSoundBuffer_Release, {voice});
    std::fprintf(stderr, "audio smoke: %u checks passed\n", checks);
    std::exit(0);
}
