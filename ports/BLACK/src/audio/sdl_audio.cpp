// mixer output through an SDL3 audio stream (Linux: PipeWire, PulseAudio or ALSA, as SDL finds them), in place of
// XAudio2. The mixer renders 5.1 in SDL's own 5.1 order; a device without 5.1 speakers hears it folded to stereo.
#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "audio/fold.h"
#include "audio/output.h"
#include "core/log.h"

namespace audio {

namespace {

std::atomic<bool> g_surround{false};
std::atomic<bool> g_mono{false};
std::atomic<uint32_t> g_mode{kOutputAuto};
std::atomic<bool> g_mode_changed{false};
std::atomic<bool> g_paused{false};

struct Output {
    SDL_AudioStream* stream = nullptr;
    std::function<void(float*, uint32_t)> render;
    int device_channels = 2;
    uint32_t outputs = 2;  // channels handed to the stream: kChannels for 5.1, else the fold's
    std::vector<float> fold;
    std::vector<float> mix, out;
};
Output g_out;
std::mutex g_pause_lock;

bool WantSurround(uint32_t mode, int device_channels) {
    if (mode == kOutputAuto) return device_channels >= 6;
    return mode == kOutputSurround;
}

const char* OutputName() {
    if (g_mono) return "mono";
    return g_surround ? "5.1" : "stereo";
}

// the stream's input as the setting and the device have it; on the audio thread, or before the stream runs
void ApplyMode(Output& o) {
    uint32_t mode = g_mode;
    g_surround = WantSurround(mode, o.device_channels);
    g_mono = mode == kOutputMono;
    // a 5.1 mix on a device without 5.1 speakers is folded to stereo
    bool own_speakers = g_surround && o.device_channels >= 6;
    o.outputs = own_speakers ? kChannels : 2;
    o.fold = own_speakers ? std::vector<float>() : FoldToStereo(2, g_mono);
    SDL_AudioSpec spec{SDL_AUDIO_F32, int(o.outputs), int(kRate)};
    SDL_SetAudioStreamFormat(o.stream, &spec, nullptr);
    LOGI("audio: %s output, %d device channels", OutputName(), o.device_channels);
}

// SDL asks for more: whole 10 ms blocks of the mix until it has enough
void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int) {
    Output& o = *static_cast<Output*>(user);
    if (g_mode_changed.exchange(false)) ApplyMode(o);
    constexpr uint32_t kBlock = kRate / 100;
    o.mix.resize(size_t(kBlock) * kChannels);
    o.out.resize(size_t(kBlock) * kChannels);
    int bytes_per_frame = int(o.outputs * sizeof(float));
    for (int have = 0; have < additional; have += int(kBlock) * bytes_per_frame) {
        o.render(o.mix.data(), kBlock);
        const float* data = o.mix.data();
        if (!o.fold.empty()) {
            ApplyMatrix(o.fold, o.outputs, o.mix.data(), o.out.data(), kBlock);
            data = o.out.data();
        }
        SDL_PutAudioStreamData(stream, data, int(kBlock) * bytes_per_frame);
    }
}

}  // namespace

bool IsSurround() { return g_surround; }

bool IsMono() { return g_mono; }

void SetOutputMode(uint32_t mode) {
    if (g_mode.exchange(mode) != mode) g_mode_changed = true;
}

// paused, SDL stops asking: the mixer does not run and every voice and stream keeps its place
void SetPaused(bool paused) {
    std::lock_guard lock(g_pause_lock);
    if (g_paused.exchange(paused) == paused || !g_out.stream) return;
    if (paused) SDL_PauseAudioStreamDevice(g_out.stream);
    else SDL_ResumeAudioStreamDevice(g_out.stream);
}

void StartOutput(std::function<void(float*, uint32_t)> render) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOGE("audio: SDL audio unavailable, no sound: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec device{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &frames)) g_out.device_channels = device.channels;
    g_out.render = std::move(render);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    g_out.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, &g_out);
    if (!g_out.stream) {
        LOGE("audio: no output device, no sound: %s", SDL_GetError());
        return;
    }
    ApplyMode(g_out);
    g_mode_changed = false;
    LOGI("audio: SDL %s, device %s", SDL_GetCurrentAudioDriver(),
         SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(g_out.stream)) ? SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(g_out.stream)) : "?");
    std::lock_guard lock(g_pause_lock);
    if (!g_paused) SDL_ResumeAudioStreamDevice(g_out.stream);
}

}  // namespace audio
