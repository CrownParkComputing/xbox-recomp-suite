// the 5.1 mix folded for stereo and mono devices, as both outputs apply it
#include <cmath>
#include <vector>

#include "audio/fold.h"
#include "audio/output.h"
#include "check.h"

namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}  // namespace

TEST("audio: stereo hears front, centre at -3 dB and back on its side, and no sub") {
    std::vector<float> m = audio::FoldToStereo(2, false);
    REQUIRE(m.size() == 2 * audio::kChannels);
    float in[audio::kChannels] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};  // FL FR C LFE BL BR
    float out[2];
    audio::ApplyMatrix(m, 2, in, out, 1);
    CHECK_MSG(Near(out[0], 0.1f + 0.707f * 0.3f + 0.5f), "left");
    CHECK_MSG(Near(out[1], 0.2f + 0.707f * 0.3f + 0.6f), "right");
}

TEST("audio: mono hears the average of the stereo fold on both speakers") {
    std::vector<float> m = audio::FoldToStereo(2, true);
    float in[audio::kChannels] = {1, 0, 0, 0, 0, 0};
    float out[2];
    audio::ApplyMatrix(m, 2, in, out, 1);
    CHECK(Near(out[0], 0.5f));
    CHECK(Near(out[1], 0.5f));
}

TEST("audio: frames are folded one after another") {
    std::vector<float> m = audio::FoldToStereo(2, false);
    std::vector<float> in(3 * audio::kChannels, 0.0f), out(6, -1.0f);
    for (int f = 0; f < 3; f++) in[size_t(f) * audio::kChannels] = float(f + 1);  // front left only
    audio::ApplyMatrix(m, 2, in.data(), out.data(), 3);
    for (int f = 0; f < 3; f++) {
        CHECK(Near(out[size_t(f) * 2], float(f + 1)));
        CHECK(Near(out[size_t(f) * 2 + 1], 0.0f));
    }
}
