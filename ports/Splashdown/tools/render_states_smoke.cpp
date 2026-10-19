// Exercise 4531's retail state numbering against the actual host API and draw
// synchronization, including the inline stores used by Splashdown's particles.
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kernel/memory.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

X86_FUNC(sub_0013A80B) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "render states smoke failed: %s\n", what); std::exit(1); }
    };
    const uint32_t memory = mem::AllocSystem(128);
    std::memset(HostPtr(memory), 0, 128);
    W32(memory, 64); W32(memory + 4, 64); W32(memory + 8, 6); W32(memory + 12, 1);
    check(CallGuest(ctx, SYM(Direct3D_CreateDevice), {0, 1, 0, 0, memory, memory + 64}) == 0, "create device");
    auto set = [&](uint32_t state, uint32_t value) {
        CallGuest(ctx, SYM(D3DDevice_SetRenderStateNotInline), {state, value});
    };
    constexpr uint32_t table = SYM(D3D_RenderState);
    set(109, 1);
    check(d3d::State().rs[gpu::kRsPointSpriteEnable] == 1, "4531 sprite enable reaches renderer");
    check(R32(table + 109 * 4) == 1, "setter mirrors retail sprite table");
    set(118, 0x1234);
    check(d3d::State().rs[gpu::kRsPsTextureModes] == 0x1234, "complex states start at 118");
    set(140, 1);
    check(d3d::State().rs[gpu::kRsDxt1NoiseEnable] == 1, "complex table skips absent SampleAlpha");
    // The real game writes these directly, bypassing SetRenderStateNotInline.
    W32(table + 83 * 4, 1);
    W32(table + 107 * 4, std::bit_cast<uint32_t>(12.5f));
    W32(table + 108 * 4, std::bit_cast<uint32_t>(2.0f));
    W32(table + 109 * 4, 0);
    W32(table + 110 * 4, 1);
    W32(table + 114 * 4, std::bit_cast<uint32_t>(32.0f));
    CallGuest(ctx, SYM(D3DDevice_DrawVertices), {1, 0, 0});
    const auto& s = d3d::State();
    check(s.rs[gpu::kRsFogEnable] == 1, "deferred fog table starts at 83");
    check(s.rs[gpu::kRsPointSize] == std::bit_cast<uint32_t>(12.5f), "inline point size is synchronized");
    check(s.rs[gpu::kRsPointSizeMin] == std::bit_cast<uint32_t>(2.0f), "inline minimum size is synchronized");
    check(s.rs[gpu::kRsPointSpriteEnable] == 0, "inline sprite disable is synchronized");
    check(s.rs[gpu::kRsPointScaleEnable] == 1, "inline program-size flag is synchronized");
    check(s.rs[gpu::kRsPointSizeMax] == std::bit_cast<uint32_t>(32.0f), "inline maximum size is synchronized");
    std::fprintf(stderr, "render states smoke: %u checks passed\n", checks);
    std::exit(0);
}
