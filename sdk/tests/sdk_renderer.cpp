// the render tests on the SDK's Vulkan renderer
#include "gpu/vulkan.h"
#include "render_backend.h"

namespace test {

std::unique_ptr<gpu::Backend> MakeRenderer(float scale) { return gpu::vk::CreateHeadlessRenderer(scale); }
std::unique_ptr<gpu::Backend> MakeMultisampledRenderer(uint32_t) { return nullptr; }
bool CountsPixels() { return true; }

}  // namespace test
