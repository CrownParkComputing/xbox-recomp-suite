#include "check.h"
#include "sound/sound.h"

namespace audio { bool IsSurround() { return false; } bool IsMono() { return false; } }
namespace sound {
Place Locate(const Voice&);
Gains Levels(const Voice&, const Place&);
}

TEST("BLACK audio: nested output buses attenuate, mute and reject cycles") {
    using namespace sound;
    g_voices.clear();
    auto& voice = g_voices[1];
    auto& bus = g_voices[2];
    auto& master = g_voices[3];
    bus.volume = .25f;
    master.volume = .5f;
    CHECK_EQ(RouteOutput(1, 2), 0u);
    CHECK_EQ(RouteOutput(2, 3), 0u);
    auto levels = Levels(voice, Locate(voice));
    CHECK(std::abs(levels.out[0][audio::FrontLeft] - .125f) < 1e-6f);
    CHECK(std::abs(levels.out[0][audio::FrontRight] - .125f) < 1e-6f);
    CHECK(RouteOutput(3, 1) != 0);
    CHECK(RouteOutput(1, 99) != 0);
    CHECK_EQ(voice.output_buffer, 2u);
    bus.volume = 0;
    levels = Levels(voice, Locate(voice));
    for (auto& row : levels.out) for (float gain : row) CHECK_EQ(gain, 0.f);
    CHECK_EQ(RouteOutput(1, 0), 0u);
    levels = Levels(voice, Locate(voice));
    CHECK_EQ(levels.out[0][audio::FrontLeft], 1.f);
    g_voices.clear();
}

TEST("BLACK audio: a 3D bus applies distance attenuation to its inputs") {
    using namespace sound;
    g_voices.clear();
    auto& voice = g_voices[1];
    auto& bus = g_voices[2];
    bus.is3d = true;
    bus.p[2] = 10;
    CHECK_EQ(RouteOutput(1, 2), 0u);
    auto levels = Levels(voice, Locate(voice));
    CHECK(std::abs(levels.out[0][audio::FrontLeft] - .1f) < 1e-6f);
    bus.mode = 2;
    levels = Levels(voice, Locate(voice));
    CHECK_EQ(levels.out[0][audio::FrontLeft], 1.f);
    g_voices.clear();
}
