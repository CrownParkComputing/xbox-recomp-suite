#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Native decoder for the original XMV files. Playback timing belongs to the
// title adapter; Decode reads one video frame and queues accompanying audio.
class NativeMovie {
public:
    NativeMovie();
    ~NativeMovie();
    bool Open(const std::string& path, bool audio = true);
    bool Decode();
    void StartAudio();
    int Width() const;
    int Height() const;
    double Pts() const;
    double FrameDuration() const;
    const std::vector<uint8_t>& Pixels() const;
    uint64_t AudioSamples() const;
    const std::string& Error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
