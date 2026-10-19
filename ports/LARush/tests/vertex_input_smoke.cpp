#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "xdk/hle.h"
#include "xdk/d3d8.h"
#include "gpu/vertex.h"

X86_FUNC(sub_001B2594) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "vertex input smoke failed: %s\n", message); std::exit(1); }
    };
    const uint32_t m = mem::AllocSystem(1024), decl = m, out = m + 32, vb = m + 64, uv = m + 96, desc = m + 128;
    const uint32_t vertices = mem::AllocContiguous(1024, 0, 0xFFFFFFFF, 16);
    const uint32_t tokens[] = {0x20000000, 0x40320000, 0x20000001, 0x40220009, 0xFFFFFFFF};
    std::memcpy(HostPtr(decl), tokens, sizeof(tokens));
    check(GUEST_CALL(D3DDevice_CreateVertexShader, ctx, {decl, 0, out, 0}) == 0, "create two-stream declaration");
    const uint32_t shader = R32(out);
    const uint32_t direct = m + 256;
    W32(direct, 0); W32(direct + 4, 0); W32(direct + 8, 0x32);
    W32(direct + 9 * 16, 1); W32(direct + 9 * 16 + 4, 0); W32(direct + 9 * 16 + 8, 0x22);
    check(GUEST_CALL(D3DDevice_SetVertexShader, ctx, {shader}) == 0, "select shader");
    W32(vb, 1); W32(vb + 4, mem::PhysicalFromVirtual(vertices));
    W32(uv, 1); W32(uv + 4, mem::PhysicalFromVirtual(vertices + 512));
    const float position[] = {10, 20, 30, 1, 40, 50, 60, 1, 70, 80, 90, 1};
    const float texcoords[] = {.1f,.2f,.3f,.4f,.5f,.6f};
    std::memcpy(HostPtr(vertices + 16), position, sizeof(position));
    std::memcpy(HostPtr(vertices + 516), texcoords, sizeof(texcoords));
    check(GUEST_CALL(D3DDevice_SetStreamSource, ctx, {0, vb, 16}) == 0, "bind ordinary stream");
    const uint32_t normal_refs = R32(vb);
    W32(desc, vb); W32(desc + 4, 16); W32(desc + 8, 16);
    W32(desc + 12, uv); W32(desc + 16, 8); W32(desc + 20, 4);
    check(d3d::SetVertexShaderInputDirect(ctx, direct, 2, desc) == 0, "bind alternate input streams");
    check(R32(vb) == normal_refs + 0x80000 && R32(uv) == 0x80001, "retain alternate buffers");
    for (bool raw : {true, false}) {
        gpu::SetRawVertices(raw);
        gpu::Assembled assembled;
        check(gpu::Assemble(d3d::State(), 5, 0, 3, nullptr, assembled), "assemble custom streams");
        for (unsigned reg : {0u, 9u}) {
            const auto& e = assembled.elements[reg]; const auto& b = assembled.buffers[e.buffer];
            float value = 0; std::memcpy(&value, b.src + e.offset, 4);
            check(std::abs(value - (reg == 0 ? 10.f : .1f)) < 1e-6f,
                  "raw and converted inputs honor per-stream offsets");
        }
    }
    check(d3d::SetVertexShaderInputDirect(ctx, direct, 2, desc) == 0 && R32(vb) == normal_refs + 0x80000,
          "rebind balances retained references");
    check(d3d::SetVertexShaderInputDirect(ctx, 0, 0, 0) == 0, "restore ordinary binding");
    check(d3d::State().streams[0].vb == vb && d3d::State().streams[0].offset == 0 && !d3d::State().streams[1].vb,
          "ordinary bindings survive an alternate input override");
    check(R32(vb) == normal_refs && R32(uv) == 1, "release alternate references");
    check(d3d::SetVertexShaderInputDirect(ctx, direct, 17, desc) != 0, "reject excessive streams");
    std::fprintf(stderr, "vertex input smoke: %u checks passed\n", checks);
    std::exit(0);
}
