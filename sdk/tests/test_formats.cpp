// the title's Direct3D 8 state as Vulkan takes it
#include <cstring>

#include <dxgiformat.h>

#include "check.h"
#include "gpu/vk_formats.h"

using namespace gpu;
using namespace gpu::vk;

namespace {

uint32_t FloatBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

}  // namespace

TEST("formats: blend factors keep colour and alpha apart as D3D defines them") {
    CHECK_EQ(Blend(0x302, false), VK_BLEND_FACTOR_SRC_ALPHA);
    CHECK_EQ(Blend(0x303, true), VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    CHECK_MSG(Blend(0x300, true) == VK_BLEND_FACTOR_SRC_ALPHA, "SRCCOLOR on the alpha channel reads source alpha");
    CHECK_EQ(Blend(0x306, false), VK_BLEND_FACTOR_DST_COLOR);
    CHECK_MSG(Blend(0x308, true) == VK_BLEND_FACTOR_ONE, "SRCALPHASAT's alpha factor is one");
    CHECK_MSG(Blend(0x8003, false) == VK_BLEND_FACTOR_CONSTANT_ALPHA, "constant alpha is the blend colour's alpha");
    CHECK_EQ(Blend(0x8001, false), VK_BLEND_FACTOR_CONSTANT_COLOR);
    CHECK_EQ(Blend(0x8002, true), VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA);
    CHECK_MSG(Blend(0x1234, false) == VK_BLEND_FACTOR_ONE, "an unknown factor is one");
}

TEST("formats: blend ops, compares and stencil ops follow the XDK numbering") {
    CHECK_EQ(BlendOp(0x800A), VK_BLEND_OP_SUBTRACT);
    CHECK_EQ(BlendOp(0x800B), VK_BLEND_OP_REVERSE_SUBTRACT);
    CHECK_EQ(BlendOp(0x8006), VK_BLEND_OP_ADD);
    CHECK_EQ(Compare(0x200), VK_COMPARE_OP_NEVER);
    CHECK_EQ(Compare(0x203), VK_COMPARE_OP_LESS_OR_EQUAL);
    CHECK_EQ(Compare(0x207), VK_COMPARE_OP_ALWAYS);
    CHECK_MSG(Compare(0) == VK_COMPARE_OP_ALWAYS, "an unset compare always passes");
    CHECK_EQ(StencilOp(0x1E01), VK_STENCIL_OP_REPLACE);
    CHECK_EQ(StencilOp(0x8507), VK_STENCIL_OP_INCREMENT_AND_WRAP);
    CHECK_EQ(StencilOp(0x1E00), VK_STENCIL_OP_KEEP);
}

TEST("formats: a shadow compare tests the reference against the texel, the console's order turned round") {
    CHECK_MSG(ShadowCompare(0x201) == VK_COMPARE_OP_GREATER, "texel < ref is ref > texel");
    CHECK_MSG(ShadowCompare(0x203) == VK_COMPARE_OP_GREATER_OR_EQUAL, "texel <= ref is ref >= texel");
    CHECK_EQ(ShadowCompare(0x202), VK_COMPARE_OP_EQUAL);
    CHECK_EQ(ShadowCompare(0x204), VK_COMPARE_OP_LESS);
}

TEST("formats: the console's ARGB write mask becomes Vulkan's RGBA bits") {
    CHECK_EQ(ColorWriteMask(0x01010101), VkColorComponentFlags(0xF));
    CHECK_EQ(ColorWriteMask(0x00010000), VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT));
    CHECK_EQ(ColorWriteMask(0x01000000), VkColorComponentFlags(VK_COLOR_COMPONENT_A_BIT));
    CHECK_EQ(ColorWriteMask(0x00000001), VkColorComponentFlags(VK_COLOR_COMPONENT_B_BIT));
}

TEST("formats: every vertex element format the assembler names has a Vulkan format") {
    for (uint32_t f : {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R16G16B16A16_SNORM,
                       DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM,
                       DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT,
                       DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R8_UNORM,
                       DXGI_FORMAT_B8G8R8A8_UNORM})
        CHECK_MSG(VertexFormat(f) != VK_FORMAT_UNDEFINED, std::to_string(f));
    CHECK_MSG(VertexFormat(DXGI_FORMAT_B8G8R8A8_UNORM) == VK_FORMAT_B8G8R8A8_UNORM, "D3DCOLOR keeps its byte order");
}

TEST("formats: texture uploads size rows by texel or by 4x4 block") {
    CHECK_EQ(HostFormat(Host::BC1), VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    CHECK_EQ(HostFormat(Host::BGRA8), VK_FORMAT_B8G8R8A8_UNORM);
    CHECK_EQ(RowBytes(VK_FORMAT_B8G8R8A8_UNORM, 10), 40u);
    CHECK_EQ(RowBytes(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 10), 24u);  // 3 blocks of 8
    CHECK_EQ(RowBytes(VK_FORMAT_BC3_UNORM_BLOCK, 4), 16u);
    CHECK_EQ(RowCount(VK_FORMAT_BC2_UNORM_BLOCK, 9), 3u);
    CHECK_EQ(RowCount(VK_FORMAT_B8G8R8A8_UNORM, 9), 9u);
}

TEST("formats: a stage's sampler follows its filters, addressing and LOD") {
    State s{};
    auto& t = s.tss[0];
    t[kTssMinFilter] = 2;  // linear
    t[kTssMagFilter] = 1;  // point
    t[kTssMipFilter] = 2;
    t[kTssAddressU] = 3;  // clamp
    t[kTssAddressV] = 2;  // mirror
    t[kTssMipLodBias] = FloatBits(-0.5f);
    t[kTssMaxMipLevel] = 2;
    SamplerDesc d = StageSampler(s, 0, false, 0, true, false);
    CHECK_EQ(d.min, VK_FILTER_LINEAR);
    CHECK_EQ(d.mag, VK_FILTER_NEAREST);
    CHECK_EQ(d.mip, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    CHECK_EQ(d.u, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK_EQ(d.v, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
    CHECK_EQ(d.lod_bias, -0.5f);
    CHECK_EQ(d.min_lod, 2.0f);
    CHECK_EQ(d.anisotropy, 1u);
    t[kTssMipFilter] = 0;
    CHECK_MSG(StageSampler(s, 0, false, 0, true, false).max_lod == 2.0f, "no mip filter pins the LOD");
}

TEST("formats: forced anisotropy applies only where the title filters across mip levels it has") {
    State s{};
    auto& t = s.tss[1];
    t[kTssMinFilter] = 2;
    t[kTssMagFilter] = 2;
    t[kTssMipFilter] = 2;
    CHECK_EQ(StageSampler(s, 1, false, 16, true, false).anisotropy, 16u);
    CHECK_MSG(StageSampler(s, 1, false, 16, false, false).anisotropy == 1u, "a texture without levels stays as asked");
    CHECK_MSG(StageSampler(s, 1, false, 16, true, true).anisotropy == 1u, "a shadow lookup is never anisotropic");
    t[kTssMipFilter] = 1;
    CHECK_MSG(StageSampler(s, 1, false, 16, true, false).anisotropy == 16u, "point mips are filtered across too, as on D3D12");
    t[kTssMipFilter] = 0;
    CHECK_MSG(StageSampler(s, 1, false, 16, true, false).anisotropy == 1u, "no mip filter: no levels are read, none forced");
    t[kTssMipFilter] = 2;
    t[kTssMinFilter] = 3;
    t[kTssMaxAnisotropy] = 4;
    CHECK_MSG(StageSampler(s, 1, false, 0, true, false).anisotropy == 4u, "the title's own anisotropy");
}

TEST("formats: wrap is read as clamp only for draws whose coordinates lie in 0..1") {
    State s{};
    s.tss[0][kTssAddressU] = 1;
    s.tss[0][kTssAddressV] = 1;
    CHECK_EQ(StageSampler(s, 0, false, 0, false, false).u, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    CHECK_EQ(StageSampler(s, 0, true, 0, false, false).u, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK_EQ(StageSampler(s, 0, true, 0, false, false).v, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

TEST("formats: a shadow sampler compares, filtered as the title filters, never mipped") {
    State s{};
    s.tss[2][kTssMinFilter] = 2;
    s.tss[2][kTssMipFilter] = 2;
    s.rs[kRsShadowFunc] = 0x203;
    SamplerDesc d = StageSampler(s, 2, false, 0, true, true);
    CHECK_EQ(d.compare, 1u);
    CHECK_EQ(d.compare_op, VK_COMPARE_OP_GREATER_OR_EQUAL);
    CHECK_EQ(d.min, VK_FILTER_LINEAR);
    CHECK_EQ(d.mip, VK_SAMPLER_MIPMAP_MODE_NEAREST);
}

TEST("formats: the border colour is the stage's ARGB as RGBA floats") {
    State s{};
    s.tss[3][kTssBorderColor] = 0x80FF4000;
    SamplerDesc d = StageSampler(s, 3, false, 0, false, false);
    CHECK_EQ(d.border[0], 1.0f);
    CHECK_EQ(d.border[1], 64.0f / 255);
    CHECK_EQ(d.border[2], 0.0f);
    CHECK_EQ(d.border[3], 128.0f / 255);
}
