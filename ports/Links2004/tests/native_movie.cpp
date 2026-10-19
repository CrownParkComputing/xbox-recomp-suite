#include "native_movie.h"
#include <chrono>
#include <cstdio>
#include <cmath>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    for (int i = 1; i < argc; ++i) {
        NativeMovie movie;
        auto start = std::chrono::steady_clock::now();
        if (!movie.Open(argv[i], false)) { std::fprintf(stderr, "%s\n", movie.Error().c_str()); return 1; }
        unsigned frames = 0;
        double previous = -1;
        bool nonzero = false;
        while (movie.Decode()) {
            if (!std::isfinite(movie.Pts()) || movie.Pts() < previous ||
                movie.Pixels().size() != size_t(movie.Width()) * movie.Height() * 4) return 1;
            previous = movie.Pts();
            for (size_t n = 0; n < movie.Pixels().size(); n += 4)
                nonzero |= movie.Pixels()[n] || movie.Pixels()[n + 1] || movie.Pixels()[n + 2];
            ++frames;
        }
        if (!movie.Error().empty() || !frames || !nonzero || !movie.AudioSamples()) {
            std::fprintf(stderr, "Invalid decode: %s\n", movie.Error().c_str()); return 1;
        }
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s: %u frames, last PTS %.3f s, %llu audio samples, %.1f decode fps\n", argv[i], frames,
            previous, static_cast<unsigned long long>(movie.AudioSamples()), frames / seconds);
    }
}
