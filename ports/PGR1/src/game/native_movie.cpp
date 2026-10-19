#ifndef _WIN32
#include "native_movie.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

struct NativeMovie::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext *video = nullptr, *audio = nullptr;
    AVFrame *frame = av_frame_alloc(), *sound = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    SwsContext* scale = nullptr;
    SwrContext* resample = nullptr;
    SDL_AudioStream* output = nullptr;
    int vi = -1, ai = -1;
    bool eof = false, started = false;
    double pts = 0, duration = 1001.0 / 30000.0, origin = 0;
    uint64_t frames = 0, samples = 0;
    std::vector<uint8_t> pixels;
    std::vector<float> pcm;
    std::string error;

    ~Impl() {
        if (output) SDL_DestroyAudioStream(output);
        swr_free(&resample);
        sws_freeContext(scale);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&sound);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&format);
    }
    bool Fail(const char* where, int code) {
        char buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, buf, sizeof(buf));
        error = std::string(where) + ": " + buf;
        return false;
    }
    bool Codec(int index, AVCodecContext*& out) {
        const auto* params = format->streams[index]->codecpar;
        const auto* codec = avcodec_find_decoder(params->codec_id);
        if (!codec) { error = "Decoder unavailable"; return false; }
        out = avcodec_alloc_context3(codec);
        if (!out) return Fail("Allocate decoder", AVERROR(ENOMEM));
        int r = avcodec_parameters_to_context(out, params);
        if (r < 0) return Fail("Decoder parameters", r);
        // Slice threading avoids extra frames of latency and unbounded host threads.
        out->thread_count = 2;
        out->thread_type = FF_THREAD_SLICE;
        r = avcodec_open2(out, codec, nullptr);
        return r >= 0 || Fail("Open decoder", r);
    }
    void AudioPacket(const AVPacket* pk) {
        if (!audio) return;
        int r = avcodec_send_packet(audio, pk);
        if (r < 0 && r != AVERROR_EOF) { Fail("Audio packet", r); return; }
        while ((r = avcodec_receive_frame(audio, sound)) >= 0) {
            int count = swr_get_out_samples(resample, sound->nb_samples);
            pcm.resize(size_t(count) * 2);
            uint8_t* dst = reinterpret_cast<uint8_t*>(pcm.data());
            r = swr_convert(resample, &dst, count,
                            const_cast<const uint8_t**>(sound->extended_data), sound->nb_samples);
            if (r < 0) { Fail("Resample audio", r); break; }
            samples += r;
            if (output && r && !SDL_PutAudioStreamData(output, pcm.data(), r * 2 * sizeof(float)))
                error = SDL_GetError();
            av_frame_unref(sound);
        }
    }
};

NativeMovie::NativeMovie() : p(std::make_unique<Impl>()) {}
NativeMovie::~NativeMovie() = default;
bool NativeMovie::Open(const std::string& path, bool enable_audio) {
    int r = avformat_open_input(&p->format, path.c_str(), nullptr, nullptr);
    if (r < 0) return p->Fail("Open movie", r);
    r = avformat_find_stream_info(p->format, nullptr);
    if (r < 0) return p->Fail("Read movie streams", r);
    p->vi = av_find_best_stream(p->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (p->vi < 0) return p->Fail("Find video", p->vi);
    if (!p->Codec(p->vi, p->video)) return false;
    if (Width() <= 0 || Height() <= 0 || Width() > 4096 || Height() > 4096) {
        p->error = "Unsupported movie dimensions"; return false;
    }
    auto rate = av_guess_frame_rate(p->format, p->format->streams[p->vi], nullptr);
    if (rate.num > 0 && rate.den > 0) p->duration = av_q2d(av_inv_q(rate));
    p->pixels.resize(size_t(Width()) * Height() * 4);
    p->ai = av_find_best_stream(p->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (p->ai >= 0) {
        if (!p->Codec(p->ai, p->audio)) return false;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        r = swr_alloc_set_opts2(&p->resample, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                               &p->audio->ch_layout, p->audio->sample_fmt, p->audio->sample_rate, 0, nullptr);
        if (r < 0 || (r = swr_init(p->resample)) < 0) return p->Fail("Initialize audio conversion", r);
        if (enable_audio) {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
            SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
            p->output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!p->output) { p->error = SDL_GetError(); return false; }
        }
    }
    return true;
}
bool NativeMovie::Decode() {
    for (;;) {
        int r = avcodec_receive_frame(p->video, p->frame);
        if (r >= 0) {
            int64_t stamp = p->frame->best_effort_timestamp;
            double pts = stamp == AV_NOPTS_VALUE ? p->frames * p->duration
                : stamp * av_q2d(p->format->streams[p->vi]->time_base);
            if (!p->frames) p->origin = pts;
            p->pts = std::max(0.0, pts - p->origin);
            ++p->frames;
            p->scale = sws_getCachedContext(p->scale, Width(), Height(), AVPixelFormat(p->frame->format),
                Width(), Height(), AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!p->scale) { p->error = "Video conversion allocation failed"; return false; }
            uint8_t* dst[] = {p->pixels.data(), nullptr, nullptr, nullptr};
            int stride[] = {Width() * 4, 0, 0, 0};
            r = sws_scale(p->scale, p->frame->data, p->frame->linesize, 0, Height(), dst, stride);
            av_frame_unref(p->frame);
            if (r != Height()) { p->error = "Incomplete converted frame"; return false; }
            return true;
        }
        if (r == AVERROR_EOF) return false;
        if (r != AVERROR(EAGAIN)) return p->Fail("Decode video", r);
        if (p->eof) return false;
        r = av_read_frame(p->format, p->packet);
        if (r < 0) {
            p->eof = true;
            if (r != AVERROR_EOF) return p->Fail("Read movie packet", r);
            avcodec_send_packet(p->video, nullptr);
            p->AudioPacket(nullptr);
            continue;
        }
        if (p->packet->stream_index == p->vi) {
            r = avcodec_send_packet(p->video, p->packet);
            av_packet_unref(p->packet);
            if (r < 0) return p->Fail("Video packet", r);
        } else {
            if (p->packet->stream_index == p->ai) p->AudioPacket(p->packet);
            av_packet_unref(p->packet);
        }
    }
}
void NativeMovie::StartAudio() {
    if (!p->started && p->output) SDL_ResumeAudioStreamDevice(p->output);
    p->started = true;
}
int NativeMovie::Width() const { return p->video ? p->video->width : 0; }
int NativeMovie::Height() const { return p->video ? p->video->height : 0; }
double NativeMovie::Pts() const { return p->pts; }
double NativeMovie::FrameDuration() const { return p->duration; }
const std::vector<uint8_t>& NativeMovie::Pixels() const { return p->pixels; }
uint64_t NativeMovie::AudioSamples() const { return p->samples; }
const std::string& NativeMovie::Error() const { return p->error; }

#endif
