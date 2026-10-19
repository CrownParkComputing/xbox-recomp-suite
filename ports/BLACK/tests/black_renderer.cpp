// the SDK's render tests on BLACK's Vulkan renderer (src/vk), with the game's options it reads
#include "common/options.h"
#include "render_backend.h"
#include "vk/vk_backend.h"

namespace game {
Options& Opt() {
    static Options o;
    return o;
}
}  // namespace game

namespace test {

// the renderer reads the MSAA setting in Init: each hook leaves it as its renderer wants it
std::unique_ptr<gpu::Backend> MakeRenderer(float scale) {
    game::Opt().msaa = 0;
    return gpu::vk::CreateHeadless(scale);
}

std::unique_ptr<gpu::Backend> MakeMultisampledRenderer(uint32_t samples) {
    game::Opt().msaa = samples;
    return gpu::vk::CreateHeadless();
}

// BLACK's renderer has no visibility test queries (its D3D12 one had none either)
bool CountsPixels() { return false; }

}  // namespace test
