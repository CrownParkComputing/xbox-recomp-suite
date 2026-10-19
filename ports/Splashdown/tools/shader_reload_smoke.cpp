// Replacing active microcode must work without rebinding the vertex declaration.
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
    auto check = [](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "shader reload smoke failed: %s\n", what);
            std::exit(1);
        }
    };
    const uint32_t memory = mem::AllocSystem(4096), declaration = memory + 256, program = memory + 1024;
    std::memset(HostPtr(memory), 0, 4096);
    W32(memory, 64);
    W32(memory + 4, 64);
    W32(memory + 8, 6);
    W32(memory + 12, 1);
    check(CallGuest(ctx, SYM(Direct3D_CreateDevice), {0, 1, 0, 0, memory, memory + 64}) == 0, "create device");
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});  // start the reference framebuffer
    for (unsigned r = 0; r < 16; ++r) W32(declaration + 0x14 + r * 16 + 8, 2);  // NONE
    W32(declaration + 0x14 + 8, 0x42);  // v0 = stream 0, FLOAT4
    const uint32_t code[] = {
        0, 1u << 21 | 0x1B, 2u << 26, 0xFu << 12 | 1u << 11,  // mov oPos, v0
        0, 1u << 21 | 96u << 13 | 0x1B, 3u << 26, 0xFu << 12 | 1u << 11 | 3u << 3 | 1u,  // mov oD0, c0
    };
    W32(program, 2u << 16 | 0x2078);
    std::memcpy(HostPtr(program + 4), code, sizeof(code));
    CallGuest(ctx, SYM(D3DDevice_LoadVertexShaderProgram), {program, 0});
    CallGuest(ctx, SYM(D3DDevice_SelectVertexShader), {declaration | 1, 0});
    CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {0});
    CallGuest(ctx, SYM(D3DDevice_SetRenderState_ZEnable), {0});
    auto& state = d3d::State();
    state.vsc[96] = {1, 0, 0, 1};
    state.vsc[97] = {0, 1, 0, 1};
    state.simple[gpu::kMColorWriteEnable >> 2] = 0x01010101;
    state.simple[gpu::kMBlendEnable >> 2] = 0;
    state.simple[gpu::kMAlphaTestEnable >> 2] = 0;
    W32(SYM(D3D_RenderState) + gpu::kRsFogEnable * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssColorOp * 4, 2);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssColorArg1 * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssAlphaOp * 4, 2);
    W32(SYM(D3D_DeferredTextureState) + gpu::kTssAlphaArg1 * 4, 0);
    W32(SYM(D3D_DeferredTextureState) + (32 + gpu::kTssColorOp) * 4, 1);
    CallGuest(ctx, SYM(D3DDevice_Clear), {0, 0, gpu::kClearTarget, 0xFF000000, std::bit_cast<uint32_t>(1.0f), 0});
    const float z = 0.5f * state.zscale;
    const float vertices[] = {4, 4, z, 1, 28, 4, z, 1, 4, 28, z, 1,
                              36, 4, z, 1, 60, 4, z, 1, 36, 28, z, 1,
                              4, 36, z, 1, 28, 36, z, 1, 4, 60, z, 1};
    std::memcpy(HostPtr(memory + 2048), vertices, sizeof(vertices));
    CallGuest(ctx, SYM(D3DDevice_DrawVerticesUP), {5, 3, memory + 2048, 16});
    // Reuse the source buffer as well as the declaration. Earlier queued draws
    // must keep the first program; subsequent draws must use the new program.
    W32(program + 4 + 5 * 4, 1u << 21 | 97u << 13 | 0x1B);
    CallGuest(ctx, SYM(D3DDevice_LoadVertexShaderProgram), {program, 64});
    CallGuest(ctx, SYM(D3DDevice_DrawVerticesUP), {5, 3, memory + 2048 + 96, 16});
    CallGuest(ctx, SYM(D3DDevice_LoadVertexShaderProgram), {program, 0});
    CallGuest(ctx, SYM(D3DDevice_DrawVerticesUP), {5, 3, memory + 2048 + 48, 16});
    const uint32_t pixels = mem::VirtualFromPhysical(state.rt.data);
    CallGuest(ctx, SYM(D3DDevice_Swap), {0});
    CallGuest(ctx, SYM(D3D_KickOffAndWaitForIdle), {});
    check(R32(pixels + (8 * 64 + 8) * 4) == 0xFFFF0000, "queued draw keeps the first program");
    check(R32(pixels + (8 * 64 + 40) * 4) == 0xFF00FF00, "replacement program applies without declaration rebind");
    check(R32(pixels + (40 * 64 + 8) * 4) == 0xFFFF0000, "loading an inactive program leaves the current draw unchanged");
    std::fprintf(stderr, "shader reload smoke: framebuffer checks passed\n");
    std::exit(0);
}
