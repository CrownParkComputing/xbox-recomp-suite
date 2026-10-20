#pragma once
// The title's Direct3D 8 calls drawn with Vulkan (Linux): gpu/d3d12.cpp carried over call for call on the Vulkan core
// (vk_context, vk_swapchain, vk_shaders, vk_formats). Console surfaces become targets at render scale, guest textures
// are decoded and cached by address, draws translate their state to pipelines, read-backs bring GPU results to guest
// memory, visibility tests count pixels with occlusion queries, and points are drawn as their console pixel.
// Split by job: vulkan.cpp (frames, targets, textures, draws, clears, copies), vulkan_readback.cpp (Sync and the
// late copies, shrinks, F3 capture), vulkan_present.cpp (present, the settings panel, the frame turning over).
// Games that replace the renderer (a src/vk of their own) build on the same core and leave these files out.
#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gpu/backend.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "gpu/vk_context.h"
#include "gpu/vk_formats.h"
#include "gpu/vk_shaders.h"
#include "gpu/vk_swapchain.h"

namespace gpu::vk {

namespace renderer {

constexpr int kFrames = 3;
constexpr uint64_t kStaleFrames = 8;  // frames a target may go unused and still be read back
constexpr uint64_t kUploadSize = 64ull << 20;
// a draw starting with less than this left in the frame's ring waits for the GPU and starts the ring over
constexpr uint64_t kUploadReserve = 8ull << 20;
constexpr uint32_t kQueries = 8192;  // occlusion queries in flight, several frames of counted draws
constexpr uint32_t kNoQuery = ~0u;
constexpr VkFormat kBgra = VK_FORMAT_B8G8R8A8_UNORM;

using PerfClock = std::chrono::steady_clock;
inline double PerfMs(PerfClock::time_point t) {
    return std::chrono::duration<double, std::milli>(PerfClock::now() - t).count();
}
// perf diagnostic: where a frame's time goes, logged once a second with diag::Perf
struct Perf {
    double draw = 0, assemble = 0, bind = 0, sig = 0, texup = 0, compile = 0, clear = 0, copy = 0, present = 0,
           wait = 0, swap = 0, sync = 0;
    uint64_t draws = 0, verts = 0, upload = 0, overflow = 0, sigs = 0, sig_bytes = 0, texups = 0, texup_bytes = 0,
             newtex = 0, targets = 0, rtups = 0, shrinks = 0, syncs = 0, psos = 0, shaders = 0, raw = 0, lates = 0;
};
extern Perf g_perf;
struct PerfScope {
    double& acc;
    PerfClock::time_point t = PerfClock::now();
    ~PerfScope() { acc += PerfMs(t); }
};

// a console surface the renderer draws into
struct Target {
    ImageRef res;
    bool depth = false;
    uint32_t gw = 0, gh = 0, w = 0, h = 0;  // console size, host size
    uint32_t data = 0, bytes = 0;
    uint64_t epoch = 0, sig = 0;  // last lock seen, guest memory as last matched
    bool drawn = false;           // the GPU wrote it since the last readback
    std::vector<uint8_t> read;    // what Sync left in guest memory
    ImageRef fb;                  // a copy read by draws that sample the target they draw into
    VkFormat fmt = kBgra;
};

struct Tex {
    ImageRef res;
    TextureDesc d;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t levels = 1;
    uint64_t sig = 0, frame = 0;
    uint64_t palette = 0;  // hash of the colours a P8 texture was decoded with
};

// the pipeline's state, hashed as bytes: every field 4 bytes
struct PsoKey {
    uint64_t vs, ps;
    uint32_t regs, topo, has_rt, has_ds;
    uint32_t elements[16];  // per register: DXGI format, buffer << 8, offset << 16
    uint32_t blend, src, dst, op, wmask;
    uint32_t zen, zwrite, zfunc, sten, sfunc, smask, swmask, sfail, szfail, spass;
    uint32_t cull, ccw, fill;
    int32_t zbias;
    float zslope;
    uint32_t rtfmt;
};

struct BlitOptions {
    bool gamma = false;    // through the gamma lookup
    bool alpha = false;    // the source's alpha, else 1
    uint32_t taps = 1;     // n x n box filter per pixel
    float mul = 1;         // colour scale
    VkFormat rtv = kBgra;  // the target's format
    bool over = false;     // premultiplied over the target, which keeps its alpha
    uint32_t rotation = 0; // clockwise quarter turns for presentation
};

struct ShrinkOptions {
    uint32_t taps = 1;
    ImageRef into;  // an image from an earlier call to draw over again
    float mul = 1;
};

struct DrawTextures {
    TexInfo info[4];
    Image* res[4] = {};
    bool rt[4] = {};
    float wide[4] = {};
    uint32_t levels[4] = {1, 1, 1, 1};
};

struct DrawShaders {
    bool prog = false;
    bool quad = false;
    bool fixed_transform = false;
    FixedKey fk;
    uint64_t vkey = 0, vh = 0, pkey = 0, ph = 0;
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
};

struct GuestExtent {
    uint32_t w, h;
};

struct Attachments {
    Image* color = nullptr;
    Image* depth = nullptr;
    bool operator==(const Attachments&) const = default;
};

uint32_t GuestPitch(uint32_t width);
uint64_t TargetMapKey(uint32_t data, bool depth);
uint64_t MemSig(uint32_t data, uint32_t total);
bool LatchCpuWrite(Target& t);

}  // namespace renderer

class VulkanRenderer final : public Backend {
public:
    // headless: no window, no swap chain, Present draws nothing, targets at headless_scale (tests)
    explicit VulkanRenderer(bool headless = false, float headless_scale = 1)
        : headless_(headless), headless_scale_(headless_scale) {}
    ~VulkanRenderer() override;
    bool Init(uint32_t width, uint32_t height) override;
    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    void PresentPaused(const Surface& front) override {
        paused_present_ = true;
        Present(front);
        paused_present_ = false;
    }
    void Sync(uint32_t data, uint32_t size) override;
    void SyncLate(uint32_t data) override;
    bool DeliverLate(uint32_t data) override;
    void RecordLate(uint32_t data) override;
    void Flush() override;
    void SetGamma(const uint8_t* ramp) override;
    void ClearReport() override { counted_.clear(); }
    // done once the commands being recorded, the next fence value, have run
    void GetReport(uint32_t report) override { reports_.push_back({report, counted_, fence_value_ + 1}); }
    uint64_t Presents() const { return presents_; }

private:
    using Target = renderer::Target;
    using Tex = renderer::Tex;
    using PsoKey = renderer::PsoKey;
    using BlitOptions = renderer::BlitOptions;
    using ShrinkOptions = renderer::ShrinkOptions;
    using DrawTextures = renderer::DrawTextures;
    using DrawShaders = renderer::DrawShaders;
    using GuestExtent = renderer::GuestExtent;
    using Attachments = renderer::Attachments;
    static constexpr int kFrames = renderer::kFrames;

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        BufferRef upload;
        uint64_t used = 0;
        uint64_t fence = 0;
        std::vector<ImageRef> images;
        std::vector<BufferRef> buffers;
        std::vector<VkSampler> samplers;
    };
    struct Alloc {
        VkBuffer buf;
        uint64_t off;
        uint8_t* cpu;
    };
    struct TargetSlot {
        uint32_t data;
        uint64_t key;
        uint32_t gw, gh;
        std::unordered_map<uint64_t, Target>::iterator it;
        bool sized;
    };
    struct ClearArgs {
        uint32_t count;
        const uint8_t* rects;
        uint32_t flags;
        uint32_t color;
        float z;
        uint32_t stencil;
    };
    struct CopyList {
        uint32_t count, src_rects, dst_points;
    };
    struct CopySource {
        Image* res;
        ImageRef down;
    };
    struct Late {
        BufferRef rb[renderer::kFrames];
        ImageRef down;
        uint64_t fence[renderer::kFrames] = {};
        uint64_t frame[renderer::kFrames] = {};
        uint64_t delivered = 0;
        uint32_t gw = 0, gh = 0, next = 0;
    };
    struct CapImage {
        Image* res;
        uint32_t w, h;
        float mul = 1;
    };
    // visibility tests: an occlusion query per counted draw, summed into report memory once the GPU is done
    struct Query {
        uint32_t index;
        float samples_per_pixel;  // host samples one console pixel covers
    };
    struct Report {
        uint32_t va;
        std::vector<Query> queries;
        uint64_t fence;
    };

    // recording (vulkan.cpp)
    Frame& CurrentFrame() { return frames_[frame_ % kFrames]; }
    VkCommandBuffer Cb() { return CurrentFrame().cb; }
    Alloc Upload(uint64_t size, uint64_t align = 256);
    void Begin();
    void Submit(bool wait, VkSemaphore acquired = VK_NULL_HANDLE, VkSemaphore done = VK_NULL_HANDLE);
    void Release(ImageRef r) {
        if (r) CurrentFrame().images.push_back(std::move(r));
    }
    void Barrier(Image& img, VkImageLayout to);
    void EndRendering();
    void BeginRendering(const Attachments& a, uint32_t w, uint32_t h);
    // render passes, for a device without Vulkan 1.3 rendering scopes: one pass per attachment format
    // pair for the device's life, one framebuffer per attachment set for the frame slot that made it
    VkRenderPass RenderPassFor(VkFormat color, bool depth);
    VkFramebuffer FramebufferFor(VkRenderPass pass, const Attachments& a, uint32_t w, uint32_t h);
    void DropSlotFramebuffers();
    void DropFramebuffers();
    void DropSwapchainFramebuffers();
    void SetViewport(float x, float y, float w, float h);
    void SetScissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void SetFullViewport(uint32_t w, uint32_t h);
    void CreateLayouts();
    bool CreateFrames();
    void CreateDummies();
    VkSampler EnsureSampler(const SamplerDesc& d);
    VkSampler BlitSampler(bool linear);
    void PushDescriptors(VkBuffer cb0, uint64_t off0, uint64_t size0, VkBuffer cb1, uint64_t off1, uint64_t size1,
                         Image* const tex[4], const uint8_t kinds[4], VkSampler const smp[4]);
    VkShaderModule Compile(const std::string& src, const char* entry, Stage stage, uint64_t& hash);

    // targets
    Target* FindTarget(const Surface& s, bool depth);
    Target* EnsureTarget(const Surface& s, bool depth);
    TargetSlot FindTargetSlot(const Surface& s, bool depth);
    uint64_t SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const;
    bool TargetStale(uint32_t data) const;
    void ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it);
    bool CreateTargetResource(Target& t);
    void UploadTarget(Target& t, const Surface& s);
    ImageRef CreatePixelTexture(const std::vector<uint8_t>& px, uint32_t pitch, uint32_t w, uint32_t h);
    bool BindTargets(const State& s);
    bool DepthCopy(const State& s);
    VkPipeline EnsureDepthCopyPipeline();
    Image* Feedback(Target& t);

    // full screen passes
    VkPipeline FullScreenPipeline(VkShaderModule vs, VkShaderModule ps, VkFormat rtv,
                                  const VkPipelineColorBlendAttachmentState* blend, bool depth_out = false);
    void DrawFullScreen(VkPipeline pso, const void* constants, uint32_t bytes, Image* const tex[4]);
    VkPipeline EnsureBlitPipeline(VkFormat rtv, bool over);
    void Blit(Image& src, const float uvrect[4], const BlitOptions& o);

    // draws, in call order
    bool AssembleDraw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices, Assembled& a);
    void EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh);
    bool BindStageTexturesAndTargets(const State& s, DrawTextures& stages);
    Image* BindTexture(const State& s, int stage, DrawTextures& stages);
    Image* BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t, DrawTextures& stages);
    Image* BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi, DrawTextures& stages);
    bool CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, uint64_t key);
    void UploadTexture(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours);
    void UploadTextureLevel(Tex& tx, uint32_t face, uint32_t level, const std::vector<uint8_t>& px, uint32_t pitch);
    void EnsurePixelShader(const PixelKey& pk, DrawShaders& sh);
    void BuildPipelineKey(const State& s, const Assembled& a, uint64_t vh, uint64_t ph, PsoKey& k);
    void KeyDepthStencil(const State& s, const Assembled& a, PsoKey& k);
    VkPipeline EnsurePipeline(const PsoKey& k, VkShaderModule vs, VkShaderModule ps);
    float PointScale() const;
    void UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages, GuestExtent size,
                         Alloc& vc, Alloc& dc);
    void FillTexelConstants(int i, const TextureDesc& d, const DrawTextures& stages, DrawConstants& c);
    void FillTextureConstants(const State& s, const DrawTextures& stages, DrawConstants& c);
    void SetViewportScissor(const State& s, uint32_t gw);
    void DrawGeometry(const Assembled& a);
    // visibility tests
    bool EnsureQueries();
    uint32_t BeginCountedDraw(const State& s);
    void EndCountedDraw(uint32_t query);
    float SamplesPerPixel() const;
    void DeliverReports();

    // clears and copies
    void ClearTarget(Target& t, const State& s, const ClearArgs& c);
    void CopyByDraw(Target& src_t, Target& dst_t, const CopyList& l);
    void CopyByRegion(Target& src_t, Target& dst_t, const CopyList& l);

    // read-backs and capture (vulkan_readback.cpp)
    CopySource PrepareCopySource(Target& t, const ShrinkOptions& o);
    ImageRef Shrink(Image& src, uint32_t w, uint32_t h, const ShrinkOptions& o = {});
    bool ReadBack(Image& res, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
    void ResetLate(Late& l, const Target& t);
    Image* PrepareLateSource(Target& t, Late& l);
    void RecordLateCopy(Late& l, uint32_t slot, Image& src);
    void CapSave(const CapImage& img, const char* name);
    void CapSave(Target& t, const char* name);
    void CapStep(const Surface& rt);
    void CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PsoKey& k, const DrawConstants& c,
                     const DrawTextures& stages);
    void CaptureShader(const char* kind, uint64_t key, const std::string& src);
    void FinishCapture(const Surface& front);
    void StartCapture();

    // present (vulkan_present.cpp)
    void InitUi(VkFormat format);
    void ShutdownUi();
    void RenderUi(Image& into);
    void ConfigureSwap();
    void UploadGammaRamp();
    void DrawFrame(Target* t, Image& bb);
    void BlitFrame(Target& t, Image& bb);
    void ReleaseOldTextures();
    void ResetFrame();
    void CountFps();
    void ApplySettings();

    bool headless_;
    float headless_scale_;
    Context ctx_;
    std::unique_ptr<Swapchain> swap_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    Frame frames_[kFrames];
    uint64_t frame_ = 0;
    bool paused_present_ = false;
    uint64_t fence_value_ = 0;
    Attachments open_;
    bool rendering_ = false;
    std::unordered_map<uint64_t, VkShaderModule> modules_;
    std::unordered_map<uint64_t, uint64_t> shader_src_;
    std::unordered_map<uint64_t, VkPipeline> psos_;
    std::unordered_map<uint64_t, VkPipeline> pass_psos_;
    // the render pass fall-back's passes, and the framebuffers of the frame slot being recorded
    struct FbKey {
        bool swapchain = false;
        VkRenderPass pass = VK_NULL_HANDLE;
        VkImageView color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
        uint32_t w = 0, h = 0;
        bool operator==(const FbKey&) const = default;
    };
    struct FbKeyHash {
        size_t operator()(const FbKey& k) const {
            auto mix = [](size_t h, uint64_t v) { return size_t(h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))); };
            size_t h = mix(0, uint64_t(k.pass));
            h = mix(h, uint64_t(k.color));
            h = mix(h, uint64_t(k.depth));
            return mix(h, (uint64_t(k.w) << 32) | k.h);
        }
    };
    std::unordered_map<uint64_t, VkRenderPass> passes_;
    std::unordered_map<FbKey, VkFramebuffer, FbKeyHash> framebuffers_[kFrames];
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint32_t, uint64_t> target_used_;  // target address to the frame it was last used
    std::unordered_map<uint64_t, Tex> textures_;
    ImageRef dummy_[4];
    ImageRef lut_;
    uint8_t ramp_[768] = {};
    bool ramp_dirty_ = false;
    bool gamma_ = false;
    Target* bound_rt_ = nullptr;
    Target* bound_ds_ = nullptr;
    Target* draw_rt_ = nullptr;
    Target* draw_ds_ = nullptr;
    uint32_t guest_w_ = 640, guest_h_ = 480;
    float scale_ = 1.0f;
    std::unordered_map<uint32_t, Late> late_;
    std::mutex late_lock_;
    std::atomic<uint64_t> presents_seen_{0};
    Assembled asm_;
    VkQueryPool queries_ = VK_NULL_HANDLE;
    bool queries_tried_ = false;
    uint32_t query_next_ = 0;
    std::vector<Query> counted_;
    std::deque<Report> reports_;
    bool ui_ready_ = false;
    VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
    VkFormat ui_format_ = VK_FORMAT_UNDEFINED;  // the format the panel's pipelines were made for; ImGui keeps a
                                                // pointer to this, so it must outlive them
    uint64_t presents_ = 0;
    double fps_ = 0;
    renderer::PerfClock::time_point fps_t_ = renderer::PerfClock::now();
    uint64_t fps_n_ = 0;
    FILE* cap_ = nullptr;
    std::filesystem::path cap_dir_;
    uint32_t cap_i_ = 0, cap_rt_ = 0;
    std::unordered_set<uint64_t> cap_sh_;
};

// a renderer without a window, its targets at scale times the console's size: the tests'
std::unique_ptr<VulkanRenderer> CreateHeadlessRenderer(float scale = 1);

// a file in the exe dir whose name alone is a switch, read once: a device run drops one part of the frame with it to
// find which part the driver objects to (adb shell run-as com.nfshp2.recomp touch files/noblit)
bool SkipPart(const char* part);

}  // namespace gpu::vk
