// Ambient occlusion goes in before the alpha list: the world, the first person models and the characters are in the
// depth buffer, and leaves, glass, particles, shadows, decals, the bloom and the HUD land on top.
#include "common/options.h"
#include "d3d/occlusion.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kProjection = 0x297720;  // camera to screen, written only by the camera begin update 0x2B830

}  // namespace

// before the frame's call that sorts and draws list 2, the alpha blended meshes
void AmbientOcclusion(X86Context&, uint8_t*) {
    uint32_t mode = game::Opt().ambient_occlusion;
    if (mode) d3d::Occlusion(&G<float>(kProjection), mode == 2);
}
