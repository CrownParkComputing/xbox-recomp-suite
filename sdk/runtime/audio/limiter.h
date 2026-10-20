#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace audio {

// The console's DSP dynamics are not reproduced. Limit the completed output
// block before output instead of flat-topping every overloaded engine sample.
// All channels share a gain, preserving pan and the relative voice levels.
class PeakLimiter {
public:
    void Process(float* samples, uint32_t frames, uint32_t rate, uint32_t channels = 2) {
        float peak = 0;
        for (uint32_t i = 0; i < frames * channels; ++i) {
            if (!std::isfinite(samples[i])) samples[i] = 0;
            peak = std::max(peak, std::abs(samples[i]));
        }
        constexpr float ceiling = .98f, release_seconds = .1f;
        const float target = peak > ceiling ? ceiling / peak : 1;
        const float recovered = 1 - (1 - gain_) * std::exp(-float(frames) / (release_seconds * rate));
        gain_ = std::min(target, recovered);
        for (uint32_t i = 0; i < frames * channels; ++i) samples[i] *= gain_;
    }

private:
    float gain_ = 1;
};

}  // namespace audio
