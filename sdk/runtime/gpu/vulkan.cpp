// Frames, targets, textures and draws: gpu/d3d12.cpp's core, on Vulkan
#include "gpu/vulkan.h"

#include <dxgiformat.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace gpu::vk {

using namespace renderer;

namespace renderer {
Perf g_perf;
}  // namespace renderer

namespace {

PFN_vkCmdPushDescriptorSetKHR g_push = nullptr;

const char* kBlitHlsl = R"(
cbuffer B : register(b1) { float4 uvrect; float4 opts; float4 rotation; };
Texture2D img : register(t0);
Texture2D lut : register(t1);
SamplerState ls : register(s0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    if (rotation.x == 1) t = float2(t.y, 1 - t.x);
    else if (rotation.x == 2) t = 1 - t;
    else if (rotation.x == 3) t = float2(1 - t.y, t.x);
    o.uv = uvrect.xy + t * uvrect.zw;
    return o;
}
float4 psmain(O i) : SV_Target {
    float4 c = 0;
    int n = max((int)opts.z, 1);
    float2 px = fwidth(i.uv);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) c += img.SampleLevel(ls, i.uv + px * ((float2(x, y) + 0.5) / n - 0.5), 0);
    c /= n * n;
    c.rgb *= opts.w;
    if (opts.x > 0) c.rgb = float3(lut.Sample(ps, float2(c.r, 0.5)).r, lut.Sample(ps, float2(c.g, 0.5)).g, lut.Sample(ps, float2(c.b, 0.5)).b);
    return float4(c.rgb, opts.y > 0 ? c.a : 1);
}
)";

float Bits(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}


// A small draw whose every float2 input lies in 0..1: sprites, HUD pieces, text. Upscaled, the far edge's last half
// texel is drawn too and wrap blends in the opposite edge (thin lines on HUD sprites); clamp takes the edge texel.
bool UnitTexcoords(const Assembled& a) {
    constexpr float kLo = -1e-4f;
    constexpr float kHi = 1.0001f;
    constexpr uint32_t kSmallDrawVertices = 4096;
    if (!a.vertices.empty() || !a.vertex_count || a.vertex_count > kSmallDrawVertices) return false;
    bool any = false;
    for (int r = 0; r < 16; r++) {
        const VertexElement& e = a.elements[size_t(r)];
        if (!(a.regs >> r & 1) || e.format != DXGI_FORMAT_R32G32_FLOAT) continue;
        const VertexBuffer& vb = a.buffers[e.buffer];
        if (!vb.src || !vb.stride || uint64_t(a.vertex_count) * vb.stride > vb.bytes) return false;
        for (uint32_t v = 0; v < a.vertex_count; v++) {
            float uv[2];
            std::memcpy(uv, vb.src + size_t(v) * vb.stride + e.offset, sizeof(uv));
            if (!(uv[0] >= kLo && uv[0] <= kHi && uv[1] >= kLo && uv[1] <= kHi)) return false;
        }
        any = true;
    }
    return any;
}


constexpr uint32_t kFmtD16 = 0x2C;     // D3DFMT_D16
constexpr uint32_t kFmtLinD16 = 0x30;  // D3DFMT_LIN_D16

uint8_t ShadowDepthKind(uint32_t format) { return format == kFmtD16 || format == kFmtLinD16 ? 2 : 1; }


// MemSig samples a big surface and misses small writes (XFONT text): after a Sync every byte is compared
bool Changed(const Target& t) {
    if (t.drawn || t.read.empty()) return false;
    uint32_t row = t.gw * 4;
    uint32_t pitch = GuestPitch(t.gw);
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++)
        if (std::memcmp(p + size_t(y) * pitch, &t.read[size_t(y) * row], row)) return true;
    return false;
}


// the pixels the CPU changed since the last Sync, opaque, the rest 0; t.read takes them
std::vector<uint8_t> LatchCpuChanges(Target& t) {
    std::vector<uint8_t> px(t.read.size(), 0);
    uint32_t guest_pitch = GuestPitch(t.gw);
    const uint8_t* guest = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        for (uint32_t x = 0; x < t.gw; x++) {
            const uint8_t* c = guest + size_t(y) * guest_pitch + x * 4;
            size_t i = (size_t(y) * t.gw + x) * 4;
            if (!std::memcmp(c, &t.read[i], 4)) continue;
            std::memcpy(&t.read[i], c, 4);
            std::memcpy(&px[i], c, 3);
            px[i + 3] = 255;
        }
    }
    return px;
}


TexKind TextureKind(const TextureDesc& d) {
    if (d.cube) return kTexCube;
    if (d.depth > 1) return kTex3D;
    return kTex2D;
}

bool SameTexture(const Tex& tx, const TextureDesc& d) {
    return tx.res && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height &&
           tx.d.depth == d.depth && tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
}

// a P8 texture's palette: its colours, their address (part of the texture's key) and their hash, all 0 without one
struct StagePalette {
    uint32_t colours[256] = {};
    uint32_t va = 0;
    uint64_t hash = 0;
};

void ReadStagePalette(const State& s, int stage, const TextureDesc& d, StagePalette& palette) {
    constexpr uint32_t kFmtP8 = 0x0B;
    constexpr uint32_t kPaletteCommon = 0;
    constexpr uint32_t kPaletteData = 4;
    constexpr uint32_t kPaletteSizeShift = 30;
    if (d.format != kFmtP8 || !s.palettes[size_t(stage)]) return;
    uint32_t header = s.palettes[size_t(stage)];
    palette.va = mem::VirtualFromPhysical(kernel::R32(header + kPaletteData));
    uint32_t size_code = kernel::R32(header + kPaletteCommon) >> kPaletteSizeShift;
    size_t bytes = (std::size(palette.colours) >> size_code) * sizeof(uint32_t);
    std::memcpy(palette.colours, kernel::HostPtr(palette.va), bytes);
    palette.hash = Hash(palette.colours, sizeof(palette.colours));
}

uint32_t SimpleState(const State& s, uint32_t method) { return s.simple[method >> 2]; }

constexpr uint32_t kQuadVertices = 6;  // a quad drawn as two triangles

DrawShaders ClassifyShaders(const State& s, const Assembled& a, uint32_t count) {
    DrawShaders sh;
    // a shader made from a declaration alone (no program) is the fixed function pipeline on those inputs
    sh.prog = (s.vs_handle & 1) && s.vs && !s.vs->program.empty();
    // up to a quad with no depth test: it may be a pass over the whole target, see QuadHlsl; points never are
    bool points = a.topology == Topology::Points;
    sh.quad = count <= kQuadVertices && !s.rs[kRsZEnable] && !points;
    sh.fixed_transform = !sh.prog && !a.pretransformed;
    if (!sh.prog) sh.fk = MakeFixedKey(s);
    return sh;
}

void EnsureProgramHash(const State& s) {
    if (!s.vs->hash) s.vs->hash = Hash(s.vs->program.data(), s.vs->program.size() * 4) | 1;
}

constexpr uint64_t kDeclarationSalt = 0x10000;
constexpr uint64_t kPretransformedSalt = 0x5A5A;
constexpr uint64_t kQuadSalt = 0xC3C30000;  // the QuadHlsl variant, apart from the plain shader
constexpr uint64_t kPixelKeySalt = 0x9E3779B97F4A7C15ull;

uint64_t VertexKey(const State& s, const Assembled& a, const DrawShaders& sh) {
    uint64_t seed = sh.prog ? s.vs->hash ^ a.regs : Hash(&s.vs_handle, 4, a.regs | kDeclarationSalt);
    uint64_t vkey = Hash(&a.decode, 8, seed);
    if (!sh.prog && a.pretransformed) vkey ^= kPretransformedSalt;
    if (sh.quad) vkey ^= kQuadSalt;
    if (!sh.prog) vkey = Hash(&sh.fk, sizeof(sh.fk), vkey);
    return vkey;
}

std::string VertexHlsl(const State& s, const Assembled& a, const DrawShaders& sh) {
    if (sh.prog) return VertexProgramHlsl(s.vs->program, a.regs, a.decode);
    return FixedVertexHlsl(a.regs, a.pretransformed, a.decode, sh.fk);
}

void KeyBlend(const State& s, PsoKey& k) {
    k.blend = SimpleState(s, kMBlendEnable) != 0;
    if (k.blend) {
        k.src = SimpleState(s, kMSrcBlend);
        k.dst = SimpleState(s, kMDestBlend);
        k.op = SimpleState(s, kMBlendOp);
    }
    k.wmask = ColorWriteMask(SimpleState(s, kMColorWriteEnable));
}

constexpr float kDepth24Max = 16777215.0f;

void KeyRasterizer(const State& s, const Assembled& a, PsoKey& k) {
    constexpr uint32_t kCullCw = 0x900;  // D3DCULL_CW
    uint32_t cull = s.rs[kRsCullMode];
    k.cull = cull != 0 && a.topology == Topology::Triangles;
    k.ccw = cull == kCullCw;  // culling clockwise leaves counter clockwise as the front
    k.fill = s.rs[kRsFillMode];
}

// alpha test off: compare always, in D3D12's comparison numbering minus 1 like the enabled test
constexpr float kAlphaTestAlways = 7.0f;

float CompareIndex(uint32_t d3d) { return float(d3d >= 0x200 && d3d <= 0x207 ? d3d - 0x200 : 7); }

// D3DRS_FOGTABLEMODE values
constexpr uint32_t kFogTableNone = 0;
constexpr float kFogTableLinear = 3.0f;

// table mode none under a vertex program: oFog is the blend factor, passed on as linear fog from 1 to 0
void FillFogConstants(const State& s, bool vertex_program, DrawConstants& c) {
    if (!s.rs[kRsFogEnable]) return;
    if (vertex_program && s.rs[kRsFogTableMode] == kFogTableNone) {
        c.fog[0] = 1.0f;
        c.fog[1] = 0.0f;
        c.fog[2] = 0.0f;
        c.fog[3] = kFogTableLinear;
        return;
    }
    c.fog[0] = Bits(s.rs[kRsFogStart]);
    c.fog[1] = Bits(s.rs[kRsFogEnd]);
    c.fog[2] = Bits(s.rs[kRsFogDensity]);
    c.fog[3] = float(s.rs[kRsFogTableMode]);
}

void FillSurfaceConstants(const State& s, GuestExtent size, DrawConstants& c) {
    c.surf[0] = float(size.w);
    c.surf[1] = float(size.h);
    c.surf[2] = 1.0f / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? CompareIndex(SimpleState(s, kMAlphaFunc)) : kAlphaTestAlways;
    c.misc[2] = CompareIndex(s.rs[kRsShadowFunc]);
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
}

void FillCombinerConstants(const State& s, DrawConstants& c) {
    for (int i = 0; i < 8; i++) {
        Color(SimpleState(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SimpleState(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SimpleState(s, kMPsFinalConstant0), c.pc[16]);
    Color(SimpleState(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
}

void MatMul(const float* x, const float* y, float* o) {
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++)
            o[r * 4 + col] = x[r * 4] * y[col] + x[r * 4 + 1] * y[4 + col] + x[r * 4 + 2] * y[8 + col] + x[r * 4 + 3] * y[12 + col];
}

void Transpose(const float* m, float* o) {
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++) o[col * 4 + r] = m[r * 4 + col];
}

void InverseTranspose3x3(const float* wv, float* n) {
    float det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) +
                wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    float id = det != 0 ? 1.0f / det : 0.0f;
    for (int r = 0; r < 3; r++) {
        for (int col = 0; col < 3; col++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (col + 1) % 3, c2 = (col + 2) % 3;
            n[r * 4 + col] = (wv[r1 * 4 + c1] * wv[r2 * 4 + c2] - wv[r1 * 4 + c2] * wv[r2 * 4 + c1]) * id;
        }
    }
}

constexpr size_t kTsView = 2;
constexpr size_t kTsProjection = 3;
constexpr size_t kTsTexture0 = 16;
constexpr size_t kTsWorld = 256;

void FillTransformConstants(const State& s, GuestExtent size, DrawConstants& c) {
    float wv[16], wvp[16];
    MatMul(s.transforms[kTsWorld].data(), s.transforms[kTsView].data(), wv);
    MatMul(wv, s.transforms[kTsProjection].data(), wvp);
    const auto& v = s.viewport;
    float sx = float(v.w) / size.w;
    float sy = float(v.h) / size.h;
    float ox = (2.0f * v.x + v.w) / size.w - 1;
    float oy = 1 - (2.0f * v.y + v.h) / size.h;
    float vpm[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, v.max_z - v.min_z, 0, ox, oy, v.min_z, 1};
    float full[16];
    MatMul(wvp, vpm, full);
    Transpose(full, c.wvp);
    Transpose(wv, c.wv);
    float n[16] = {};
    InverseTranspose3x3(wv, n);
    Transpose(n, c.wvn);
    for (int t = 0; t < 4; t++) Transpose(s.transforms[kTsTexture0 + size_t(t)].data(), c.tm[t]);
}

// D3DLIGHT8 as floats
constexpr int kLightFloats = 26;

void LightToView(const std::array<uint32_t, kLightFloats>& l, const float* vm, float (&o)[7][4]) {
    float f[kLightFloats];
    std::memcpy(f, l.data(), sizeof(f));
    for (int j = 0; j < 4; j++) {
        o[0][j] = f[1 + j];
        o[1][j] = f[5 + j];
        o[2][j] = f[9 + j];
    }
    const float* pos = f + 13;
    const float* dir = f + 16;
    for (int j = 0; j < 3; j++) {
        o[3][j] = pos[0] * vm[j] + pos[1] * vm[4 + j] + pos[2] * vm[8 + j] + vm[12 + j];
        o[4][j] = dir[0] * vm[j] + dir[1] * vm[4 + j] + dir[2] * vm[8 + j];
    }
    o[3][3] = f[19];
    o[4][3] = f[20];
    o[5][0] = f[21];
    o[5][1] = f[22];
    o[5][2] = f[23];
    o[6][0] = std::cos(f[24] * 0.5f);
    o[6][1] = std::cos(f[25] * 0.5f);
}

void FillLightConstants(const State& s, const FixedKey& fk, DrawConstants& c) {
    std::memcpy(c.mat, s.material.data(), sizeof(float) * 16);
    c.mat[4][0] = s.material[16];
    Color(s.rs[kRsAmbient], c.amb);
    const float* vm = s.transforms[kTsView].data();
    for (int i = 0; i < 8; i++) {
        if (!((fk.types >> (i * 2)) & 3)) continue;
        LightToView(s.lights[size_t(i)], vm, c.lt[i]);
    }
}

VkPrimitiveTopology PrimitiveTopology(const Assembled& a) {
    if (a.topology == Topology::Points) return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (a.topology == Topology::Lines) return a.strip ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    return a.strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkPrimitiveTopology TopologyClass(uint32_t topo) {
    switch (topo) {
        case 0: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case 1: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

constexpr uint32_t kFillWireframe = 0x1B01;
constexpr float kUpscaledMin = 1.01f;
constexpr float kGlowBoxTexels = 3.0f;

// copies a decoded level's rows into upload memory: rows past the source's are zero
void CopyLevelRows(uint8_t* out, uint32_t rows, uint32_t slices, uint32_t row_size, const std::vector<uint8_t>& px,
                   uint32_t pitch) {
    size_t src_rows = pitch ? px.size() / pitch / std::max(slices, 1u) : 0;
    for (uint32_t z = 0; z < slices; z++) {
        for (uint32_t y = 0; y < rows; y++) {
            uint8_t* dst = out + (size_t(z) * rows + y) * row_size;
            if (y < src_rows) {
                size_t n = std::min<size_t>(pitch, row_size);
                std::memcpy(dst, &px[(size_t(z) * src_rows + y) * pitch], n);
                if (n < row_size) std::memset(dst + n, 0, row_size - n);
            } else {
                std::memset(dst, 0, row_size);
            }
        }
    }
}

struct GuestRect {
    int32_t left, top, right, bottom;
};
struct GuestPoint {
    int32_t x, y;
};

GuestRect ReadGuestRect(uint32_t rects, uint32_t i) {
    uint32_t r = rects + i * 16;
    return {int32_t(kernel::R32(r)), int32_t(kernel::R32(r + 4)), int32_t(kernel::R32(r + 8)), int32_t(kernel::R32(r + 12))};
}

GuestPoint ReadGuestPoint(uint32_t points, uint32_t i) {
    uint32_t p = points + i * 8;
    return {int32_t(kernel::R32(p)), int32_t(kernel::R32(p + 4))};
}

struct DrawnRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, dx = 0, dy = 0;
};

DrawnRect ReadDrawnRect(uint32_t src_rects, uint32_t dst_points, uint32_t i) {
    GuestRect g = ReadGuestRect(src_rects, i);
    DrawnRect r{float(g.left), float(g.top), float(g.right), float(g.bottom), float(g.left), float(g.top)};
    if (dst_points) {
        GuestPoint p = ReadGuestPoint(dst_points, i);
        r.dx = float(p.x);
        r.dy = float(p.y);
    }
    return r;
}

uint32_t ScaleUnsigned(int32_t v, float sc) { return uint32_t(float(uint32_t(v)) * sc); }

constexpr float kTapsSlack = 1e-3f;

}  // namespace

// ---------------------------------------------------------------- shared helpers

namespace renderer {

uint32_t GuestPitch(uint32_t width) { return (width * 4 + 63) & ~63u; }

uint64_t TargetMapKey(uint32_t data, bool depth) { return uint64_t(data) << 1 | (depth ? 1 : 0); }

// MemSig hashes a surface up to kSigWholeMax bytes whole, a bigger one as evenly spaced samples
constexpr uint32_t kSigWholeMax = 65536;
constexpr uint32_t kSigSamples = 64;
constexpr uint32_t kSigSampleBytes = 64;

uint64_t MemSig(uint32_t data, uint32_t total) {
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(data));
    uint64_t sig = Hash(&total, sizeof(total));
    g_perf.sigs++;
    g_perf.sig_bytes += total <= kSigWholeMax ? total : kSigSamples * kSigSampleBytes;
    if (total <= kSigWholeMax) return Hash(p, total, sig);
    for (uint32_t i = 0; i < kSigSamples; i++) {
        const uint8_t* sample = p + uint64_t(total - kSigSampleBytes) * i / (kSigSamples - 1);
        sig = Hash(sample, kSigSampleBytes, sig);
    }
    return sig;
}

// a lock alone proves nothing, reads lock too: only changed bytes mean the CPU wrote it
bool LatchCpuWrite(Target& t) {
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = MemSig(t.data, t.bytes);
    if (sig == t.sig && !Changed(t)) return false;
    t.sig = sig;
    return true;
}

}  // namespace renderer

// ---------------------------------------------------------------- set up

VulkanRenderer::~VulkanRenderer() {
    if (!ctx_.dev) return;
    vkDeviceWaitIdle(ctx_.dev);
    ShutdownUi();
    if (queries_) vkDestroyQueryPool(ctx_.dev, queries_, nullptr);
    for (auto& [k, p] : psos_)
        if (p) vkDestroyPipeline(ctx_.dev, p, nullptr);
    for (auto& [k, p] : pass_psos_)
        if (p) vkDestroyPipeline(ctx_.dev, p, nullptr);
    for (auto& [k, m] : modules_)
        if (m) vkDestroyShaderModule(ctx_.dev, m, nullptr);
    for (auto& [k, s] : samplers_) vkDestroySampler(ctx_.dev, s, nullptr);
    DropFramebuffers();
    for (auto& [k, p] : passes_)
        if (p) vkDestroyRenderPass(ctx_.dev, p, nullptr);
    for (Frame& f : frames_) {
        for (VkSampler s : f.samplers) vkDestroySampler(ctx_.dev, s, nullptr);
        f.images.clear();
        f.buffers.clear();
        f.upload.reset();
        if (f.pool) vkDestroyCommandPool(ctx_.dev, f.pool, nullptr);
    }
    targets_.clear();
    textures_.clear();
    late_.clear();
    for (auto& d : dummy_) d.reset();
    lut_.reset();
    if (cache_) vkDestroyPipelineCache(ctx_.dev, cache_, nullptr);
    if (layout_) vkDestroyPipelineLayout(ctx_.dev, layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_.dev, set_layout_, nullptr);
    swap_.reset();
}

bool VulkanRenderer::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    std::string why;
    if (!CompilerAvailable(why)) {
        LOGE("vulkan: no shader compiler: %s", why.c_str());
        return false;
    }
    std::vector<const char*> exts;
    if (!headless_) exts = app::VulkanInstanceExtensions();
    if (!ctx_.Init(exts.data(), uint32_t(exts.size()), !headless_)) return false;
    if (!headless_) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!app::CreateVulkanSurface(ctx_.instance, reinterpret_cast<uint64_t*>(&surface))) {
            LOGE("vulkan: no window surface");
            return false;
        }
        swap_ = std::make_unique<Swapchain>();
        if (!swap_->Init(ctx_, surface)) return false;
        // the chain's images are made again on resize: the framebuffers that hold their views go with them
        swap_->on_views_gone = [this] { DropSwapchainFramebuffers(); };
    }
    g_push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(ctx_.dev, "vkCmdPushDescriptorSet"));
    if (!g_push) g_push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(ctx_.dev, "vkCmdPushDescriptorSetKHR"));
    if (!g_push) {
        LOGE("vulkan: no push descriptors");
        return false;
    }
    CreateLayouts();
    if (!CreateFrames()) return false;
    Begin();
    CreateDummies();
    lut_ = ctx_.CreateImage({.format = kBgra, .w = 256, .h = 1});
    if (!lut_) return false;
    Barrier(*lut_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    scale_ = headless_ ? headless_scale_ : app::RenderScale(guest_h_);
    LOGI("vulkan: %ux%u guest, render scale %.3f", width, height, scale_);
    return true;
}

// the D3D12 root signature as one push descriptor set: b0 b1, t0 to t3, s0 to s3
void VulkanRenderer::CreateLayouts() {
    VkDescriptorSetLayoutBinding b[kBindingCount] = {};
    for (uint32_t i = 0; i < kBindingCount; i++) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
        b[i].descriptorType = i < kBindingTextures   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : i < kBindingSamplers ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                     : VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    li.bindingCount = kBindingCount;
    li.pBindings = b;
    vkCreateDescriptorSetLayout(ctx_.dev, &li, nullptr, &set_layout_);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    vkCreatePipelineLayout(ctx_.dev, &pl, nullptr, &layout_);
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCreatePipelineCache(ctx_.dev, &ci, nullptr, &cache_);
}

bool VulkanRenderer::CreateFrames() {
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = ctx_.family;
        if (vkCreateCommandPool(ctx_.dev, &pi, nullptr, &f.pool) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(ctx_.dev, &ai, &f.cb);
        f.upload = ctx_.CreateBuffer(kUploadSize,
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     Memory::Upload);
        if (!f.upload) return false;
    }
    return true;
}

// null views for stages without a texture: a black transparent texel of each kind
void VulkanRenderer::CreateDummies() {
    dummy_[kTex2D] = ctx_.CreateImage({.format = kBgra});
    dummy_[kTex3D] = ctx_.CreateImage({.format = kBgra, .depth = 2});
    dummy_[kTexCube] = ctx_.CreateImage({.format = kBgra, .layers = 6, .cube = true});
    dummy_[kTexNone] = dummy_[kTex2D];
    VkClearColorValue black{};
    for (int i = 1; i < 4; i++) {
        Image& d = *dummy_[i];
        Barrier(d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, d.desc.layers};
        vkCmdClearColorImage(Cb(), d.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &r);
        Barrier(d, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}



// the sample count the device has for the scene's formats, the one asked for or the next under it


// ---------------------------------------------------------------- recording

void VulkanRenderer::Begin() {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult br = vkBeginCommandBuffer(Cb(), &bi);
    if (br != VK_SUCCESS) LOGE("vulkan: command buffer begin failed (%d)", int(br));
    rendering_ = false;
    open_ = {};
    bound_rt_ = bound_ds_ = nullptr;
}

void VulkanRenderer::Submit(bool wait, VkSemaphore acquired, VkSemaphore done) {
    EndRendering();
    VkCommandBuffer cb = Cb();
    if (presents_ < 10)
        LOGI("vulkan: submit %llu cb=%p frame=%llu pool=%p", (unsigned long long)presents_, (void*)cb,
             (unsigned long long)frame_, (void*)CurrentFrame().pool);
    vkEndCommandBuffer(cb);
    Frame& f = CurrentFrame();
    f.fence = ++fence_value_;
    VkSemaphoreSubmitInfo waits{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waits.semaphore = acquired;
    waits.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signals[2] = {{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    signals[0].semaphore = ctx_.timeline;
    signals[0].value = fence_value_;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].semaphore = done;
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cb;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = acquired ? 1 : 0;
    si.pWaitSemaphoreInfos = &waits;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = done ? 2 : 1;
    si.pSignalSemaphoreInfos = signals;
    if (VkResult r = ctx_.d.QueueSubmit2(ctx_.queue, 1, &si, VK_NULL_HANDLE); r != VK_SUCCESS) {
        LOGE("vulkan: submit failed (%d)", int(r));
    }
    if (wait) {
        ctx_.Wait(fence_value_);
        vkResetCommandPool(ctx_.dev, f.pool, 0);
        DropSlotFramebuffers();
        f.images.clear();
        f.buffers.clear();
        for (VkSampler s : f.samplers) vkDestroySampler(ctx_.dev, s, nullptr);
        f.samplers.clear();
        f.used = 0;
        Begin();
    }
}

void VulkanRenderer::Flush() { Submit(true); }

VulkanRenderer::Alloc VulkanRenderer::Upload(uint64_t size, uint64_t align) {
    Frame& f = CurrentFrame();
    g_perf.upload += size;
    uint64_t o = (f.used + align - 1) & ~(align - 1);
    if (o + size <= kUploadSize) {
        f.used = o + size;
        return {f.upload->buffer, o, f.upload->map + o};
    }
    // too big for the ring: a buffer of its own, released with the frame
    g_perf.overflow++;
    BufferRef b = ctx_.CreateBuffer(std::max<uint64_t>(size, 256),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    Memory::Upload);
    f.buffers.push_back(b);
    return {b->buffer, 0, b->map};
}

void VulkanRenderer::EndRendering() {
    if (!rendering_) return;
    if (ctx_.caps.dynamic_rendering) ctx_.d.CmdEndRendering(Cb());
    else vkCmdEndRenderPass(Cb());
    rendering_ = false;
    open_ = {};
}

void VulkanRenderer::Barrier(Image& img, VkImageLayout to) {
    if (img.layout == to && to != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) return;
    EndRendering();
    Transition(ctx_, Cb(), img, to);
}

// a render pass for a pipeline's attachment formats: laid out as the scopes leave their attachments, loaded
// and stored, with the load ordered after the barrier that put the attachment in that layout
VkRenderPass VulkanRenderer::RenderPassFor(VkFormat color, bool depth) {
    uint64_t key = uint64_t(color) | (uint64_t(depth) << 32) | (uint64_t(ctx_.caps.depth_format) << 33);
    auto it = passes_.find(key);
    if (it != passes_.end()) return it->second;
    VkAttachmentDescription att[2]{};
    VkAttachmentReference color_ref{}, depth_ref{};
    uint32_t n = 0;
    if (color != VK_FORMAT_UNDEFINED) {
        att[n].format = color;
        att[n].samples = VK_SAMPLE_COUNT_1_BIT;
        att[n].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[n].initialLayout = att[n].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_ref.attachment = n++;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (depth) {
        att[n].format = ctx_.caps.depth_format;
        att[n].samples = VK_SAMPLE_COUNT_1_BIT;
        att[n].loadOp = att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[n].storeOp = att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[n].initialLayout = att[n].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_ref.attachment = n++;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = color != VK_FORMAT_UNDEFINED ? 1 : 0;
    sub.pColorAttachments = color != VK_FORMAT_UNDEFINED ? &color_ref : nullptr;
    sub.pDepthStencilAttachment = depth ? &depth_ref : nullptr;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = n;
    rp.pAttachments = att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(ctx_.dev, &rp, nullptr, &pass) != VK_SUCCESS) LOGE("vulkan: render pass creation failed");
    LOG_FIRST(4, "vulkan: render pass %p color %d depth %d attachments %u", (void*)pass, int(color), int(depth), unsigned(n));
    passes_[key] = pass;
    return pass;
}

VkFramebuffer VulkanRenderer::FramebufferFor(VkRenderPass pass, const Attachments& a, uint32_t w, uint32_t h) {
    if (!pass) return VK_NULL_HANDLE;
    FbKey key;
    key.pass = pass;
    key.swapchain = a.color && !a.color->alloc;
    key.color = a.color ? a.color->target : VK_NULL_HANDLE;
    key.depth = a.depth ? a.depth->target : VK_NULL_HANDLE;
    key.w = w;
    key.h = h;
    auto& cache = framebuffers_[frame_ % kFrames];
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    VkImageView views[2] = {key.color, key.depth};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = pass;
    fi.attachmentCount = (a.color ? 1u : 0u) + (a.depth ? 1u : 0u);
    fi.pAttachments = views + (a.color ? 0 : 1);
    fi.width = w;
    fi.height = h;
    fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(ctx_.dev, &fi, nullptr, &fb) != VK_SUCCESS) LOGE("vulkan: framebuffer creation failed");
    cache[key] = fb;
    return fb;
}

// the current slot's recording is done, so the framebuffers it made can go: before the frame's images, whose
// views they hold
void VulkanRenderer::DropSlotFramebuffers() {
    auto& cache = framebuffers_[frame_ % kFrames];
    for (auto& [k, fb] : cache)
        if (fb) vkDestroyFramebuffer(ctx_.dev, fb, nullptr);
    cache.clear();
}

void VulkanRenderer::DropFramebuffers() {
    for (auto& cache : framebuffers_) {
        for (auto& [k, fb] : cache)
            if (fb) vkDestroyFramebuffer(ctx_.dev, fb, nullptr);
        cache.clear();
    }
}

void VulkanRenderer::DropSwapchainFramebuffers() {
    uint32_t dropped = 0, kept = 0;
    for (auto& cache : framebuffers_) {
        for (auto it = cache.begin(); it != cache.end();) {
            if (!it->first.swapchain) { ++kept; ++it; continue; }
            if (it->second) vkDestroyFramebuffer(ctx_.dev, it->second, nullptr);
            it = cache.erase(it);
            ++dropped;
        }
    }
    LOG_FIRST(2, "vulkan: swapchain cleanup dropped %u framebuffers, kept %u offscreen", dropped, kept);
}

void VulkanRenderer::BeginRendering(const Attachments& a, uint32_t w, uint32_t h) {
    if (rendering_ && open_ == a) return;
    EndRendering();
    if (SkipPart("norp")) return;
    // a scope's loads read what an earlier scope stored: ordered by a dependency, not by luck
    AttachmentDependency(ctx_, Cb());
    if (a.color) Barrier(*a.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (a.depth) Barrier(*a.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if (ctx_.caps.dynamic_rendering) {
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (a.color) {
            color.imageView = a.color->target;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        if (a.depth) {
            depth.imageView = a.depth->target;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {w, h}};
        ri.layerCount = 1;
        ri.colorAttachmentCount = a.color ? 1 : 0;
        ri.pColorAttachments = a.color ? &color : nullptr;
        ri.pDepthAttachment = a.depth ? &depth : nullptr;
        ri.pStencilAttachment = a.depth ? &depth : nullptr;
        LOG_FIRST(4, "vulkan: dynamic rendering scope %ux%u color %p depth %p", w, h,
                  (void*)(a.color ? a.color->target : VK_NULL_HANDLE), (void*)(a.depth ? a.depth->target : VK_NULL_HANDLE));
        ctx_.d.CmdBeginRendering(Cb(), &ri);
    } else {
        VkRenderPass pass = RenderPassFor(a.color ? a.color->desc.format : VK_FORMAT_UNDEFINED, a.depth != nullptr);
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        bi.renderPass = pass;
        bi.framebuffer = FramebufferFor(pass, a, w, h);
        bi.renderArea = {{0, 0}, {w, h}};
        LOG_FIRST(4, "vulkan: render pass scope %ux%u pass %p framebuffer %p format %d depth %d", w, h, (void*)bi.renderPass,
                  (void*)bi.framebuffer, int(a.color ? a.color->desc.format : VK_FORMAT_UNDEFINED), int(a.depth != nullptr));
        if (!bi.renderPass || !bi.framebuffer)
            LOGE("vulkan: begin render pass %p framebuffer %p (%ux%u)", bi.renderPass, bi.framebuffer, w, h);
        vkCmdBeginRenderPass(Cb(), &bi, VK_SUBPASS_CONTENTS_INLINE);
    }
    rendering_ = true;
    open_ = a;
}

// Direct3D's y up: the viewport is flipped so clip space and winding stay as the title has them
void VulkanRenderer::SetViewport(float x, float y, float w, float h) {
    VkViewport vp{x, y + h, w, -h, 0, 1};
    vkCmdSetViewport(Cb(), 0, 1, &vp);
}

void VulkanRenderer::SetScissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    VkRect2D r{{std::max(x0, 0), std::max(y0, 0)}, {uint32_t(std::max(x1 - std::max(x0, 0), 0)), uint32_t(std::max(y1 - std::max(y0, 0), 0))}};
    vkCmdSetScissor(Cb(), 0, 1, &r);
}

void VulkanRenderer::SetFullViewport(uint32_t w, uint32_t h) {
    SetViewport(0, 0, float(w), float(h));
    SetScissor(0, 0, int32_t(w), int32_t(h));
}

VkShaderModule VulkanRenderer::Compile(const std::string& src, const char* entry, Stage stage, uint64_t& hash) {
    uint64_t seed = Hash(entry, std::strlen(entry), uint64_t(stage) + 1);
    hash = Hash(src.data(), src.size(), seed);
    auto it = modules_.find(hash);
    if (it != modules_.end()) return it->second;
    PerfScope perf_compile{g_perf.compile};
    g_perf.shaders++;
    std::string error;
    std::vector<uint32_t> spirv = CompileHlsl(src, entry, stage, error);
    VkShaderModule m = VK_NULL_HANDLE;
    if (spirv.empty()) {
        LOGE("vulkan: %s compile failed: %s", entry, error.c_str());
        LOGE("%s", src.c_str());
    } else {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = spirv.size() * 4;
        ci.pCode = spirv.data();
        if (vkCreateShaderModule(ctx_.dev, &ci, nullptr, &m) != VK_SUCCESS) {
            LOGE("vulkan: %s: the shader module was refused (%u words)", entry, unsigned(spirv.size()));
            m = VK_NULL_HANDLE;
        }
    }
    modules_[hash] = m;
    return m;
}

VkSampler VulkanRenderer::EnsureSampler(const SamplerDesc& d) {
    uint64_t key = Hash(&d, sizeof(d));
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    // a device's sampler count is limited: the cache starts over, the old ones go with the frame
    constexpr size_t kMaxSamplers = 2000;
    if (samplers_.size() >= kMaxSamplers) {
        for (auto& [k, s] : samplers_) CurrentFrame().samplers.push_back(s);
        samplers_.clear();
    }
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = d.mag;
    ci.minFilter = d.min;
    ci.mipmapMode = d.mip;
    ci.addressModeU = d.u;
    ci.addressModeV = d.v;
    ci.addressModeW = d.w;
    ci.mipLodBias = d.lod_bias;
    ci.anisotropyEnable = d.anisotropy > 1 && ctx_.caps.max_anisotropy > 1;
    ci.maxAnisotropy = std::min(float(d.anisotropy), ctx_.caps.max_anisotropy);
    ci.compareEnable = d.compare != 0;
    ci.compareOp = d.compare_op;
    ci.minLod = d.min_lod;
    ci.maxLod = d.max_lod;
    VkSamplerCustomBorderColorCreateInfoEXT border{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    if (ctx_.caps.custom_border) {
        std::memcpy(border.customBorderColor.float32, d.border, sizeof(d.border));
        border.format = VK_FORMAT_UNDEFINED;
        ci.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        ci.pNext = &border;
    } else {
        // the nearest of the three fixed ones
        bool white = d.border[0] + d.border[1] + d.border[2] > 1.5f;
        ci.borderColor = d.border[3] < 0.5f ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                         : white            ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                            : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(ctx_.dev, &ci, nullptr, &s);
    samplers_[key] = s;
    return s;
}

// the full screen passes' clamp samplers: linear at s0, point at s1 to s3
VkSampler VulkanRenderer::BlitSampler(bool linear) {
    SamplerDesc d;
    d.min = d.mag = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    d.mip = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    d.u = d.v = d.w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return EnsureSampler(d);
}

void VulkanRenderer::PushDescriptors(VkBuffer cb0, uint64_t off0, uint64_t size0, VkBuffer cb1, uint64_t off1,
                                    uint64_t size1, Image* const tex[4], const uint8_t kinds[4], VkSampler const smp[4]) {
    VkDescriptorBufferInfo bufs[2] = {{cb0, off0, size0}, {cb1, off1, size1}};
    VkDescriptorImageInfo imgs[4], smps[4];
    VkWriteDescriptorSet w[kBindingCount];
    for (uint32_t i = 0; i < kBindingCount; i++) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    for (int i = 0; i < 2; i++) {
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[i].pBufferInfo = &bufs[i];
    }
    for (int i = 0; i < 4; i++) {
        Image* img = tex[i] ? tex[i] : dummy_[kinds ? kinds[i] : kTex2D].get();
        imgs[i] = {VK_NULL_HANDLE, img->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        w[kBindingTextures + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[kBindingTextures + i].pImageInfo = &imgs[i];
        smps[i] = {smp[i], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        w[kBindingSamplers + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w[kBindingSamplers + i].pImageInfo = &smps[i];
    }
    g_push(Cb(), VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, kBindingCount, w);
}

// ---------------------------------------------------------------- targets

void VulkanRenderer::ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it) {
    if (bound_rt_ == &it->second || bound_ds_ == &it->second) bound_rt_ = bound_ds_ = nullptr;
    if (open_.color == it->second.res.get() || open_.depth == it->second.res.get()) EndRendering();
    Release(std::move(it->second.res));
    Release(std::move(it->second.fb));
    targets_.erase(it);
}

// the host image of a new target at render scale, cleared as D3D12 clears a view
bool VulkanRenderer::CreateTargetResource(Target& t) {
    t.w = std::max(1u, uint32_t(std::lround(t.gw * scale_)));
    t.h = std::max(1u, uint32_t(std::lround(t.gh * scale_)));
    ImageDesc d;
    d.format = t.depth ? ctx_.caps.depth_format : t.fmt;
    d.w = t.w;
    d.h = t.h;
    d.target = true;
    d.depth_stencil = t.depth;
    t.res = ctx_.CreateImage(d);
    if (!t.res) {
        LOGE("vulkan: no %ux%u target", t.w, t.h);
        return false;
    }
    Image& img = *t.res;
    Barrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageSubresourceRange r{img.aspect, 0, 1, 0, 1};
    if (t.depth) {
        if (SkipPart("nodsclear")) return true;
        VkClearDepthStencilValue one{1, 0};
        vkCmdClearDepthStencilImage(Cb(), img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &r);
    } else {
        if (SkipPart("nocolclear")) return true;
        VkClearColorValue black{};
        vkCmdClearColorImage(Cb(), img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &r);
    }
    return true;
}

// a smaller depth surface's console size, above the address bits
constexpr int kKeyWidthShift = 34;
constexpr int kKeyHeightShift = 48;

// a smaller depth surface laid on the depth buffer's memory is a target of its own
uint64_t VulkanRenderer::SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const {
    uint64_t key = TargetMapKey(data, depth);
    bool small_depth = depth && (gw != guest_w_ || gh != guest_h_);
    if (!small_depth) return key;
    return key | uint64_t(gw) << kKeyWidthShift | uint64_t(gh) << kKeyHeightShift;
}

VulkanRenderer::TargetSlot VulkanRenderer::FindTargetSlot(const Surface& s, bool depth) {
    TargetSlot slot;
    slot.data = mem::VirtualFromPhysical(s.data);
    SurfaceSize(s, slot.gw, slot.gh);
    slot.key = SizedTargetKey(slot.data, depth, slot.gw, slot.gh);
    slot.it = targets_.find(slot.key);
    slot.sized = slot.it != targets_.end() && slot.it->second.gw == slot.gw && slot.it->second.gh == slot.gh;
    return slot;
}

// long unused: the title freed it and other data is at this address now
bool VulkanRenderer::TargetStale(uint32_t data) const {
    auto it = target_used_.find(data);
    uint64_t used = it == target_used_.end() ? 0 : it->second;
    return frame_ - used > kStaleFrames;
}

Target* VulkanRenderer::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    TargetSlot slot = FindTargetSlot(s, depth);
    if (!slot.sized) return nullptr;
    target_used_[slot.data] = frame_;
    return &slot.it->second;
}

Target* VulkanRenderer::EnsureTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    TargetSlot slot = FindTargetSlot(s, depth);
    target_used_[slot.data] = frame_;
    if (slot.sized) return &slot.it->second;
    if (slot.it != targets_.end()) ReleaseTarget(slot.it);
    Target t;
    t.depth = depth;
    t.gw = slot.gw;
    t.gh = slot.gh;
    if (!CreateTargetResource(t)) return nullptr;
    g_perf.targets++;
    t.data = slot.data;
    t.bytes = depth ? slot.gw * slot.gh * 4 : GuestPitch(slot.gw) * slot.gh;
    t.epoch = DirtyEpoch(slot.data);
    t.sig = MemSig(slot.data, t.bytes);
    Target& ref = targets_[slot.key];
    ref = std::move(t);
    return &ref;
}

bool VulkanRenderer::BindTargets(const State& s) {
    Target* rt = EnsureTarget(s.rt, false);
    Target* ds = s.rs[kRsZEnable] || s.rs[kRsStencilEnable] ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return false;
    if (rt) {
        if (LatchCpuWrite(*rt)) UploadTarget(*rt, s.rt);
        Barrier(*rt->res, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        rt->drawn = true;
    }
    if (ds) {
        Barrier(*ds->res, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        ds->drawn = true;
    }
    bound_rt_ = rt;
    bound_ds_ = ds;
    return true;
}

// BGRA pixels into a new w x h texture, left readable
ImageRef VulkanRenderer::CreatePixelTexture(const std::vector<uint8_t>& px, uint32_t pitch, uint32_t w, uint32_t h) {
    ImageRef tex = ctx_.CreateImage({.format = kBgra, .w = w, .h = h});
    if (!tex) return nullptr;
    Alloc a = Upload(uint64_t(w) * h * 4, 16);
    for (uint32_t y = 0; y < h; y++) std::memcpy(a.cpu + size_t(y) * w * 4, &px[size_t(y) * pitch], w * 4);
    Barrier(*tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.bufferOffset = a.off;
    c.imageSubresource = Layers(*tex);
    c.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(Cb(), a.buf, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Barrier(*tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return tex;
}

// CPU pixels of a colour surface into its host target, scaled
void VulkanRenderer::UploadTarget(Target& t, const Surface& s) {
    g_perf.rtups++;
    TextureDesc d;
    if (!Describe(s.header, d) || Format(d.format).depth) return;
    std::vector<uint8_t> px;
    uint32_t pitch;
    // after a Sync only the pixels the CPU changed go over the picture, which stays at render scale
    bool over = !t.drawn && d.width == t.gw && d.height == t.gh && t.read.size() == size_t(t.gw) * t.gh * 4;
    if (over) {
        LOG_FIRST(4, "vulkan: CPU pixels drawn over target %08X", t.data);
        pitch = t.gw * 4;
        px = LatchCpuChanges(t);
    } else {
        DecodeLevel(d, 0, 0, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), nullptr, px, pitch);
    }
    ImageRef tex = CreatePixelTexture(px, pitch, d.width, d.height);
    if (!tex) return;
    BeginRendering({t.res.get(), nullptr}, t.w, t.h);
    SetFullViewport(t.w, t.h);
    float uv[4] = {0, 0, 1, 1};
    Blit(*tex, uv, {.alpha = true, .rtv = t.fmt, .over = over});
    bound_rt_ = bound_ds_ = nullptr;
    Release(std::move(tex));
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
}

// a depth buffer drawn as colour into another depth buffer's memory: copied as depth
bool VulkanRenderer::DepthCopy(const State& s) {
    TextureDesc d;
    if (!s.rt.header || !s.textures[0] || !Describe(s.textures[0], d) || Format(d.format).depth || d.cube || d.depth != 1)
        return false;
    uint32_t gw, gh;
    SurfaceSize(s.rt, gw, gh);
    auto di = targets_.find(SizedTargetKey(mem::VirtualFromPhysical(s.rt.data), true, gw, gh));
    auto si = targets_.find(SizedTargetKey(d.data, true, d.width, d.height));
    if (di == targets_.end() || si == targets_.end() || di == si) return false;
    Target& dst = di->second;
    Target& src = si->second;
    VkPipeline pso = EnsureDepthCopyPipeline();
    if (!pso) return false;
    if (cap_) fprintf(cap_, "%04u depth copy %08X to %08X\n", cap_i_++, src.data, dst.data);
    Barrier(*src.res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginRendering({nullptr, dst.res.get()}, dst.w, dst.h);
    SetFullViewport(dst.w, dst.h);
    Image* tex[4] = {src.res.get(), nullptr, nullptr, nullptr};
    float zero[4] = {};
    DrawFullScreen(pso, zero, sizeof(zero), tex);
    dst.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
    return true;
}

const char* kDepthHlsl = R"(
Texture2D<float> img : register(t0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float psmain(O i) : SV_Depth { return img.SampleLevel(ps, i.uv, 0); }
)";

// DepthCopy's pipeline: writes depth only, always passing; null when it can't be made
VkPipeline VulkanRenderer::EnsureDepthCopyPipeline() {
    VkPipeline& pso = pass_psos_[~0ull];
    if (pso) return pso;
    uint64_t h;
    VkShaderModule vs = Compile(kDepthHlsl, "vs", Stage::Vertex, h);
    VkShaderModule ps = Compile(kDepthHlsl, "psmain", Stage::Pixel, h);
    if (!vs || !ps) return VK_NULL_HANDLE;
    pso = FullScreenPipeline(vs, ps, VK_FORMAT_UNDEFINED, nullptr, true);
    return pso;
}


// a copy of the target to read while drawing to it
Image* VulkanRenderer::Feedback(Target& t) {
    if (!t.fb) {
        ImageDesc d = t.res->desc;
        d.target = false;
        t.fb = ctx_.CreateImage(d);
        if (!t.fb) return nullptr;
    }
    Barrier(*t.res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Barrier(*t.fb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy c{Layers(*t.res), {}, Layers(*t.fb), {}, {t.w, t.h, 1}};
    vkCmdCopyImage(Cb(), t.res->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.fb->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Barrier(*t.fb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return t.fb.get();
}


// ---------------------------------------------------------------- full screen passes

VkPipeline VulkanRenderer::FullScreenPipeline(VkShaderModule vs, VkShaderModule ps, VkFormat rtv,
                                              const VkPipelineColorBlendAttachmentState* blend, bool depth_out) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = ps;
    st[1].pName = "main";
    // DXC names every entry point main in SPIR-V unless told: the source entry is what -E chose
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (depth_out) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    }
    VkPipelineColorBlendAttachmentState plain{};
    plain.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = rtv != VK_FORMAT_UNDEFINED ? 1 : 0;
    cb.pAttachments = blend ? blend : &plain;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = uint32_t(std::size(dyn));
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = cb.attachmentCount;
    ri.pColorAttachmentFormats = &rtv;
    if (depth_out) ri.depthAttachmentFormat = ri.stencilAttachmentFormat = ctx_.caps.depth_format;
    VkRenderPass pass = RenderPassFor(rtv, depth_out);
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                    ctx_.caps.dynamic_rendering ? static_cast<const void*>(&ri) : nullptr};
    pi.renderPass = pass;
    pi.subpass = 0;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = layout_;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult pr = vkCreateGraphicsPipelines(ctx_.dev, cache_, 1, &pi, nullptr, &p);
    if (pr != VK_SUCCESS) {
        LOGE("vulkan: pass pipeline failed (%d) format %d depth %d render pass %s", int(pr), int(rtv), int(depth_out),
             pass ? "made" : "null");
        p = VK_NULL_HANDLE;
    }
    return p;
}

// the full screen triangle: constants at b0 and b1, tex at t0 to t3, s0 and s1 as given, s2 and s3 point
void VulkanRenderer::DrawFullScreen(VkPipeline pso, const void* constants, uint32_t bytes, Image* const tex[4]) {
    Alloc cb = Upload(256, 256);
    std::memcpy(cb.cpu, constants, std::min<uint32_t>(bytes, 256));
    vkCmdBindPipeline(Cb(), VK_PIPELINE_BIND_POINT_GRAPHICS, pso);
    VkSampler point = BlitSampler(false);
    VkSampler smp[4] = {BlitSampler(true), point, point, point};
    PushDescriptors(cb.buf, cb.off, 256, cb.buf, cb.off, 256, tex, nullptr, smp);
    vkCmdDraw(Cb(), 3, 1, 0, 0);
}

VkPipeline VulkanRenderer::EnsureBlitPipeline(VkFormat rtv, bool over) {
    VkPipeline& pso = pass_psos_[uint64_t(rtv) | uint64_t(over) << 32];
    if (pso) return pso;
    uint64_t h;
    VkShaderModule vs = Compile(kBlitHlsl, "vs", Stage::Vertex, h);
    VkShaderModule ps = Compile(kBlitHlsl, "psmain", Stage::Pixel, h);
    if (!vs || !ps) return VK_NULL_HANDLE;
    VkPipelineColorBlendAttachmentState b{};
    b.colorWriteMask = 0xF;
    if (over) {
        // premultiplied colour, the target keeps its alpha
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    pso = FullScreenPipeline(vs, ps, rtv, &b);
    return pso;
}

// src over the open scope's viewport: src and the gamma lookup are readable, the target open
void VulkanRenderer::Blit(Image& src, const float uvrect[4], const BlitOptions& o) {
    VkPipeline pso = EnsureBlitPipeline(o.rtv, o.over);
    if (!pso) return;
    float c[12] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3], o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f, float(o.taps), o.mul, float(o.rotation), 0, 0, 0};
    Image* tex[4] = {&src, lut_.get(), nullptr, nullptr};
    DrawFullScreen(pso, c, sizeof(c), tex);
}



// ---------------------------------------------------------------- textures

Image* VulkanRenderer::BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t, DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    if (&t == draw_ds_) return nullptr;
    bool feedback = &t == draw_rt_;
    stages.rt[stage] = true;
    if (!t.depth && LatchCpuWrite(t)) {
        Surface su;
        su.header = s.textures[size_t(stage)];
        UploadTarget(t, su);
    }
    info.kind = kTex2D;
    // a pass shrinking it averages each tap over a console texel, else upscaled taps skip texels and ghost
    Target* rt = FindTarget(s.rt, false);
    float k = float(t.w) / t.gw;
    if (!t.depth && k > kUpscaledMin && rt && rt != &t && (t.gw > rt->gw || t.gh > rt->gh)) {
        stages.wide[stage] = k;
        info.flags |= kTexWide;
    }
    // the console reads the pixels it is about to overwrite: give the draw a copy
    if (feedback) return Feedback(t);
    Barrier(*t.res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (t.depth) info.shadow = ShadowDepthKind(d.format);
    return t.res.get();
}

bool VulkanRenderer::CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, uint64_t key) {
    g_perf.newtex++;
    Release(std::move(tx.res));
    tx = Tex{};
    tx.d = d;
    tx.fmt = HostFormat(fi.host);
    uint32_t w = d.width, h = d.height;
    tx.levels = d.levels;
    if (IsBlockCompressed(tx.fmt) && ((w & 3) || (h & 3))) {
        w = (w + 3) & ~3u;
        h = (h + 3) & ~3u;
        tx.levels = 1;
    }
    ImageDesc id;
    id.format = tx.fmt;
    id.w = w;
    id.h = h;
    id.depth = d.depth;
    id.layers = d.cube ? 6 : 1;
    id.levels = tx.levels;
    id.cube = d.cube;
    tx.res = ctx_.CreateImage(id);
    if (!tx.res) {
        LOG_FIRST(8, "vulkan: no %ux%u texture, format %X", w, h, d.format);
        textures_.erase(key);
        return false;
    }
    return true;
}

void VulkanRenderer::UploadTextureLevel(Tex& tx, uint32_t f, uint32_t l, const std::vector<uint8_t>& px, uint32_t pitch) {
    const ImageDesc& id = tx.res->desc;
    uint32_t w = std::max(1u, id.w >> l), h = std::max(1u, id.h >> l), z = std::max(1u, id.depth >> l);
    uint32_t row = RowBytes(tx.fmt, w), rows = RowCount(tx.fmt, h);
    Alloc a = Upload(uint64_t(row) * rows * z, 16);
    CopyLevelRows(a.cpu, rows, z, row, px, pitch);
    VkBufferImageCopy c{};
    c.bufferOffset = a.off;
    c.imageSubresource = Layers(*tx.res, l, f);
    c.imageExtent = {w, h, z};
    vkCmdCopyBufferToImage(Cb(), a.buf, tx.res->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
}

void VulkanRenderer::UploadTexture(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours) {
    Barrier(*tx.res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uint32_t faces = d.cube ? 6 : 1;
    std::vector<uint8_t> px;
    for (uint32_t f = 0; f < faces; f++) {
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            UploadTextureLevel(tx, f, l, px, pitch);
        }
    }
    Barrier(*tx.res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// a texture in guest memory: made again when its layout changed, uploaded again when its bytes did
Image* VulkanRenderer::BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi,
                                       DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    StagePalette palette;
    ReadStagePalette(s, stage, d, palette);
    const uint64_t key = uint64_t(palette.va) << 32 | d.data;
    Tex& tx = textures_[key];
    uint32_t total = TotalSize(d);
    bool same = SameTexture(tx, d);
    if (!same && !CreateTexture(tx, d, fi, key)) return nullptr;
    // cheap change check once per frame: lock epochs plus a sample of the bytes
    if (tx.frame != presents_ + 1 || !same || tx.palette != palette.hash) {
        tx.frame = presents_ + 1;
        tx.palette = palette.hash;
        const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
        auto perf_t = PerfClock::now();
        uint64_t sig = MemSig(d.data, total) ^ DirtyEpoch(d.data) ^ palette.hash;
        g_perf.sig += PerfMs(perf_t);
        if (sig != tx.sig || !same) {
            PerfScope perf_up{g_perf.texup};
            g_perf.texups++;
            g_perf.texup_bytes += total;
            tx.sig = sig;
            UploadTexture(tx, d, p, palette.va ? palette.colours : nullptr);
        }
    }
    stages.levels[stage] = tx.levels;
    if (fi.depth) info.shadow = ShadowDepthKind(d.format);
    return tx.res.get();
}

Image* VulkanRenderer::BindTexture(const State& s, int stage, DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    info = {};
    stages.rt[stage] = false;
    stages.wide[stage] = 0;
    stages.levels[stage] = 1;
    TextureDesc d;
    if (!s.textures[size_t(stage)] || !Describe(s.textures[size_t(stage)], d)) return nullptr;
    const FormatInfo& fi = Format(d.format);
    info.kind = TextureKind(d);
    info.rect = d.linear;
    // rendered by the GPU: sample the target itself; DXT data loaded over a target's memory is not it
    auto tit = targets_.find(SizedTargetKey(d.data, fi.depth, d.width, d.height));
    if (tit != targets_.end() && !d.cube && d.depth == 1 && !fi.compressed)
        return BindTargetTexture(s, stage, d, tit->second, stages);
    return BindGuestTexture(s, stage, d, fi, stages);
}

// textures may have recorded copies or passes that moved the targets out of their attachment layouts
bool VulkanRenderer::BindStageTexturesAndTargets(const State& s, DrawTextures& stages) {
    auto perf_t = PerfClock::now();
    draw_rt_ = bound_rt_;
    draw_ds_ = bound_ds_;
    for (int i = 0; i < 4; i++) stages.res[i] = BindTexture(s, i, stages);
    g_perf.bind += PerfMs(perf_t);
    // textures may have recorded copies that moved the targets out of their attachment layouts
    return BindTargets(s);
}

// ---------------------------------------------------------------- draws

void VulkanRenderer::EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh) {
    auto vit = shader_src_.find(sh.vkey);
    if (vit != shader_src_.end()) {
        sh.vh = vit->second;
        sh.vs = modules_[sh.vh];
        return;
    }
    std::string src = VertexHlsl(s, a, sh);
    sh.vs = Compile(sh.quad ? QuadHlsl(std::move(src)) : src, "main", Stage::Vertex, sh.vh);
    shader_src_[sh.vkey] = sh.vh;
}

void VulkanRenderer::EnsurePixelShader(const PixelKey& pk, DrawShaders& sh) {
    auto pit = shader_src_.find(sh.pkey ^ kPixelKeySalt);
    if (pit != shader_src_.end()) {
        sh.ph = pit->second;
        sh.ps = modules_[sh.ph];
        return;
    }
    sh.ps = Compile(PixelHlsl(pk), "main", Stage::Pixel, sh.ph);
    shader_src_[sh.pkey ^ kPixelKeySalt] = sh.ph;
}

void VulkanRenderer::KeyDepthStencil(const State& s, const Assembled& a, PsoKey& k) {
    k.zen = s.rs[kRsZEnable] != 0;
    if (k.zen) {
        k.zwrite = SimpleState(s, kMZWriteEnable) != 0;
        k.zfunc = SimpleState(s, kMZFunc);
    }
    k.sten = s.rs[kRsStencilEnable] != 0;
    if (k.sten) {
        k.sfunc = SimpleState(s, kMStencilFunc);
        k.smask = SimpleState(s, kMStencilMask) & 0xFF;
        k.swmask = SimpleState(s, kMStencilWriteMask) & 0xFF;
        k.sfail = s.rs[kRsStencilFail];
        k.szfail = SimpleState(s, kMStencilZFail);
        k.spass = SimpleState(s, kMStencilPass);
    }
    // polygon offset: bias in depth buffer units, the target is 24 bit (or float near 1, where its units match);
    // the slope is per console pixel, and an upscaled target's pixels are smaller
    if (a.topology == Topology::Triangles && SimpleState(s, kMSolidOffsetEnable)) {
        k.zbias = int32_t(std::lround(Bits(SimpleState(s, kMDepthOffset)) * kDepth24Max / s.zscale));
        k.zslope = Bits(SimpleState(s, kMSlopeScale)) * (bound_ds_ ? float(bound_ds_->w) / bound_ds_->gw : 1.0f);
    }
}

void VulkanRenderer::BuildPipelineKey(const State& s, const Assembled& a, uint64_t vh, uint64_t ph, PsoKey& k) {
    std::memset(&k, 0, sizeof(k));
    k.vs = vh;
    k.ps = ph;
    k.regs = a.regs;
    for (int r = 0; r < 16; r++) {
        if ((a.regs >> r) & 1) {
            const VertexElement& e = a.elements[size_t(r)];
            k.elements[r] = e.format | uint32_t(e.buffer) << 8 | uint32_t(e.offset) << 16;
        }
    }
    k.topo = uint32_t(a.topology);
    k.has_rt = bound_rt_ != nullptr;
    k.has_ds = bound_ds_ != nullptr;
    k.rtfmt = bound_rt_ ? uint32_t(bound_rt_->fmt) : 0;
    KeyBlend(s, k);
    if (k.has_ds) KeyDepthStencil(s, a, k);
    KeyRasterizer(s, a, k);
}

VkPipeline VulkanRenderer::EnsurePipeline(const PsoKey& k, VkShaderModule vs, VkShaderModule ps) {
    uint64_t h = Hash(&k, sizeof(k));
    auto it = psos_.find(h);
    if (it != psos_.end()) return it->second;
    g_perf.psos++;
    bool points = k.topo == uint32_t(Topology::Points);
    // the key's vertex attributes, in register order: location n is the n-th register the shader reads
    VkVertexInputAttributeDescription attrs[16];
    VkVertexInputBindingDescription binds[16];
    uint32_t na = 0, nb = 0;
    bool bound[16] = {};
    for (uint32_t r = 0; r < 16; r++) {
        if (!((k.regs >> r) & 1)) continue;
        uint32_t e = k.elements[r];
        uint32_t buffer = (e >> 8) & 0xFF;
        attrs[na] = {na, buffer, VertexFormat(e & 0xFF), e >> 16};
        na++;
        if (!bound[buffer]) {
            bound[buffer] = true;
            binds[nb++] = {buffer, 0, VK_VERTEX_INPUT_RATE_VERTEX};  // the stride is set per draw
        }
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = nb;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = na;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineShaderStageCreateInfo st[3] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = ps;
    st[1].pName = "main";
    uint32_t stages = 2;
    // a point is drawn as its whole console pixel by a geometry shader, solid, front facing and never culled
    if (points && ctx_.caps.geometry) {
        uint64_t gh;
        if (VkShaderModule gs = Compile(PointHlsl(), "main", Stage::Geometry, gh)) {
            st[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
            st[2].module = gs;
            st[2].pName = "main";
            stages = 3;
        }
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = TopologyClass(k.topo);
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = k.fill == kFillWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = k.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = k.ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = k.zbias != 0 || k.zslope != 0;
    rs.depthBiasConstantFactor = float(k.zbias);
    rs.depthBiasSlopeFactor = k.zslope;
    rs.lineWidth = 1;
    if (stages == 3) {
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    }
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k.zen;
    ds.depthWriteEnable = k.zwrite;
    ds.depthCompareOp = Compare(k.zfunc);
    ds.stencilTestEnable = k.sten;
    VkStencilOpState so{StencilOp(k.sfail), StencilOp(k.spass), StencilOp(k.szfail), Compare(k.sfunc), k.smask, k.swmask, 0};
    ds.front = ds.back = so;
    VkPipelineColorBlendAttachmentState b{};
    b.blendEnable = k.blend;
    b.srcColorBlendFactor = Blend(k.src, false);
    b.dstColorBlendFactor = Blend(k.dst, false);
    b.colorBlendOp = BlendOp(k.op);
    b.srcAlphaBlendFactor = Blend(k.src, true);
    b.dstAlphaBlendFactor = Blend(k.dst, true);
    b.alphaBlendOp = BlendOp(k.op);
    b.colorWriteMask = k.wmask;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = k.has_rt ? 1 : 0;
    cb.pAttachments = &b;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,
                            VK_DYNAMIC_STATE_BLEND_CONSTANTS,   VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = uint32_t(std::size(dyn));
    dy.pDynamicStates = dyn;
    VkFormat rtfmt = VkFormat(k.rtfmt);
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = k.has_rt ? 1 : 0;
    ri.pColorAttachmentFormats = &rtfmt;
    if (k.has_ds) ri.depthAttachmentFormat = ri.stencilAttachmentFormat = ctx_.caps.depth_format;
    VkRenderPass pass = RenderPassFor(k.has_rt ? rtfmt : VK_FORMAT_UNDEFINED, k.has_ds != 0);
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                    ctx_.caps.dynamic_rendering ? static_cast<const void*>(&ri) : nullptr};
    pi.renderPass = pass;
    pi.subpass = 0;
    pi.stageCount = stages;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = layout_;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult pr = vkCreateGraphicsPipelines(ctx_.dev, cache_, 1, &pi, nullptr, &p);
    if (pr != VK_SUCCESS) {
        LOGE("vulkan: pipeline creation failed (%d) format %d", int(pr), int(pi.pColorBlendState->attachmentCount));
        p = VK_NULL_HANDLE;
    }
    psos_[h] = p;
    return p;
}

float VulkanRenderer::PointScale() const {
    const Target* t = bound_rt_ ? bound_rt_ : bound_ds_;
    if (!t || !t->gw) return 1;
    return float(t->w) / float(t->gw);
}

void VulkanRenderer::FillTexelConstants(int i, const TextureDesc& d, const DrawTextures& stages, DrawConstants& c) {
    c.texsize[i][0] = float(d.width);
    c.texsize[i][1] = float(d.height);
    // the console samples pixels at integer positions, the host half a pixel on: targets are read half a texel back
    if (!stages.rt[i]) return;
    c.texsize[i][2] = -0.5f / float(d.width);
    c.texsize[i][3] = -0.5f / float(d.height);
    // a 3 console texel box per tap keeps the console chain's glow width and brightness
    if (stages.wide[i] > 0) {
        float n = std::ceil(kGlowBoxTexels * stages.wide[i] * 0.5f);
        c.spread[i][0] = kGlowBoxTexels / (n * float(d.width));
        c.spread[i][1] = kGlowBoxTexels / (n * float(d.height));
        c.spread[i][2] = n;
    }
}

void VulkanRenderer::FillTextureConstants(const State& s, const DrawTextures& stages, DrawConstants& c) {
    for (int i = 0; i < 4; i++) {
        TextureDesc d;
        if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], d)) FillTexelConstants(i, d, stages, c);
        const auto& t = s.tss[size_t(i)];
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
}

void VulkanRenderer::UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages,
                                    GuestExtent size, Alloc& vc, Alloc& dc) {
    vc = Upload(sizeof(s.vsc), 256);
    std::memcpy(vc.cpu, s.vsc.data(), sizeof(s.vsc));
    dc = Upload(sizeof(DrawConstants), 256);
    DrawConstants c{};
    FillSurfaceConstants(s, size, c);
    // points: host pixels per console pixel, also keeps them off the far edge pull out of screen passes
    if (k.topo == uint32_t(Topology::Points)) c.surf[3] = PointScale();
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    FillTextureConstants(s, stages, c);
    if (sh.fixed_transform) {
        FillTransformConstants(s, size, c);
        FillLightConstants(s, sh.fk, c);
    }
    std::memcpy(dc.cpu, &c, sizeof(c));
}

// viewport covers the target, the guest viewport is in the shader; scissor clips to it
void VulkanRenderer::SetViewportScissor(const State& s, uint32_t gw) {
    float W = float(bound_rt_ ? bound_rt_->w : bound_ds_->w);
    float H = float(bound_rt_ ? bound_rt_->h : bound_ds_->h);
    SetViewport(0, 0, W, H);
    float sc = W / gw;
    int32_t x0 = int32_t(s.viewport.x * sc);
    int32_t y0 = int32_t(s.viewport.y * sc);
    int32_t x1 = int32_t((s.viewport.x + s.viewport.w) * sc);
    int32_t y1 = int32_t((s.viewport.y + s.viewport.h) * sc);
    if (s.scissor_count) {
        x0 = std::max(x0, int32_t(s.scissors[0] * sc));
        y0 = std::max(y0, int32_t(s.scissors[1] * sc));
        x1 = std::min(x1, int32_t(s.scissors[2] * sc));
        y1 = std::min(y1, int32_t(s.scissors[3] * sc));
    }
    SetScissor(std::clamp(x0, 0, int32_t(W)), std::clamp(y0, 0, int32_t(H)), std::clamp(x1, 0, int32_t(W)),
               std::clamp(y1, 0, int32_t(H)));
}

void VulkanRenderer::DrawGeometry(const Assembled& a) {
    VkBuffer bufs[16];
    VkDeviceSize offs[16], strides[16];
    for (uint32_t i = 0; i < a.buffer_count; i++) {
        const VertexBuffer& b = a.buffers[i];
        Alloc vb = Upload(std::max(b.bytes, 4u), 16);
        std::memcpy(vb.cpu, b.src, b.bytes);
        bufs[i] = vb.buf;
        offs[i] = vb.off;
        strides[i] = b.stride;
    }
    if (a.buffer_count) ctx_.d.BindVertexBuffers2(Cb(), 0, a.buffer_count, bufs, offs, nullptr, strides);
    ctx_.d.SetPrimitiveTopology(Cb(), PrimitiveTopology(a));
    if (a.index_count) {
        uint32_t ibytes = a.index_count * (a.index16 ? 2 : 4);
        Alloc ib = Upload(ibytes, 4);
        std::memcpy(ib.cpu, a.index_src, ibytes);
        vkCmdBindIndexBuffer(Cb(), ib.buf, ib.off, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(Cb(), a.index_count, 1, 0, a.base_vertex, 0);
    } else {
        vkCmdDraw(Cb(), a.draw_count, 1, 0, 0);
    }
}

bool VulkanRenderer::AssembleDraw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices,
                                 Assembled& a) {
    auto perf_t = PerfClock::now();
    bool assembled = Assemble(s, prim, start, count, indices, a);
    g_perf.assemble += PerfMs(perf_t);
    g_perf.verts += a.vertex_count;
    g_perf.raw += a.vertices.empty();
    return assembled;
}

void VulkanRenderer::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    PerfScope perf_draw{g_perf.draw};
    g_perf.draws++;
    if (cap_) CapStep(s.rt);
    if (DepthCopy(s)) return;
    Assembled& a = asm_;
    if (!AssembleDraw(s, prim, start, count, indices, a)) return;
    // past the ring every upload is a buffer of its own: seconds lost on a frame that draws a whole level once
    if (CurrentFrame().used > kUploadSize - kUploadReserve) Submit(true);
    if (!BindTargets(s)) return;

    DrawShaders sh = ClassifyShaders(s, a, count);
    if (sh.prog) EnsureProgramHash(s);
    sh.vkey = VertexKey(s, a, sh);
    EnsureVertexShader(s, a, sh);

    DrawTextures stages;
    if (!BindStageTexturesAndTargets(s, stages)) return;
    PixelKey pk = MakePixelKey(s, stages.info);
    pk.quad = sh.quad;
    sh.pkey = Hash(&pk, sizeof(pk));
    EnsurePixelShader(pk, sh);
    if (!sh.vs || !sh.ps) return;

    PsoKey k;
    BuildPipelineKey(s, a, sh.vh, sh.ph, k);
    VkPipeline pso = EnsurePipeline(k, sh.vs, sh.ps);
    if (!pso) return;

    GuestExtent size;
    SurfaceSize(bound_rt_ ? s.rt : s.zs, size.w, size.h);
    Alloc vc, dc;
    UploadConstants(s, sh, k, stages, size, vc, dc);
    if (cap_) {
        DrawConstants c{};
        std::memcpy(&c, dc.cpu, sizeof(c));
        CaptureDraw(s, prim, count, k, c, stages);
        for (unsigned r = 0; r < 16; ++r) {
            if (!(a.regs & (1u << r))) continue;
            const auto& e = a.elements[r];
            const auto& b = a.buffers[e.buffer];
            fprintf(cap_, "     input v%u format %u stride %u offset %u decode %llu\n", r,
                    unsigned(e.format), b.stride, unsigned(e.offset), (unsigned long long)a.decode);
            for (unsigned i = 0; i < std::min(a.vertex_count, 4u); ++i) {
                size_t offset = size_t(i) * b.stride + e.offset;
                if (offset + 16 > b.bytes) break;
                float v[4];
                std::memcpy(v, b.src + offset, sizeof(v));
                fprintf(cap_, "       vertex %u %.9g %.9g %.9g %.9g\n", i, v[0],v[1],v[2],v[3]);
            }
        }
        std::string vsrc = VertexHlsl(s, a, sh);
        CaptureShader("vs", sh.vkey, sh.quad ? QuadHlsl(std::move(vsrc)) : vsrc);
        CaptureShader("ps", sh.pkey, PixelHlsl(pk));
    }

    // samplers read the bound views' levels; a stage without a texture gets a dummy of its kind
    bool clamp = UnitTexcoords(a);
    uint32_t aniso = headless_ ? 0 : app::Get().aniso;
    VkSampler smp[4];
    uint8_t kinds[4];
    for (int i = 0; i < 4; i++) {
        smp[i] = EnsureSampler(StageSampler(s, i, clamp, aniso, stages.levels[i] > 1, stages.info[i].shadow != 0));
        kinds[i] = stages.info[i].kind;
    }

    Target* dims = bound_rt_ ? bound_rt_ : bound_ds_;
    BeginRendering({bound_rt_ ? bound_rt_->res.get() : nullptr, bound_ds_ ? bound_ds_->res.get() : nullptr}, dims->w, dims->h);
    vkCmdBindPipeline(Cb(), VK_PIPELINE_BIND_POINT_GRAPHICS, pso);
    PushDescriptors(vc.buf, vc.off, sizeof(s.vsc), dc.buf, dc.off, sizeof(DrawConstants), stages.res, kinds, smp);
    SetViewportScissor(s, size.w);
    float bf[4];
    Color(SimpleState(s, kMBlendColor), bf);
    vkCmdSetBlendConstants(Cb(), bf);
    vkCmdSetStencilReference(Cb(), VK_STENCIL_FACE_FRONT_AND_BACK, SimpleState(s, kMStencilRef) & 0xFF);
    // geometry, counted while the title runs a visibility test
    uint32_t query = BeginCountedDraw(s);
    DrawGeometry(a);
    EndCountedDraw(query);
}

// ---------------------------------------------------------------- visibility tests

bool VulkanRenderer::EnsureQueries() {
    if (queries_) return true;
    if (queries_tried_) return false;
    queries_tried_ = true;
    if (!ctx_.caps.queries) {
        LOG_FIRST(1, "vulkan: no precise occlusion queries, visibility tests stay pending");
        return false;
    }
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_OCCLUSION;
    qi.queryCount = kQueries;
    if (vkCreateQueryPool(ctx_.dev, &qi, nullptr, &queries_) != VK_SUCCESS) queries_ = VK_NULL_HANDLE;
    return queries_ != VK_NULL_HANDLE;
}

// a draw while the title counts visible pixels gets an occlusion query of its own, kNoQuery otherwise; the ring holds
// several frames of counted draws, so a query is free again by the time it comes round
uint32_t VulkanRenderer::BeginCountedDraw(const State& s) {
    if (!s.simple[kMCountEnable >> 2] || !EnsureQueries()) return kNoQuery;
    uint32_t query = query_next_++ % kQueries;
    ctx_.d.ResetQueryPool(ctx_.dev, queries_, query, 1);
    vkCmdBeginQuery(Cb(), queries_, query, VK_QUERY_CONTROL_PRECISE_BIT);
    return query;
}

void VulkanRenderer::EndCountedDraw(uint32_t query) {
    if (query == kNoQuery) return;
    vkCmdEndQuery(Cb(), queries_, query);
    counted_.push_back({query, SamplesPerPixel()});
}

// the console counts its own pixels: the bound target's scale comes out
float VulkanRenderer::SamplesPerPixel() const {
    const Target* t = bound_rt_ ? bound_rt_ : bound_ds_;
    if (!t || !t->gw || !t->gh) return 1;
    return float(t->w) * float(t->h) / (float(t->gw) * float(t->gh));
}

// reports whose draws the GPU has finished, written as the console's GPU writes them: time, count, then 0 for done
void VulkanRenderer::DeliverReports() {
    constexpr uint32_t kTime = 0;
    constexpr uint32_t kTimeHigh = 4;
    constexpr uint32_t kCount = 8;
    constexpr uint32_t kStatus = 12;
    uint64_t done = ctx_.Completed();
    while (!reports_.empty() && reports_.front().fence <= done) {
        const Report& r = reports_.front();
        double pixels = 0;
        for (const Query& q : r.queries) {
            uint64_t n = 0;
            vkGetQueryPoolResults(ctx_.dev, queries_, q.index, 1, sizeof(n), &n, sizeof(n), VK_QUERY_RESULT_64_BIT);
            pixels += double(n) / q.samples_per_pixel;
        }
        kernel::W32(r.va + kTime, uint32_t(presents_));
        kernel::W32(r.va + kTimeHigh, 0);
        kernel::W32(r.va + kCount, uint32_t(std::lround(pixels)));
        std::atomic_thread_fence(std::memory_order_release);
        kernel::W32(r.va + kStatus, 0);
        reports_.pop_front();
    }
}

// ---------------------------------------------------------------- clears

void VulkanRenderer::ClearTarget(Target& t, const State& s, const ClearArgs& c) {
    float sc = float(t.w) / t.gw;
    auto scaled = [&](int x0, int y0, int x1, int y1) {
        int32_t l = std::clamp(int32_t(float(x0) * sc), 0, int32_t(t.w));
        int32_t tp = std::clamp(int32_t(float(y0) * sc), 0, int32_t(t.h));
        int32_t r = std::clamp(int32_t(float(x1) * sc), 0, int32_t(t.w));
        int32_t b = std::clamp(int32_t(float(y1) * sc), 0, int32_t(t.h));
        return VkClearRect{{{l, tp}, {uint32_t(std::max(r - l, 0)), uint32_t(std::max(b - tp, 0))}}, 0, 1};
    };
    std::vector<VkClearRect> rs;
    if (c.count && c.rects) {
        for (uint32_t i = 0; i < c.count; i++) {
            int32_t r[4];
            std::memcpy(r, c.rects + size_t(i) * sizeof(r), sizeof(r));
            rs.push_back(scaled(r[0], r[1], r[2], r[3]));
        }
    } else {
        const auto& v = s.viewport;
        rs.push_back(scaled(int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h)));
    }
    rs.erase(std::remove_if(rs.begin(), rs.end(), [](const VkClearRect& r) { return !r.rect.extent.width || !r.rect.extent.height; }),
             rs.end());
    // Full-surface clears need no render pass. In particular, older Adreno
    // drivers fail when finalizing a legacy pass containing only a clear.
    const bool full = rs.size() == 1 && rs[0].rect.offset.x == 0 && rs[0].rect.offset.y == 0 &&
                      rs[0].rect.extent.width == t.w && rs[0].rect.extent.height == t.h;
    if (full) {
        if (!t.depth && LatchCpuWrite(t)) UploadTarget(t, s.rt);
        VkImageAspectFlags aspect = t.depth
            ? (((c.flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
               ((c.flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
            : VK_IMAGE_ASPECT_COLOR_BIT;
        if (aspect) {
            Barrier(*t.res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
            if (t.depth) {
                VkClearDepthStencilValue value{std::clamp(c.z, 0.0f, 1.0f), c.stencil & 0xFF};
                vkCmdClearDepthStencilImage(Cb(), t.res->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            } else {
                VkClearColorValue value{};
                Color(c.color, value.float32);
                vkCmdClearColorImage(Cb(), t.res->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            }
        }
        t.drawn = true;
        return;
    }
    VkClearAttachment ca{};
    if (t.depth) {
        ca.aspectMask = ((c.flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) | ((c.flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        ca.clearValue.depthStencil = {std::clamp(c.z, 0.0f, 1.0f), c.stencil & 0xFF};
        if (!rs.empty() && ca.aspectMask && !SkipPart("norp")) {
            BeginRendering({nullptr, t.res.get()}, t.w, t.h);
            vkCmdClearAttachments(Cb(), 1, &ca, uint32_t(rs.size()), rs.data());
        }
    } else {
        // pixels the CPU wrote before this clear go in first, a later upload would paint over the clear
        if (LatchCpuWrite(t)) UploadTarget(t, s.rt);
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Color(c.color, ca.clearValue.color.float32);
        if (!rs.empty() && !SkipPart("norp")) {
            BeginRendering({t.res.get(), nullptr}, t.w, t.h);
            vkCmdClearAttachments(Cb(), 1, &ca, uint32_t(rs.size()), rs.data());
        }
    }
    t.drawn = true;
}

void VulkanRenderer::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                          uint32_t stencil) {
    PerfScope perf_clear{g_perf.clear};
    if (cap_) {
        CapStep(s.rt);
        fprintf(cap_, "%04u clear rt %08X ds %08X flags %X color %08X z %.3f\n", cap_i_++, s.rt.data, s.zs.data, flags, color, z);
    }
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? EnsureTarget(s.zs, true) : nullptr;
    ClearArgs c{count, rects, flags, color, z, stencil};
    for (Target* t : {rt, ds})
        if (t) ClearTarget(*t, s, c);
}

// ---------------------------------------------------------------- copies

// one Blit per rect: for formats that differ or a multisampled target
void VulkanRenderer::CopyByDraw(Target& src_t, Target& dst_t, const CopyList& l) {
    Image& src = *src_t.res;
    Barrier(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginRendering({dst_t.res.get(), nullptr}, dst_t.w, dst_t.h);
    float src_scale = float(src_t.w) / src_t.gw;
    float dst_scale = float(dst_t.w) / dst_t.gw;
    bool listed = l.count && l.src_rects;
    uint32_t n = listed ? l.count : 1;
    for (uint32_t i = 0; i < n; i++) {
        DrawnRect r{0, 0, float(std::min(src_t.gw, dst_t.gw)), float(std::min(src_t.gh, dst_t.gh))};
        if (listed) r = ReadDrawnRect(l.src_rects, l.dst_points, i);
        if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
        float vx = r.dx * dst_scale, vy = r.dy * dst_scale, vw = (r.x1 - r.x0) * dst_scale, vh = (r.y1 - r.y0) * dst_scale;
        int32_t right = std::min(int32_t(vx + vw), int32_t(dst_t.w));
        int32_t bottom = std::min(int32_t(vy + vh), int32_t(dst_t.h));
        if (right <= int32_t(vx) || bottom <= int32_t(vy)) continue;
        SetViewport(vx, vy, vw, vh);
        SetScissor(int32_t(vx), int32_t(vy), right, bottom);
        float uv[4] = {r.x0 / src_t.gw, r.y0 / src_t.gh, (r.x1 - r.x0) / src_t.gw, (r.y1 - r.y0) / src_t.gh};
        uint32_t taps = uint32_t(std::ceil(std::max(src_scale / dst_scale, 1.0f) - kTapsSlack));
        Blit(src, uv, {.alpha = true, .taps = taps, .rtv = dst_t.fmt});
    }
    dst_t.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
}

// one image copy per rect, clamped to both targets
void VulkanRenderer::CopyByRegion(Target& src_t, Target& dst_t, const CopyList& l) {
    Image& src = *src_t.res;
    Barrier(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Barrier(*dst_t.res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    float dst_scale = float(dst_t.w) / dst_t.gw;
    bool listed = l.count && l.src_rects;
    uint32_t n = listed ? l.count : 1;
    std::vector<VkImageCopy> regions;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t left = 0, top = 0, right = std::min(src_t.w, dst_t.w), bottom = std::min(src_t.h, dst_t.h);
        uint32_t dx = 0, dy = 0;
        if (listed) {
            GuestRect r = ReadGuestRect(l.src_rects, i);
            left = ScaleUnsigned(r.left, dst_scale);
            top = ScaleUnsigned(r.top, dst_scale);
            right = std::min(ScaleUnsigned(r.right, dst_scale), src_t.w);
            bottom = std::min(ScaleUnsigned(r.bottom, dst_scale), src_t.h);
            dx = left;
            dy = top;
            if (l.dst_points) {
                GuestPoint p = ReadGuestPoint(l.dst_points, i);
                dx = ScaleUnsigned(p.x, dst_scale);
                dy = ScaleUnsigned(p.y, dst_scale);
            }
        }
        if (right <= left || bottom <= top || dx >= dst_t.w || dy >= dst_t.h) continue;
        right = std::min(right, left + dst_t.w - dx);
        bottom = std::min(bottom, top + dst_t.h - dy);
        VkImageCopy c{Layers(src), {int32_t(left), int32_t(top), 0}, Layers(*dst_t.res), {int32_t(dx), int32_t(dy), 0},
                      {right - left, bottom - top, 1}};
        regions.push_back(c);
    }
    if (!regions.empty())
        vkCmdCopyImage(Cb(), src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_t.res->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(regions.size()), regions.data());
    dst_t.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
}

void VulkanRenderer::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    PerfScope perf_copy{g_perf.copy};
    Target* src_t = FindTarget(src, false);
    if (cap_) {
        const char* from = src_t ? (src_t->drawn ? "target" : "idle target") : "memory";
        fprintf(cap_, "%04u copy %08X %s to %08X\n", cap_i_++, src.data, from, dst.data);
    }
    // pixels the CPU made: the copy happens in memory, textures and targets pick it up from there
    if (!src_t || !src_t->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points)) {
            LOG_FIRST(8, "vulkan: CopyRects %08X to %08X skipped", src.data, dst.data);
        } else if (Target* dst_t = FindTarget(dst, false)) {
            UploadTarget(*dst_t, dst);
        }
        return;
    }
    Target* dst_t = EnsureTarget(dst, false);
    // EnsureTarget may have made another target: src_t's entry is unmoved (unordered_map keeps nodes)
    if (!dst_t || src_t == dst_t) return;
    CopyList l{count, src_rects, dst_points};
    // formats that differ are drawn across
    if (src_t->fmt != dst_t->fmt) {
        CopyByDraw(*src_t, *dst_t, l);
        return;
    }
    CopyByRegion(*src_t, *dst_t, l);
    if (cap_) {
        char name[32];
        snprintf(name, sizeof(name), "%04u_copy_%08X", cap_i_, dst_t->data);
        CapSave(*dst_t, name);
    }
}

void VulkanRenderer::SetGamma(const uint8_t* ramp) {
    std::memcpy(ramp_, ramp, sizeof(ramp_));
    ramp_dirty_ = true;
}


std::unique_ptr<VulkanRenderer> CreateHeadlessRenderer(float scale) { return std::make_unique<VulkanRenderer>(true, scale); }

}  // namespace gpu::vk

namespace gpu {

// the SDK's backend on Linux; a game with a renderer of its own (src/vk) leaves this file out
std::unique_ptr<Backend> CreateBackend() { return std::make_unique<vk::VulkanRenderer>(); }

}  // namespace gpu
