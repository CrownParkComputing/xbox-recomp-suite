#pragma once
// the renderer a test executable runs the render tests on: the SDK's (sdk_renderer.cpp), or a game's own
#include <cstdint>
#include <memory>

#include "gpu/backend.h"

namespace test {

// headless, its targets at scale times the console's size
std::unique_ptr<gpu::Backend> MakeRenderer(float scale);
// headless with the scene multisampled, null when the renderer has no MSAA
std::unique_ptr<gpu::Backend> MakeMultisampledRenderer(uint32_t samples);
// the renderer answers visibility tests (GetReport) with occlusion query counts
bool CountsPixels();

}  // namespace test
