#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include "audio/audio.h"
#include "core/log.h"

namespace audio {
void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        std::vector<float> samples(kRate / 100 * 2);
        unsigned ticks = 0;
        float peak = 0;
        double energy = 0;
        for (;;) {
            render(samples.data(), kRate / 100);
            for (float value : samples) {
                peak = std::max(peak, std::abs(value));
                energy += value * value;
            }
            if (++ticks % 100 == 0) {
                LOGI("audio probe: second %u peak %.5g rms %.5g", ticks / 100, peak,
                     std::sqrt(energy / (100 * samples.size())));
                peak = 0;
                energy = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}
}
