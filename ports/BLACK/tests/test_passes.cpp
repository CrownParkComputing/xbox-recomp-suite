// BLACK's own full screen passes (src/render/pass_shaders.h, SMAA) through DXC
#include <string>
#include <vector>

#include "check.h"
#include "d3d/smaa_shader.h"
#include "gpu/vk_shaders.h"
#include "render/pass_shaders.h"

using namespace gpu;
using gpu::vk::Stage;

namespace {

bool Compiles(const std::string& src, const char* entry, Stage stage) {
    std::string e;
    return !gpu::vk::CompileHlsl(src, entry, stage, e).empty();
}

}  // namespace

TEST("black: the renderer's own passes compile") {
    CHECK(Compiles(kBlitHlsl, "vs", Stage::Vertex));
    CHECK(Compiles(kBlitHlsl, "psmain", Stage::Pixel));
    CHECK(Compiles(kHdrHlsl, "hdr10", Stage::Pixel));
    std::string smaa = std::string(kSmaaConfig) + kSmaaHlsl + kSmaaPasses;
    for (const char* e : {"vs_edges", "vs_weights", "vs_blend"}) CHECK_MSG(Compiles(smaa, e, Stage::Vertex), e);
    for (const char* e : {"smaa_edges", "smaa_weights", "smaa_blend"}) CHECK_MSG(Compiles(smaa, e, Stage::Pixel), e);
    for (const char* ms : {"", "#define MS 1\n"}) {
        CHECK(Compiles(std::string(ms) + kDepthHlsl, "psmain", Stage::Pixel));
        for (const char* e : {"occlusion", "gtao", "occlusion_blur", "occlusion_apply"})
            CHECK_MSG(Compiles(std::string(ms) + kAoHlsl, e, Stage::Pixel), e);
    }
}
