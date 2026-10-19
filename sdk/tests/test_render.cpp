// The Vulkan renderer against the SDK's CPU reference renderer: the same Direct3D 8 state and draws, each into its own
// guest surfaces, the Vulkan one read back through Sync as the title would lock it, then compared pixel for pixel.
// Runs on the machine's real GPU.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "gpu/state.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "reference_renderer.h"
#include "render_backend.h"

using namespace gpu;

namespace {

constexpr uint32_t kW = 64, kH = 64;
constexpr uint32_t kFmtLinArgb = 0x12;   // D3DFMT_LIN_A8R8G8B8
constexpr uint32_t kFmtLinD24S8 = 0x2E;  // D3DFMT_LIN_D24S8
constexpr uint32_t kTriangleList = 5;    // D3DPT_TRIANGLELIST
constexpr uint32_t kTriangleStrip = 6;
constexpr uint32_t kFvfXyz = 0x2, kFvfXyzRhw = 0x4, kFvfDiffuse = 0x40, kFvfTex1 = 0x100;
constexpr uint32_t kCullCw = 0x900, kCullCcw = 0x901;
constexpr uint32_t kTopSelectArg1 = 2, kTopDisable = 1, kTopModulate = 4;
constexpr uint32_t kTaDiffuse = 0, kTaTexture = 2;

bool GuestUp() {
    static const bool up = [] {
        if (!mem::Init()) return false;
        kernel::g_base = mem::Base();
        return true;
    }();
    return up;
}

uint32_t Contiguous(uint32_t bytes) { return mem::AllocContiguous(bytes, 0, mem::kPhysicalSize - 1, 0x1000); }

float Bits(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
uint32_t FloatBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// a linear surface or texture in guest memory, its D3DPixelContainer header in system memory
struct GuestSurface {
    uint32_t header = 0, data = 0, w = 0, h = 0, pitch = 0;
    Surface s;
    GuestSurface(uint32_t fmt, uint32_t width, uint32_t height, bool texture = false) : w(width), h(height) {
        pitch = (w * 4 + 63) & ~63u;
        data = Contiguous(pitch * h);
        header = mem::AllocSystem(20);
        kernel::W32(header, texture ? 0x40001 : 0x50001);  // D3DCOMMON: texture or surface, one reference
        kernel::W32(header + 4, mem::PhysicalFromVirtual(data));
        kernel::W32(header + 12, fmt << 8);
        kernel::W32(header + 16, (w - 1) | (h - 1) << 12 | (pitch / 64 - 1) << 24);
        s = {header, mem::PhysicalFromVirtual(data), fmt, 0};
    }
    uint32_t At(uint32_t x, uint32_t y) const { return kernel::R32(data + y * pitch + x * 4); }
    void Set(uint32_t x, uint32_t y, uint32_t argb) { kernel::W32(data + y * pitch + x * 4, argb); }
};

// a vertex buffer: D3DVertexBuffer's Common and Data, the vertices in contiguous memory
struct GuestBuffer {
    uint32_t header = 0, data = 0;
    explicit GuestBuffer(const std::vector<float>& v) {
        data = Contiguous(uint32_t(v.size() * 4));
        std::memcpy(kernel::HostPtr(data), v.data(), v.size() * 4);
        header = mem::AllocSystem(16);
        kernel::W32(header, 0x00000001);
        kernel::W32(header + 4, mem::PhysicalFromVirtual(data));
    }
};

uint32_t Argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) { return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | b; }

// the state both renderers start each scene from: diffuse colour out, no depth, no blend, no culling
State BaseState(const Surface& rt, const Surface* zs) {
    State s{};
    s.rt = rt;
    if (zs) s.zs = *zs;
    s.viewport = {0, 0, kW, kH, 0, 1};
    s.simple[kMColorWriteEnable >> 2] = 0x01010101;
    s.simple[kMZFunc >> 2] = 0x203;  // D3DCMP_LESSEQUAL
    s.simple[kMZWriteEnable >> 2] = 1;
    s.zscale = 16777215.0f;
    for (int i = 0; i < 4; i++) {
        s.tss[size_t(i)][kTssColorOp] = i ? kTopDisable : kTopSelectArg1;
        s.tss[size_t(i)][kTssAlphaOp] = i ? kTopDisable : kTopSelectArg1;
        s.tss[size_t(i)][kTssColorArg1] = kTaDiffuse;
        s.tss[size_t(i)][kTssAlphaArg1] = kTaDiffuse;
        s.tss[size_t(i)][kTssMinFilter] = 1;
        s.tss[size_t(i)][kTssMagFilter] = 1;
        s.tss[size_t(i)][kTssAddressU] = 3;
        s.tss[size_t(i)][kTssAddressV] = 3;
    }
    // identity transforms
    for (size_t t : {size_t(2), size_t(3), size_t(256)})
        for (int i = 0; i < 4; i++) s.transforms[t][size_t(i * 5)] = 1;
    return s;
}


// pixels more than tol apart in any channel, and the worst difference
struct Diff {
    uint32_t count = 0, worst = 0;
    std::string first;
};

Diff Compare(const GuestSurface& a, const GuestSurface& b, uint32_t tol) {
    Diff d;
    for (uint32_t y = 0; y < a.h; y++)
        for (uint32_t x = 0; x < a.w; x++) {
            uint32_t p = a.At(x, y), q = b.At(x, y), worst = 0;
            for (int c = 0; c < 32; c += 8) worst = std::max(worst, uint32_t(std::abs(int((p >> c) & 255) - int((q >> c) & 255))));
            d.worst = std::max(d.worst, worst);
            if (worst <= tol) continue;
            if (!d.count) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "first at %u,%u: vulkan %08X reference %08X", x, y, p, q);
                d.first = buf;
            }
            d.count++;
        }
    return d;
}

std::unique_ptr<Backend>& Vulkan() {
    static std::unique_ptr<Backend> b = [] {
        auto v = test::MakeRenderer(1);
        if (!GuestUp() || !v->Init(kW, kH)) v.reset();
        return v;
    }();
    return b;
}

// both renderers draw the scene into fresh surfaces; the Vulkan one is read back as a lock would
Diff Run(const char* name, uint32_t tol, const std::function<void(State&, Backend*)>& scene, bool depth = false,
         Backend* vulkan = nullptr) {
    if (!vulkan) vulkan = Vulkan().get();
    GuestSurface vrt(kFmtLinArgb, kW, kH), rrt(kFmtLinArgb, kW, kH);
    GuestSurface vzs(kFmtLinD24S8, kW, kH), rzs(kFmtLinD24S8, kW, kH);
    State vs = BaseState(vrt.s, depth ? &vzs.s : nullptr);
    State rs = BaseState(rrt.s, depth ? &rzs.s : nullptr);
    reference::BeginFrame();
    scene(rs, nullptr);
    scene(vs, vulkan);
    vulkan->Sync(vrt.data, 0);
    Diff d = Compare(vrt, rrt, tol);
    std::printf("    %s: %u pixels apart (worst %u)%s%s\n", name, d.count, d.worst, d.count ? ", " : "", d.first.c_str());
    return d;
}

void Clear(State& s, Backend* b, uint32_t flags, uint32_t color, float z = 1) {
    if (b) b->Clear(s, 0, nullptr, flags, color, z, 0);
    else reference::Clear(s, 0, nullptr, flags, color, z, 0);
}

// each renderer reads vertices as it does for real: the Vulkan one the guest's own, the reference one converted
void Draw(State& s, Backend* b, uint32_t prim, uint32_t count) {
    SetRawVertices(b != nullptr);
    if (b) b->Draw(s, prim, 0, count, nullptr);
    else reference::Draw(s, prim, 0, count, nullptr);
    SetRawVertices(true);
}

// screen space vertices: x y z rhw, diffuse
std::vector<float> RhwDiffuse(std::initializer_list<std::array<float, 4>> pos, std::initializer_list<uint32_t> col) {
    std::vector<float> v;
    auto c = col.begin();
    for (const auto& p : pos) {
        v.insert(v.end(), p.begin(), p.end());
        v.push_back(Bits(*c++));
    }
    return v;
}

// edge pixels may round differently between a rasterizer and the CPU's: a scene passes with few of them apart
constexpr uint32_t kEdgeSlack = kW * 2;

}  // namespace

TEST("render: a Vulkan device and the headless renderer come up") {
    REQUIRE(GuestUp());
    CHECK_MSG(Vulkan() != nullptr, "no Vulkan 1.3 device, or the renderer failed to start (see the log)");
}

TEST("render: a clear fills the target with the colour, exactly") {
    REQUIRE(Vulkan());
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, Argb(0xFF, 0x33, 0x66, 0x99), 1, 0);
    Vulkan()->Sync(rt.data, 0);
    CHECK_EQ(rt.At(0, 0), Argb(0xFF, 0x33, 0x66, 0x99));
    CHECK_EQ(rt.At(kW - 1, kH - 1), Argb(0xFF, 0x33, 0x66, 0x99));
}

TEST("render: a clear with rects fills only them") {
    REQUIRE(Vulkan());
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, 0, 1, 0);
    int32_t rect[4] = {8, 8, 24, 16};
    Vulkan()->Clear(s, 1, reinterpret_cast<const uint8_t*>(rect), kClearTarget, Argb(0xFF, 0xFF, 0, 0), 1, 0);
    Vulkan()->Sync(rt.data, 0);
    CHECK_EQ(rt.At(8, 8), Argb(0xFF, 0xFF, 0, 0));
    CHECK_EQ(rt.At(23, 15), Argb(0xFF, 0xFF, 0, 0));
    CHECK_EQ(rt.At(24, 15), 0u);
    CHECK_EQ(rt.At(8, 16), 0u);
}

TEST("render: a screen space gouraud triangle matches the reference") {
    REQUIRE(Vulkan());
    GuestBuffer vb(RhwDiffuse({{{4, 4, 0.5f, 1}}, {{60, 10, 0.5f, 1}}, {{20, 58, 0.5f, 1}}},
                              {Argb(255, 255, 0, 0), Argb(255, 0, 255, 0), Argb(255, 0, 0, 255)}));
    Diff d = Run("rhw triangle", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {vb.header, 20};
        Clear(s, b, kClearTarget, Argb(255, 16, 16, 16));
        Draw(s, b, kTriangleList, 3);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}

TEST("render: transformed geometry goes through world, view, projection and viewport as the reference has it") {
    REQUIRE(Vulkan());
    // a triangle in world units, pushed off centre by the world matrix and squeezed by the projection
    std::vector<float> v;
    for (auto [x, y, c] : {std::tuple{-0.5f, -0.5f, Argb(255, 255, 255, 0)}, std::tuple{0.6f, -0.4f, Argb(255, 0, 255, 255)},
                           std::tuple{0.0f, 0.7f, Argb(255, 255, 0, 255)}}) {
        v.insert(v.end(), {x, y, 0.5f, Bits(c)});
    }
    GuestBuffer vb(v);
    Diff d = Run("transformed triangle", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyz | kFvfDiffuse;
        s.streams[0] = {vb.header, 16};
        s.transforms[256][12] = 0.25f;  // world: x + 0.25
        s.transforms[3][0] = 0.8f;      // projection: x scaled
        s.transforms[3][5] = 1.2f;
        s.viewport = {8, 4, 48, 56, 0, 1};
        Clear(s, b, kClearTarget, 0);
        Draw(s, b, kTriangleList, 3);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}

TEST("render: culling removes the same winding the reference removes") {
    REQUIRE(Vulkan());
    // left: clockwise on screen, right: counter clockwise
    GuestBuffer vb(RhwDiffuse({{{2, 2, 0.5f, 1}}, {{30, 2, 0.5f, 1}}, {{2, 60, 0.5f, 1}},
                               {{34, 2, 0.5f, 1}}, {{34, 60, 0.5f, 1}}, {{62, 2, 0.5f, 1}}},
                              {Argb(255, 255, 0, 0), Argb(255, 255, 0, 0), Argb(255, 255, 0, 0), Argb(255, 0, 0, 255),
                               Argb(255, 0, 0, 255), Argb(255, 0, 0, 255)}));
    for (uint32_t cull : {kCullCw, kCullCcw}) {
        Diff d = Run(cull == kCullCw ? "cull clockwise" : "cull counter clockwise", 3, [&](State& s, Backend* b) {
            s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
            s.streams[0] = {vb.header, 20};
            s.rs[kRsCullMode] = cull;
            Clear(s, b, kClearTarget, 0);
            Draw(s, b, kTriangleList, 6);
        });
        CHECK_MSG(d.count < kEdgeSlack, d.first);
    }
}

TEST("render: a textured strip samples a linear texture as the reference does") {
    REQUIRE(Vulkan());
    GuestSurface tex(kFmtLinArgb, 4, 4, true);
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) tex.Set(x, y, (x + y) & 1 ? Argb(255, 255, 255, 255) : Argb(255, 200, 40, 10));
    // x y z rhw u v: a linear texture is addressed in texels
    std::vector<float> v = {0, 0, 0.5f, 1, 0, 0, 64, 0, 0.5f, 1, 4, 0, 0, 64, 0.5f, 1, 0, 4, 64, 64, 0.5f, 1, 4, 4};
    GuestBuffer vb(v);
    Diff d = Run("textured strip", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfTex1;
        s.streams[0] = {vb.header, 24};
        s.textures[0] = tex.header;
        s.tss[0][kTssColorArg1] = kTaTexture;
        s.tss[0][kTssAlphaArg1] = kTaTexture;
        Clear(s, b, kClearTarget, 0);
        Draw(s, b, kTriangleStrip, 4);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}

TEST("render: the depth test keeps the nearer of two overlapping triangles in either order") {
    REQUIRE(Vulkan());
    GuestBuffer near_tri(RhwDiffuse({{{4, 4, 0.2f, 1}}, {{60, 4, 0.2f, 1}}, {{32, 60, 0.2f, 1}}},
                                    {Argb(255, 0, 255, 0), Argb(255, 0, 255, 0), Argb(255, 0, 255, 0)}));
    GuestBuffer far_tri(RhwDiffuse({{{4, 60, 0.8f, 1}}, {{32, 4, 0.8f, 1}}, {{60, 60, 0.8f, 1}}},
                                   {Argb(255, 255, 0, 0), Argb(255, 255, 0, 0), Argb(255, 255, 0, 0)}));
    for (bool near_first : {true, false}) {
        Diff d = Run(near_first ? "depth near first" : "depth far first", 3, [&](State& s, Backend* b) {
            s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
            s.rs[kRsZEnable] = 1;
            Clear(s, b, kClearTarget | kClearZ | kClearStencil, 0, 1);
            for (const GuestBuffer* t : near_first ? std::vector{&near_tri, &far_tri} : std::vector{&far_tri, &near_tri}) {
                s.streams[0] = {t->header, 20};
                Draw(s, b, kTriangleList, 3);
            }
        }, true);
        CHECK_MSG(d.count < kEdgeSlack, d.first);
    }
}

// A lone triangle off the pixel grid: the reference renderer counts a pixel centre exactly on an edge as inside (both
// triangles of a strip draw it, so a blended strip blends its diagonal twice); a GPU, the console's included, draws such
// a pixel once, by the top-left rule. Measured: a strip's 64 diagonal pixels differ by exactly one extra blend.
TEST("render: source alpha blending over the target matches the reference") {
    REQUIRE(Vulkan());
    // a quarter pixel off the grid: no pixel centre lies on an edge, where the two rules part
    GuestBuffer vb(RhwDiffuse({{{0.25f, 0.25f, 0.5f, 1}}, {{64.25f, 0.25f, 0.5f, 1}}, {{0.25f, 64.25f, 0.5f, 1}}},
                              {Argb(64, 255, 0, 0), Argb(128, 255, 0, 0), Argb(192, 0, 0, 255)}));
    Diff d = Run("alpha blend", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {vb.header, 20};
        s.simple[kMBlendEnable >> 2] = 1;
        s.simple[kMSrcBlend >> 2] = 0x302;   // SRCALPHA
        s.simple[kMDestBlend >> 2] = 0x303;  // INVSRCALPHA
        s.simple[kMBlendOp >> 2] = 0x8006;   // ADD
        Clear(s, b, kClearTarget, Argb(255, 20, 40, 200));
        Draw(s, b, kTriangleList, 3);
    });
    CHECK_MSG(d.count == 0, d.first);
}

TEST("render: the alpha test discards what fails the reference") {
    REQUIRE(Vulkan());
    GuestBuffer vb(RhwDiffuse({{{0, 0, 0.5f, 1}}, {{64, 0, 0.5f, 1}}, {{0, 64, 0.5f, 1}}, {{64, 64, 0.5f, 1}}},
                              {Argb(0, 255, 255, 255), Argb(255, 255, 255, 255), Argb(0, 255, 255, 255), Argb(255, 255, 255, 255)}));
    Diff d = Run("alpha test", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {vb.header, 20};
        s.simple[kMAlphaTestEnable >> 2] = 1;
        s.simple[kMAlphaFunc >> 2] = 0x206;  // GREATEREQUAL
        s.simple[kMAlphaRef >> 2] = 128;
        Clear(s, b, kClearTarget, 0);
        Draw(s, b, kTriangleStrip, 4);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}

TEST("render: CPU pixels written over a drawn target after a lock reach the next frame") {
    REQUIRE(Vulkan());
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, Argb(255, 0, 0, 0), 1, 0);
    Vulkan()->Sync(rt.data, 0);
    rt.Set(10, 10, Argb(255, 1, 2, 3));
    MarkDirty(rt.header);
    // a draw that leaves (10,10) alone: the target is drawn into again, the CPU's pixel must still be there after
    GuestBuffer vb(RhwDiffuse({{{40, 40, 0.5f, 1}}, {{60, 40, 0.5f, 1}}, {{40, 60, 0.5f, 1}}},
                              {Argb(255, 9, 9, 9), Argb(255, 9, 9, 9), Argb(255, 9, 9, 9)}));
    s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
    s.streams[0] = {vb.header, 20};
    Vulkan()->Draw(s, kTriangleList, 0, 3, nullptr);
    Vulkan()->Sync(rt.data, 0);
    CHECK_EQ(rt.At(10, 10), Argb(255, 1, 2, 3));
    CHECK_EQ(rt.At(42, 42), Argb(255, 9, 9, 9));
}

TEST("render: CopyRects between drawn targets copies the rect to its point") {
    REQUIRE(Vulkan());
    GuestSurface a(kFmtLinArgb, kW, kH), b(kFmtLinArgb, kW, kH);
    State s = BaseState(a.s, nullptr);
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, Argb(255, 200, 100, 50), 1, 0);
    State t = BaseState(b.s, nullptr);
    Vulkan()->Clear(t, 0, nullptr, kClearTarget, 0, 1, 0);
    uint32_t rects = mem::AllocSystem(16), points = mem::AllocSystem(8);
    kernel::W32(rects, 0);
    kernel::W32(rects + 4, 0);
    kernel::W32(rects + 8, 8);
    kernel::W32(rects + 12, 8);
    kernel::W32(points, 20);
    kernel::W32(points + 4, 30);
    Vulkan()->Copy(a.s, b.s, 1, rects, points);
    Vulkan()->Sync(b.data, 0);
    CHECK_EQ(b.At(20, 30), Argb(255, 200, 100, 50));
    CHECK_EQ(b.At(27, 37), Argb(255, 200, 100, 50));
    CHECK_EQ(b.At(28, 37), 0u);
    CHECK_EQ(b.At(19, 30), 0u);
}

TEST("render: presents turn frames over without a window, the last frame's pixels readable") {
    REQUIRE(Vulkan());
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    for (int i = 0; i < 5; i++) {
        Vulkan()->Clear(s, 0, nullptr, kClearTarget, Argb(255, uint8_t(i * 40), 0, 0), 1, 0);
        Vulkan()->Present(rt.s);
    }
    Vulkan()->Sync(rt.data, 0);
    CHECK_EQ(rt.At(1, 1), Argb(255, 160, 0, 0));
}

// a vertex program: v0 straight to oPos (the console's screen space), c0 (hardware c96) to oD0
TEST("render: a vertex program's position and constants reach the target as the reference has them") {
    REQUIRE(Vulkan());
    auto vs = std::make_shared<VertexShader>();
    vs->handle = 0x1001;
    vs->inputs[0] = {0, 0, 0x42};  // stream 0, offset 0, FLOAT4
    vs->stream_stride[0] = 16;
    vs->program = {0, 1u << 21 | 2u << 26 >> 26 << 26 | 0u << 9, 2u << 26, 0xFu << 12 | 1u << 11 | 0u << 3,
                   0, 1u << 21 | 96u << 13, 3u << 26, 0xFu << 12 | 1u << 11 | 3u << 3 | 1u};
    // instruction 1 reads its A operand from input v0 (mux 2), instruction 2 from constant 96 (mux 3)
    vs->program[1] = 1u << 21;
    float z = 0.5f * 16777215.0f;
    GuestBuffer vb({6, 6, z, 1, 58, 12, z, 1, 30, 60, z, 1});
    Diff d = Run("vertex program", 3, [&](State& s, Backend* b) {
        s.vs_handle = vs->handle;
        s.vs = vs;
        s.streams[0] = {vb.header, 16};
        s.vsc[96] = {0.25f, 0.5f, 1.0f, 1.0f};
        Clear(s, b, kClearTarget, 0);
        Draw(s, b, kTriangleList, 3);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
    CHECK_MSG(d.worst <= 3 || d.count, "the constant colour is exact");
}

// Fixed function lighting, against D3D's lighting equation worked here: the reference renderer passes the vertex
// colour through instead of lighting (measured: white everywhere), so it can't judge this. Per vertex
// colour = emissive + material ambient * global ambient + material diffuse * light diffuse * max(N . -dir, 0),
// interpolated across the triangle; checked at interior pixel centres.
TEST("render: fixed function lighting follows D3D's lighting equation, interpolated per vertex") {
    REQUIRE(Vulkan());
    struct Vtx {
        float x, y, z, nx, ny, nz;
    };
    const Vtx tri[3] = {{-0.8f, -0.8f, 0.5f, 0, 0, -1}, {0.8f, -0.8f, 0.5f, 0.6f, 0, -0.8f}, {0, 0.8f, 0.5f, -0.3f, 0.5f, -0.81f}};
    std::vector<float> v;
    for (const Vtx& t : tri) v.insert(v.end(), {t.x, t.y, t.z, t.nx, t.ny, t.nz});
    GuestBuffer vb(v);
    const float ld[3] = {1, 0.9f, 0.7f}, dir[3] = {0.3f, -0.2f, 1}, md[3] = {0.8f, 0.6f, 0.4f}, me[3] = {0, 0, 0.1f};
    const float amb[3] = {30 / 255.0f, 30 / 255.0f, 60 / 255.0f};
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    s.vs_handle = kFvfXyz | 0x10;  // XYZ, NORMAL
    s.streams[0] = {vb.header, 24};
    s.rs[kRsLighting] = 1;
    s.rs[kRsAmbient] = Argb(255, 30, 30, 60);
    s.light_enable = 1;
    auto& l = s.lights[0];
    l[0] = 3;  // D3DLIGHT_DIRECTIONAL: the type is a DWORD, the rest floats
    float light[26] = {0, ld[0], ld[1], ld[2], 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, dir[0], dir[1], dir[2], 0, 0, 1, 0, 0, 0, 0};
    for (int i = 1; i < 26; i++) l[size_t(i)] = FloatBits(light[i]);
    float mat[17] = {md[0], md[1], md[2], 1, 1, 1, 1, 1, 0, 0, 0, 0, me[0], me[1], me[2], 0, 0};
    std::copy(mat, mat + 17, s.material.begin());
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, 0, 1, 0);
    Vulkan()->Draw(s, kTriangleList, 0, 3, nullptr);
    Vulkan()->Sync(rt.data, 0);

    // the lit colour of each corner, and where the corners land
    float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float col[3][3], sx[3], sy[3];
    for (int k = 0; k < 3; k++) {
        float nl = -(tri[k].nx * dir[0] + tri[k].ny * dir[1] + tri[k].nz * dir[2]) / dl;
        for (int c = 0; c < 3; c++) col[k][c] = std::min(1.0f, me[c] + amb[c] + md[c] * ld[c] * std::max(nl, 0.0f));
        sx[k] = (tri[k].x + 1) * 0.5f * kW;
        sy[k] = (1 - tri[k].y) * 0.5f * kH;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    int checked = 0;
    for (auto [px, py] : {std::pair{31, 40}, std::pair{20, 50}, std::pair{45, 52}, std::pair{32, 20}}) {
        float cx = px + 0.5f, cy = py + 0.5f;
        float b0 = ((sx[1] - cx) * (sy[2] - cy) - (sx[2] - cx) * (sy[1] - cy)) / area;
        float b1 = ((sx[2] - cx) * (sy[0] - cy) - (sx[0] - cx) * (sy[2] - cy)) / area;
        float b2 = 1 - b0 - b1;
        REQUIRE(b0 > 0.05f && b1 > 0.05f && b2 > 0.05f);  // well inside
        uint32_t got = rt.At(uint32_t(px), uint32_t(py));
        for (int c = 0; c < 3; c++) {
            float want = (b0 * col[0][c] + b1 * col[1][c] + b2 * col[2][c]) * 255;
            int have = int((got >> (16 - 8 * c)) & 255);
            CHECK_MSG(std::fabs(have - want) <= 2.5f, "pixel " + std::to_string(px) + "," + std::to_string(py) + " channel " +
                                                         std::to_string(c) + ": " + std::to_string(have) + " vs " + std::to_string(want));
        }
        checked++;
    }
    CHECK_EQ(checked, 4);
}

// the scene's target multisampled, where the renderer has MSAA: a read-back resolves it; inside the triangle every
// sample agrees
TEST("render: a multisampled scene resolves on read-back to the reference inside its edges") {
    REQUIRE(Vulkan());
    auto msaa = test::MakeMultisampledRenderer(4);
    if (!msaa) {
        std::printf("    no MSAA in this renderer: skipped\n");
        return;
    }
    REQUIRE(msaa->Init(kW, kH));
    GuestBuffer vb(RhwDiffuse({{{4, 4, 0.5f, 1}}, {{60, 10, 0.5f, 1}}, {{20, 58, 0.5f, 1}}},
                              {Argb(255, 255, 0, 0), Argb(255, 0, 255, 0), Argb(255, 0, 0, 255)}));
    Diff d = Run("msaa triangle", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {vb.header, 20};
        Clear(s, b, kClearTarget, Argb(255, 16, 16, 16));
        Draw(s, b, kTriangleList, 3);
    }, false, msaa.get());
    // the edges are blended by coverage, the inside is not
    CHECK_MSG(d.count < kW * 3, d.first);
    CHECK_MSG(d.count > 0, "MSAA smooths the edges, so some edge pixels differ from the aliased reference");
}

// targets at twice the console size: a read-back shrinks them to the guest's pixels
TEST("render: an upscaled target is shrunk to the console's pixels on read-back") {
    REQUIRE(Vulkan());
    auto big = test::MakeRenderer(2.0f);
    REQUIRE(big->Init(kW, kH));
    GuestBuffer vb(RhwDiffuse({{{0, 0, 0.5f, 1}}, {{64, 0, 0.5f, 1}}, {{0, 64, 0.5f, 1}}, {{64, 64, 0.5f, 1}}},
                              {Argb(255, 200, 60, 20), Argb(255, 200, 60, 20), Argb(255, 200, 60, 20), Argb(255, 200, 60, 20)}));
    Diff d = Run("2x scale", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {vb.header, 20};
        Clear(s, b, kClearTarget, 0);
        Draw(s, b, kTriangleStrip, 4);
    }, false, big.get());
    CHECK_MSG(d.count == 0, d.first);
}

// a draw that samples the target it draws into reads what was there before it (a copy), texel for pixel
TEST("render: a draw sampling its own target reads the target's pixels, texel for pixel") {
    REQUIRE(Vulkan());
    GuestBuffer tri(RhwDiffuse({{{4, 4, 0.5f, 1}}, {{60, 10, 0.5f, 1}}, {{20, 58, 0.5f, 1}}},
                               {Argb(255, 255, 0, 0), Argb(255, 0, 255, 0), Argb(255, 0, 0, 255)}));
    // a full screen strip that modulates the target's own pixels by half grey
    std::vector<float> quad;
    for (auto [x, y] : {std::pair{0.f, 0.f}, std::pair{64.f, 0.f}, std::pair{0.f, 64.f}, std::pair{64.f, 64.f}})
        quad.insert(quad.end(), {x, y, 0.5f, 1, Bits(Argb(255, 128, 128, 128)), x, y});
    GuestBuffer full(quad);
    Diff d = Run("feedback", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {tri.header, 20};
        Clear(s, b, kClearTarget, Argb(255, 40, 40, 40));
        Draw(s, b, kTriangleList, 3);
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse | kFvfTex1;
        s.streams[0] = {full.header, 28};
        s.textures[0] = s.rt.header;
        s.tss[0][kTssColorOp] = kTopModulate;
        s.tss[0][kTssColorArg1] = kTaTexture;
        s.tss[0][kTssColorArg2] = kTaDiffuse;
        Draw(s, b, kTriangleStrip, 4);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}

// the title writes new texels and locks the texture: the next frame samples the new ones
TEST("render: a texture the CPU changed is uploaded again for the next frame") {
    REQUIRE(Vulkan());
    GuestSurface tex(kFmtLinArgb, 4, 4, true);
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) tex.Set(x, y, Argb(255, 10, 20, 30));
    std::vector<float> v = {0, 0, 0.5f, 1, 0, 0, 64, 0, 0.5f, 1, 4, 0, 0, 64, 0.5f, 1, 0, 4, 64, 64, 0.5f, 1, 4, 4};
    GuestBuffer vb(v);
    GuestSurface rt(kFmtLinArgb, kW, kH);
    State s = BaseState(rt.s, nullptr);
    s.vs_handle = kFvfXyzRhw | kFvfTex1;
    s.streams[0] = {vb.header, 24};
    s.textures[0] = tex.header;
    s.tss[0][kTssColorArg1] = kTaTexture;
    s.tss[0][kTssAlphaArg1] = kTaTexture;
    Vulkan()->Draw(s, kTriangleStrip, 0, 4, nullptr);
    Vulkan()->Present(rt.s);
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) tex.Set(x, y, Argb(255, 200, 150, 100));
    MarkDirty(tex.header);
    Vulkan()->Draw(s, kTriangleStrip, 0, 4, nullptr);
    Vulkan()->Sync(rt.data, 0);
    CHECK_EQ(rt.At(32, 32), Argb(255, 200, 150, 100));
}

// a visibility test: the title counts the pixels a draw passes, then reads the report the GPU writes once done
TEST("render: a visibility test reports the pixels its draws covered, once the GPU has finished them") {
    REQUIRE(Vulkan());
    if (!test::CountsPixels()) {
        std::printf("    no visibility tests in this renderer: skipped\n");
        return;
    }
    GuestSurface rt(kFmtLinArgb, kW, kH);
    // an axis aligned 20 x 10 rectangle on pixel edges: exactly 200 pixels by any rasterizer's rule
    GuestBuffer vb(RhwDiffuse({{{10, 20, 0.5f, 1}}, {{30, 20, 0.5f, 1}}, {{10, 30, 0.5f, 1}}, {{30, 30, 0.5f, 1}}},
                              {Argb(255, 1, 1, 1), Argb(255, 1, 1, 1), Argb(255, 1, 1, 1), Argb(255, 1, 1, 1)}));
    uint32_t report = mem::AllocSystem(16);
    kernel::W32(report + 12, 0xFFFFFFFF);  // pending until the GPU writes it
    State s = BaseState(rt.s, nullptr);
    s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
    s.streams[0] = {vb.header, 20};
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, 0, 1, 0);
    Vulkan()->ClearReport();
    s.simple[kMCountEnable >> 2] = 1;
    Vulkan()->Draw(s, kTriangleStrip, 0, 4, nullptr);
    s.simple[kMCountEnable >> 2] = 0;
    Vulkan()->Draw(s, kTriangleStrip, 0, 4, nullptr);  // not counted
    Vulkan()->GetReport(report);
    for (int i = 0; i < 4 && kernel::R32(report + 12); i++) {
        Vulkan()->Present(rt.s);
        Vulkan()->Flush();
    }
    CHECK_MSG(kernel::R32(report + 12) == 0, "the report was written");
    CHECK_EQ(kernel::R32(report + 8), 200u);
}

// points are drawn one console pixel each, and nothing round it; the console's pixel centres are on whole numbers
TEST("render: points light one pixel each") {
    REQUIRE(Vulkan());
    GuestSurface rt(kFmtLinArgb, kW, kH);
    GuestBuffer vb(RhwDiffuse({{{10, 10, 0.5f, 1}}, {{40, 20, 0.5f, 1}}}, {Argb(255, 250, 0, 0), Argb(255, 0, 250, 0)}));
    State s = BaseState(rt.s, nullptr);
    s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
    s.streams[0] = {vb.header, 20};
    Vulkan()->Clear(s, 0, nullptr, kClearTarget, 0, 1, 0);
    Vulkan()->Draw(s, 1, 0, 2, nullptr);  // D3DPT_POINTLIST
    Vulkan()->Sync(rt.data, 0);
    int red = 0, green = 0, other = 0;
    std::string where;
    for (uint32_t y = 0; y < kH; y++)
        for (uint32_t x = 0; x < kW; x++) {
            uint32_t p = rt.At(x, y);
            if (!p) continue;
            where += " " + std::to_string(x) + "," + std::to_string(y);
            if (p == Argb(255, 250, 0, 0)) red++;
            else if (p == Argb(255, 0, 250, 0)) green++;
            else other++;
        }
    CHECK_MSG(red == 1 && green == 1 && other == 0, "lit:" + where);
}

TEST("render: immediate float4 screen vertices preserve XYZRHW") {
    REQUIRE(Vulkan());
    GuestBuffer native(RhwDiffuse({{{4, 4, 0.5f, 1}}, {{60, 10, 0.5f, 1}}, {{20, 58, 0.5f, 1}}},
                                  {0xFFFF0000, 0xFF00FF00, 0xFF0000FF}));
    GuestBuffer immediate({4, 4, 0.5f, 1, 1, 0, 0, 1,
                           60, 10, 0.5f, 1, 0, 1, 0, 1,
                           20, 58, 0.5f, 1, 0, 0, 1, 1});
    Diff d = Run("immediate XYZRHW", 3, [&](State& s, Backend* b) {
        s.vs_handle = kFvfXyzRhw | kFvfDiffuse;
        s.streams[0] = {native.header, 20};
        if (b) {
            s.vs_handle |= 1;
            s.vs = std::make_shared<VertexShader>();
            s.vs->pretransformed = true;
            s.vs->inputs[0] = {0, 0, 0x42};
            s.vs->inputs[3] = {0, 16, 0x42};
            s.vs->stream_stride[0] = 32;
            s.streams[0] = {immediate.header, 32};
        }
        Clear(s, b, kClearTarget, 0xFF101010);
        Draw(s, b, kTriangleList, 3);
    });
    CHECK_MSG(d.count < kEdgeSlack, d.first);
}
