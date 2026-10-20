// Present: the front surface into the window, letterboxed, the settings panel on top; then the frame turns over and
// the settings that remake targets apply
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "app/app.h"
#include "core/host_pause.h"
#include "core/diag.h"
#include "core/log.h"
#include "gpu/vulkan.h"

namespace gpu::vk {

using namespace renderer;

namespace {


void LogPerf() {
    static PerfClock::time_point perf_start = PerfClock::now();
    static uint64_t perf_frames = 0;
    perf_frames++;
    double el = PerfMs(perf_start);
    if (el < 1000.0) return;
    Perf& p = g_perf;
    double n = double(perf_frames);
    double host = p.draw + p.clear + p.copy + p.present + p.sync;
    LOGI("perf: %.1f fps %.2f ms/frame | host %.2f (draw %.2f: assemble %.2f bind %.2f [check %.2f, upload %.2f] compile %.2f; "
         "clear %.2f copy %.2f sync %.2f present %.2f [swap %.2f wait %.2f]) other %.2f",
         n * 1000.0 / el, el / n, host / n, p.draw / n, p.assemble / n, p.bind / n, p.sig / n, p.texup / n, p.compile / n,
         p.clear / n, p.copy / n, p.sync / n, p.present / n, p.swap / n, p.wait / n, (el - host) / n);
    LOGI("perf: title thread per frame: recording %.2f ms, waiting %.2f ms (%.1f waits) | read-backs waited for: %.1f lock, %.1f NOOVERWRITE",
         g_perf_capture_ns.exchange(0) / n / 1e6, g_perf_wait_ns.exchange(0) / n / 1e6, g_perf_waits.exchange(0) / n,
         g_perf_sync_block.exchange(0) / n, g_perf_sync_late.exchange(0) / n);
    LOGI("perf: per frame %.0f draws (%.0f raw) %.0fk verts %.1f MB upload %.1f overflow | %.0f checks %.2f MB hashed | %.1f tex uploads "
         "%.2f MB, %.1f new tex, %.1f new targets, %.1f target uploads, %.1f shrinks, %.1f read-backs %.1f late | %llu shaders %llu psos this second",
         p.draws / n, p.raw / n, p.verts / n / 1000.0, p.upload / n / 1048576.0, p.overflow / n, p.sigs / n,
         p.sig_bytes / n / 1048576.0, p.texups / n, p.texup_bytes / n / 1048576.0, p.newtex / n, p.targets / n, p.rtups / n,
         p.shrinks / n, p.syncs / n, p.lates / n, (unsigned long long)p.shaders, (unsigned long long)p.psos);
    g_perf = Perf{};
    perf_start = PerfClock::now();
    perf_frames = 0;
}

// ImGui reports every Vulkan failure through this and no further: without one, a pipeline it could not make stays
// silent until it is bound and the driver faults on it
void LogUiResult(VkResult err) {
    if (err != VK_SUCCESS) LOGE("vulkan: the panel reported (%d)", int(err));
}

}  // namespace

bool SkipPart(const char* part) {
    static std::mutex lock;
    static std::map<std::string, bool> flags;
    std::lock_guard guard(lock);
    auto it = flags.find(part);
    if (it != flags.end()) return it->second;
    std::error_code ec;
    bool on = std::filesystem::exists(app::ExeDir() / part, ec);
    flags[part] = on;
    if (on) LOGI("vulkan: dropping %s", part);
    return on;
}

// ---------------------------------------------------------------- the settings panel

// the panel's pipelines are made for the format and the render style of the target it is drawn into
void VulkanRenderer::InitUi(VkFormat format) {
    if (headless_ || !ImGui::GetCurrentContext()) return;
    if (ui_ready_) {
        if (ui_format_ == format) return;
        ShutdownUi();  // a different target: the panel's pipelines cannot draw this one
    }
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 16;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(ctx_.dev, &pi, nullptr, &ui_pool_) != VK_SUCCESS) return;
    ui_format_ = format;  // ImGui keeps the pointer to this in PipelineRenderingCreateInfo, it outlives the init
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = ctx_.instance;
    ii.PhysicalDevice = ctx_.phys;
    ii.Device = ctx_.dev;
    ii.QueueFamily = ctx_.family;
    ii.Queue = ctx_.queue;
    ii.DescriptorPool = ui_pool_;
    ii.MinImageCount = 2;
    ii.ImageCount = kFrames;
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineCache = cache_;
    // below dynamic rendering the panel's pipeline is built for the render pass RenderUi draws inside: the same one
    // BeginRendering opens for this target, where VkPipelineRenderingCreateInfoKHR is not understood yet
    ii.UseDynamicRendering = ctx_.caps.dynamic_rendering;
    if (ctx_.caps.dynamic_rendering) {
        ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ui_format_;
    } else {
        ii.RenderPass = RenderPassFor(format, false);
        ii.Subpass = 0;
    }
    ii.CheckVkResultFn = &LogUiResult;
    std::lock_guard lock(app::UiLock());
    ui_ready_ = ImGui_ImplVulkan_Init(&ii);
    if (!ui_ready_) LOGE("vulkan: the settings panel could not start");
}

void VulkanRenderer::ShutdownUi() {
    if (ui_ready_) {
        std::lock_guard lock(app::UiLock());
        ImGui_ImplVulkan_Shutdown();
        ui_ready_ = false;
    }
    if (ui_pool_) vkDestroyDescriptorPool(ctx_.dev, ui_pool_, nullptr);
    ui_pool_ = VK_NULL_HANDLE;
}

// the panel into the target that is already open
void VulkanRenderer::RenderUi(Image& into) {
    host::Pause().FrameReady();
    InitUi(into.desc.format);
    if (presents_ < 10)
        LOGI("vulkan: ui ready=%d draw=%p drop=%d", int(ui_ready_), (void*)ImGui::GetDrawData(), int(SkipPart("noui")));
    if (!ui_ready_ || SkipPart("noui")) return;
    BeginRendering({&into, nullptr}, into.desc.w, into.desc.h);
    std::lock_guard lock(app::UiLock());
    app::UiNewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (app::kGame.ui) app::kGame.ui(fps_);
    else app::DrawGraphicsMenu(fps_);
    ImGui::Render();
    ImDrawData* draw = ImGui::GetDrawData();
    // The game blit and host panel must use the same display pre-rotation.
    // Transform positions and clipping together; gamepad navigation remains in
    // ImGui's logical, upright coordinate system.
    const auto transform = swap_ ? swap_->Transform() : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (draw && transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        const ImVec2 origin = draw->DisplayPos, size = draw->DisplaySize;
        auto rotate = [&](ImVec2 p) {
            float x = (p.x - origin.x) / size.x, y = (p.y - origin.y) / size.y;
            if (transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) { float old = x; x = 1 - y; y = old; }
            else if (transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) { x = 1 - x; y = 1 - y; }
            else if (transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) { float old = x; x = y; y = 1 - old; }
            return ImVec2(origin.x + x * size.x, origin.y + y * size.y);
        };
        if (size.x > 0 && size.y > 0) {
            for (ImDrawList* list : draw->CmdLists) {
                for (ImDrawVert& vertex : list->VtxBuffer) vertex.pos = rotate(vertex.pos);
                for (ImDrawCmd& cmd : list->CmdBuffer) {
                    ImVec2 a = rotate(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y));
                    ImVec2 b = rotate(ImVec2(cmd.ClipRect.z, cmd.ClipRect.w));
                    cmd.ClipRect = ImVec4(std::min(a.x,b.x), std::min(a.y,b.y), std::max(a.x,b.x), std::max(a.y,b.y));
                }
            }
        }
    }
    ImGui_ImplVulkan_RenderDrawData(draw, Cb());
}

// ---------------------------------------------------------------- output

// the window's size and display: the swap chain made again when they, HDR or vsync changed
void VulkanRenderer::ConfigureSwap() {
    uint32_t w = 0, h = 0;
    app::WindowPixels(w, h);
    EndRendering();
    swap_->Configure(std::max(w, 1u), std::max(h, 1u), false, app::Get().vsync);
}

// ramp_ holds the red, green and blue ramps, 256 entries each
void VulkanRenderer::UploadGammaRamp() {
    ramp_dirty_ = false;
    Alloc a = Upload(256 * 4, 16);
    bool identity = true;
    for (int i = 0; i < 256; i++) {
        uint8_t* p = a.cpu + i * 4;
        p[0] = ramp_[512 + i];
        p[1] = ramp_[256 + i];
        p[2] = ramp_[i];
        p[3] = 255;
        identity &= ramp_[i] == i && ramp_[256 + i] == i && ramp_[512 + i] == i;
    }
    gamma_ = !identity;
    Barrier(*lut_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.bufferOffset = a.off;
    c.imageSubresource = Layers(*lut_);
    c.imageExtent = {256, 1, 1};
    vkCmdCopyBufferToImage(Cb(), a.buf, lut_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Barrier(*lut_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// what the frame shows: the target made readable, SMAA's blend of it when that is on





// the frame letterboxed into the open target: 16:9 when the console reports a widescreen display, else 4:3
void VulkanRenderer::BlitFrame(Target& t, Image& bb) {
    if (SkipPart("noblit")) return;
    float aspect = app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = float(bb.desc.w);
    float h = float(bb.desc.h);
    float vw = std::min(w, h * aspect);
    float vh = vw / aspect;
    SetViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    SetScissor(0, 0, int32_t(w), int32_t(h));
    Barrier(*t.res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginRendering({&bb, nullptr}, bb.desc.w, bb.desc.h);
    float uv[4] = {0, 0, 1, 1};
    // Match the pre-rotation promised to the presentation engine.
    const auto transform = swap_->Transform();
    uint32_t rotation = transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1 :
                        transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2 :
                        transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3 : 0;
    Blit(*t.res, uv, {.gamma = gamma_, .rtv = bb.desc.format, .rotation = rotation});
}




void VulkanRenderer::DrawFrame(Target* t, Image& bb) {
    if (SkipPart("nodf")) return;
    if (!SkipPart("norep")) {
        if (t) Barrier(*t->res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        BeginRendering({&bb, nullptr}, bb.desc.w, bb.desc.h);
        VkClearAttachment ca{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
        ca.clearValue.color.float32[3] = 1;
        VkClearRect cr{{{0, 0}, {bb.desc.w, bb.desc.h}}, 0, 1};
        vkCmdClearAttachments(Cb(), 1, &ca, 1, &cr);
        bound_rt_ = bound_ds_ = nullptr;
        if (t) BlitFrame(*t, bb);
        RenderUi(bb);
    }
    Barrier(bb, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

// ---------------------------------------------------------------- the frame turns over

void VulkanRenderer::ReleaseOldTextures() {
    constexpr uint64_t kSweepEvery = 60;
    constexpr size_t kTexturesKept = 2048;
    constexpr uint64_t kIdleFrames = 600;
    // textures unused for ten seconds go, levels stream theirs in and out
    if (presents_ % kSweepEvery != 0 || textures_.size() <= kTexturesKept) return;
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.frame + kIdleFrames < presents_) {
            Release(std::move(it->second.res));
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

void VulkanRenderer::ResetFrame() {
    Frame& f = CurrentFrame();
    if (f.fence && ctx_.Completed() < f.fence) {
        auto perf_wait = PerfClock::now();
        ctx_.Wait(f.fence);
        g_perf.wait += PerfMs(perf_wait);
    }
    vkResetCommandPool(ctx_.dev, f.pool, 0);
    DropSlotFramebuffers();
    f.images.clear();
    f.buffers.clear();
    for (VkSampler s : f.samplers) vkDestroySampler(ctx_.dev, s, nullptr);
    f.samplers.clear();
    f.used = 0;
    Begin();
}

void VulkanRenderer::CountFps() {
    fps_n_++;
    auto now = PerfClock::now();
    double el = std::chrono::duration<double>(now - fps_t_).count();
    if (el >= 0.5) {
        fps_ = fps_n_ / el;
        fps_n_ = 0;
        fps_t_ = now;
    }
}

void VulkanRenderer::ApplySettings() {
    if (headless_) return;
    float want = app::RenderScale(guest_h_);
    if (std::fabs(want - scale_) <= 1e-4f) return;
    Flush();
    for (auto& [key, tg] : targets_) {
        Release(std::move(tg.res));
        Release(std::move(tg.fb));
    }
    targets_.clear();
    scale_ = want;
    bound_rt_ = bound_ds_ = nullptr;
}

void VulkanRenderer::Present(const Surface& front) {
    PerfScope perf_present{g_perf.present};
    if (cap_) {
        FinishCapture(front);
    } else if (g_capture.exchange(false)) {
        if (diag::CaptureDir().empty()) LOG_FIRST(1, "capture: set XBE_CAPTURE_DIR");
        else StartCapture();
    }
    if (swap_) ConfigureSwap();
    Target* t = EnsureTarget(front, false);
    if (t && LatchCpuWrite(*t)) UploadTarget(*t, front);
    if (ramp_dirty_) UploadGammaRamp();
    Barrier(*lut_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    uint32_t index = 0;
    VkSemaphore acquired = VK_NULL_HANDLE, done = VK_NULL_HANDLE;
    Image* bb = nullptr;
    if (swap_) {
        auto perf_swap = PerfClock::now();
        EndRendering();
        if (swap_->Acquire(index, acquired)) {
            bb = &swap_->At(index);
            done = swap_->DoneSemaphore(index);
        }
        g_perf.swap += PerfMs(perf_swap);
    }
    if (presents_ < 10)
        LOGI("vulkan: present %llu bb=%p target=%p acquired=%p", (unsigned long long)presents_, (void*)bb, (void*)t,
             (void*)acquired);
    if (bb) DrawFrame(t, *bb);
    Submit(false, acquired, done);
    DeliverReports();
    if (bb) swap_->Present(index, done);

    if (!paused_present_) {
        presents_++;
        presents_seen_.store(presents_);
    }
    frame_++;
    ReleaseOldTextures();
    ResetFrame();
    CountFps();
    if (diag::Perf()) LogPerf();
    if (!host::Pause().Paused()) ApplySettings();
}

}  // namespace gpu::vk
