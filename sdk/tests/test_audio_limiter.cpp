#include "check.h"
#include "audio/limiter.h"

#include <array>
#include <limits>

TEST("audio: quiet stereo output passes unchanged") {
    audio::PeakLimiter limiter;
    float samples[] = {.125f, -.25f, .5f, -.75f};
    limiter.Process(samples, 2, 48000);
    CHECK_EQ(samples[0], .125f);
    CHECK_EQ(samples[1], -.25f);
    CHECK_EQ(samples[2], .5f);
    CHECK_EQ(samples[3], -.75f);
}

TEST("audio: summed engines retain their waveform and stereo balance without clipping") {
    audio::PeakLimiter limiter;
    std::array<float, 960> samples;
    const auto original = [&] {
        for (unsigned i = 0; i < 480; ++i) {
            samples[i * 2] = 6 * std::sin(i * .07f);
            samples[i * 2 + 1] = samples[i * 2] * .25f;
        }
        return samples;
    }();
    limiter.Process(samples.data(), 480, 48000);
    const float gain = samples[2] / original[2];
    CHECK(gain > 0 && gain < 1);
    for (unsigned i = 0; i < samples.size(); ++i) {
        CHECK(std::abs(samples[i]) <= .980001f);
        CHECK(std::abs(samples[i] - original[i] * gain) < 1e-6f);
    }
    float previous = 0;
    for (unsigned block = 0; block < 100; ++block) {
        samples.fill(.25f);
        limiter.Process(samples.data(), 480, 48000);
        CHECK(samples[0] >= previous && samples[0] <= .25f);
        previous = samples[0];
    }
    CHECK(previous > .249f);
}

TEST("audio: non-finite samples cannot poison the output block") {
    audio::PeakLimiter limiter;
    float samples[] = {std::numeric_limits<float>::infinity(), .25f,
                       std::numeric_limits<float>::quiet_NaN(), -.25f};
    limiter.Process(samples, 2, 48000);
    CHECK_EQ(samples[0], 0.f);
    CHECK_EQ(samples[1], .25f);
    CHECK_EQ(samples[2], 0.f);
    CHECK_EQ(samples[3], -.25f);
}

TEST("audio: linked surround limiter protects all six channels after folding") {
    audio::PeakLimiter limiter;
    float samples[] = {4.f, 2.f, 1.f, -4.f, .5f, -.5f};
    limiter.Process(samples, 1, 48000, 6);
    for (float sample : samples) CHECK(std::abs(sample) <= .980001f);
    CHECK(std::abs(samples[1] * 2 - samples[0]) < 1e-6f);
    CHECK(std::abs(samples[3] + samples[0]) < 1e-6f);
}
