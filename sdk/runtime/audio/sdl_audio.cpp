// mixer output through an SDL3 audio stream (Linux: PipeWire, PulseAudio or ALSA, as SDL finds them), in place of
// XAudio2 (xaudio2.cpp): interleaved stereo float frames, pulled in 10 ms blocks as the device asks
#include <SDL3/SDL.h>

#include <vector>
#include <algorithm>
#include "core/host_pause.h"
#include "audio/limiter.h"

#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {

struct Output {
    std::function<void(float*, uint32_t)> render;
    std::vector<float> block;
};
Output g_out;

void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int) {
    Output& o = *static_cast<Output*>(user);
    constexpr uint32_t kFrames = kRate / 100;
    constexpr int kBytes = int(kFrames * 2 * sizeof(float));
    o.block.resize(kFrames * 2);
    for (int have = 0; have < additional; have += kBytes) {
        if (host::Pause().Paused()) std::fill(o.block.begin(), o.block.end(), 0.0f);
        else o.render(o.block.data(), kFrames);
        SDL_PutAudioStreamData(stream, o.block.data(), kBytes);
    }
}

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOGE("audio: SDL audio unavailable, no sound: %s", SDL_GetError());
        return;
    }
    g_out.render = std::move(render);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, &g_out);
    if (!stream) {
        LOGE("audio: no output device, no sound: %s", SDL_GetError());
        return;
    }
    LOGI("audio: SDL %s", SDL_GetCurrentAudioDriver());
    SDL_ResumeAudioStreamDevice(stream);
}

}  // namespace audio
