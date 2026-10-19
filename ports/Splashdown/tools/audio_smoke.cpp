// Link into the headless harness with -e tools/audio_smoke.cpp. It replaces the
// game entry point, exercising the actual mapped audio APIs on initialized guest memory.
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "kernel/memory.h"
#include "xdk/hle.h"

X86_FUNC(sub_0013A80B) {
    using namespace kernel;
    (void)base;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "audio smoke failed: %s\n", what);
            std::exit(1);
        }
    };
    const uint32_t memory = mem::AllocSystem(512);
    std::memset(HostPtr(memory), 0, 512);
    const uint32_t format = memory, desc = memory + 32, out = memory + 64;
    const uint32_t status = memory + 80, packet = memory + 96;
    W32(format, 1 | (1 << 16));  // PCM, mono
    W32(format + 4, 48000);
    W32(format + 8, 96000);
    W32(format + 12, 2 | (16 << 16));  // block alignment, bits per sample
    W32(desc, 0x10);  // 3D stream, one pending packet
    W32(desc + 4, 1);
    W32(desc + 8, format);
    check(CallGuest(ctx, SYM(DirectSoundCreateStream), {desc, out}) == 0, "create stream");
    const uint32_t stream = R32(out);
    check(stream != 0, "stream object");
    check(CallGuest(ctx, SYM(CDirectSoundStream_GetStatus), {stream, status}) == 0 && R32(status) == 1,
          "empty stream accepts input");
    check(CallGuest(ctx, SYM(CDirectSoundStream_Pause), {stream, 1}) == 0, "pause stream");
    W32(packet, memory + 160);
    W32(packet + 4, 64);
    check(CallGuest(ctx, SYM(CDirectSoundStream_Process), {stream, packet, 0}) == 0, "submit packet");
    check(CallGuest(ctx, SYM(CDirectSoundStream_GetStatus), {stream, status}) == 0 && R32(status) == 0,
          "full paused stream refuses input");
    check(CallGuest(ctx, SYM(CDirectSoundStream_Flush), {stream}) == 0, "flush stream");
    check(CallGuest(ctx, SYM(CDirectSoundStream_GetStatus), {stream, status}) == 0 && R32(status) == 1,
          "flushed stream accepts input");

    std::memset(HostPtr(desc), 0, 24);
    W32(desc, 20);
    W32(desc + 4, 0x10);
    W32(desc + 8, 64);
    W32(desc + 12, format);
    check(CallGuest(ctx, SYM(IDirectSound_CreateSoundBuffer), {0, desc, out, 0}) == 0, "create 3D buffer");
    const uint32_t buffer = R32(out);
    check(buffer != 0, "buffer object");
    struct Setter { uint32_t function, object; const char* name; };
    const Setter setters[] = {
        {SYM(IDirectSoundBuffer_SetDistanceFactor), buffer, "buffer distance factor"},
        {SYM(IDirectSoundBuffer_SetDopplerFactor), buffer, "buffer Doppler factor"},
        {SYM(IDirectSoundBuffer_SetRolloffFactor), buffer, "buffer rolloff factor"},
        {SYM(CDirectSoundStream_SetDistanceFactor), stream, "stream distance factor"},
        {SYM(CDirectSoundStream_SetDopplerFactor), stream, "stream Doppler factor"},
        {SYM(CDirectSoundStream_SetRolloffFactor), stream, "stream rolloff factor"},
    };
    for (const auto& s : setters) {
        check(CallGuest(ctx, s.function, {s.object, std::bit_cast<uint32_t>(0.5f), 1}) == 0, s.name);
        check(CallGuest(ctx, s.function, {0, std::bit_cast<uint32_t>(0.5f), 1}) == 0x80004005u,
              "invalid voice returns E_FAIL");
    }
    check(CallGuest(ctx, SYM(IDirectSoundBuffer_SetMode), {buffer, 2, 1}) == 0, "buffer 3D mode");
    check(CallGuest(ctx, SYM(CDirectSoundStream_SetMode), {stream, 2, 1}) == 0, "stream 3D mode");
    check(CallGuest(ctx, SYM(IDirectSoundBuffer_SetMode), {0, 2, 1}) == 0x80004005u, "invalid buffer mode");
    check(CallGuest(ctx, SYM(CDirectSoundStream_SetMode), {0, 2, 1}) == 0x80004005u, "invalid stream mode");
    check(CallGuest(ctx, SYM(CDirectSoundStream_Release), {stream}) == 0, "release stream");
    check(CallGuest(ctx, SYM(IDirectSoundBuffer_Release), {buffer}) == 0, "release buffer");
    std::fprintf(stderr, "audio smoke: %u checks passed\n", checks);
    std::exit(0);
}
