// Link into the headless harness with -e tools/indexed_draw_smoke.cpp and run with REF=1.
// Verify an indexed caller-owned vertex draw by reading its actual framebuffer pixels.
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gpu/backend.h"
#include "kernel/memory.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

X86_FUNC(sub_0013A80B) {
    using namespace kernel;
    (void)base;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "indexed draw smoke failed: %s\n", what);
            std::exit(1);
        }
    };
    const uint32_t memory = mem::AllocSystem(512), out = memory + 64;
    std::memset(HostPtr(memory), 0, 512);
    W32(memory, 64);
    W32(memory + 4, 64);
    W32(memory + 8, 6);  // A8R8G8B8
    W32(memory + 12, 1);
    check(CallGuest(ctx, SYM(Direct3D_CreateDevice), {0, 1, 0, 0, memory, out}) == 0, "create device");
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});  // REF=1 starts drawing the next frame
    CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {0});
    CallGuest(ctx, SYM(D3DDevice_SetVertexShader), {0x44});  // XYZRHW + diffuse
    CallGuest(ctx, SYM(D3DDevice_SetRenderState_ZEnable), {0});
    auto& state = d3d::State();
    state.simple[gpu::kMColorWriteEnable >> 2] = 0x01010101;
    state.simple[gpu::kMBlendEnable >> 2] = 0;
    state.simple[gpu::kMAlphaTestEnable >> 2] = 0;
    W32(SYM(D3D_RenderState) + gpu::kRsFogEnable * 4, 0);
    W32(SYM(D3D_RenderState) + gpu::kRsLighting * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssColorOp * 4, 2);  // SELECTARG1, diffuse
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssColorArg1 * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssAlphaOp * 4, 2);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssAlphaArg1 * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + (32 + gpu::kTssColorOp) * 4, 1);
    CallGuest(ctx, SYM(D3DDevice_Clear), {0, 0, gpu::kClearTarget, 0xFF0000FF, std::bit_cast<uint32_t>(1.0f), 0});

    struct Vertex { float x, y, z, rhw; uint32_t color; };
    const Vertex vertices[] = {
        {60, 60, 0.5f, 1, 0xFF00FF00},  // unreferenced: must not affect the draw
        {8, 8, 0.5f, 1, 0xFFFF0000},
        {8, 56, 0.5f, 1, 0xFFFF0000},
        {56, 8, 0.5f, 1, 0xFFFF0000},
    };
    const uint16_t indices[] = {1, 3, 2};
    const uint32_t vertex_data = memory + 128, index_data = memory + 256;
    std::memcpy(HostPtr(vertex_data), vertices, sizeof(vertices));
    std::memcpy(HostPtr(index_data), indices, sizeof(indices));
    check(CallGuest(ctx, SYM(D3DDevice_DrawIndexedVerticesUP), {5, 3, index_data, vertex_data, sizeof(Vertex)}) == 0,
          "indexed draw");
    const uint32_t pixels = mem::VirtualFromPhysical(state.rt.data);
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, SYM(D3D_KickOffAndWaitForIdle), {});
    check(R32(pixels + (16 * 64 + 16) * 4) == 0xFFFF0000, "indexed triangle is red");
    check(R32(pixels + (56 * 64 + 56) * 4) == 0xFF0000FF, "uncovered framebuffer stays blue");

    const uint32_t vb = memory + 288, push = memory + 320;
    const uint32_t commands = mem::AllocContiguous(4096, 0, 0xFFFFFFFF, 4096);
    Vertex green[4];
    std::memcpy(green, vertices, sizeof(green));
    for (auto& v : green) v.color = 0xFF00FF00;
    std::memcpy(HostPtr(commands + 128), green, sizeof(green));
    W32(vb, 1);
    W32(vb + 4, mem::PhysicalFromVirtual(commands + 128));
    CallGuest(ctx, SYM(D3DDevice_SetStreamSource), {0, vb, sizeof(Vertex)});
    W32(push, 1);
    W32(push + 4, mem::PhysicalFromVirtual(commands));
    const uint32_t indexed_commands[] = {
        0x000417FC, 5,                 // BEGIN TRIANGLE_LIST
        0x40041800, (3u << 16) | 1u,  // ARRAY_ELEMENT16: vertices 1, 3
        0x00041808, 2,                 // ARRAY_ELEMENT32: final vertex 2
        0x000417FC, 0,                 // END
        0x00020000,                   // final hardware return, excluded from execution
    };
    std::memcpy(HostPtr(commands), indexed_commands, sizeof(indexed_commands));
    W32(push + 12, sizeof(indexed_commands));
    CallGuest(ctx, SYM(D3DDevice_Clear), {0, 0, gpu::kClearTarget, 0xFF0000FF, std::bit_cast<uint32_t>(1.0f), 0});
    check(CallGuest(ctx, SYM(D3DDevice_RunPushBuffer), {push, 0}) == 0, "recorded indexed draw");
    const uint32_t recorded_pixels = mem::VirtualFromPhysical(state.rt.data);
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, SYM(D3D_KickOffAndWaitForIdle), {});
    check(R32(recorded_pixels + (16 * 64 + 16) * 4) == 0xFF00FF00, "recorded indexed triangle is green");

    const uint32_t array_commands[] = {
        0x000417FC, 5,
        0x00041810, (2u << 24) | 1u,  // DRAW_ARRAYS: first 1, count 3
        0x000417FC, 0,
        0x00020000,
    };
    std::memcpy(HostPtr(commands), array_commands, sizeof(array_commands));
    W32(push + 12, sizeof(array_commands));
    CallGuest(ctx, SYM(D3DDevice_Clear), {0, 0, gpu::kClearTarget, 0xFF0000FF, std::bit_cast<uint32_t>(1.0f), 0});
    check(CallGuest(ctx, SYM(D3DDevice_RunPushBuffer), {push, 0}) == 0, "recorded array draw");
    const uint32_t array_pixels = mem::VirtualFromPhysical(state.rt.data);
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, SYM(D3D_KickOffAndWaitForIdle), {});
    check(R32(array_pixels + (16 * 64 + 16) * 4) == 0xFF00FF00, "recorded array triangle is green");

    // The queued clear must finish before the CPU installs a decoded movie frame.
    // With the render thread enabled this catches movie pixels overwritten by older draws.
    CallGuest(ctx, SYM(D3DDevice_Clear), {0, 0, gpu::kClearTarget, 0xFF0000FF, std::bit_cast<uint32_t>(1.0f), 0});
    const uint32_t movie_pixels = mem::VirtualFromPhysical(state.rt.data);
    W32(memory + 400, 0xFFAA5500);
    d3d::SetMovieFrame(memory + 400, 1, 1, 4, 0, 0);
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, SYM(D3D_KickOffAndWaitForIdle), {});
    check(R32(movie_pixels) == 0xFFAA5500, "decoded movie survives queued clear");
    std::fprintf(stderr, "draw and movie smoke: framebuffer checks passed\n");
    std::exit(0);
}
