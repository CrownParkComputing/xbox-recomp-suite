#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include "vulkan_probe.h"
#include "app/app.h"
#include "core/log.h"
#include "harness.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"

namespace app {
std::vector<const char*> VulkanInstanceExtensions() { return {}; }
bool CreateVulkanSurface(void*, uint64_t*) { return false; }
void WindowPixels(uint32_t& w, uint32_t& h) { w = 640; h = 480; }
void UiNewFrame() {}
bool GraphicsMenuOpen() { return false; }
void DrawGraphicsMenu(double) {}
bool Unfocused() { return false; }
void Quit() { std::exit(0); }
void ShowError(const wchar_t*) {}
bool AskYesNo(const wchar_t*) { return false; }
}

namespace {
class CaptureBackend final : public gpu::Backend {
    std::unique_ptr<gpu::vk::VulkanRenderer> inner_ = gpu::vk::CreateHeadlessRenderer();
    unsigned snaps_ = 0;
    uint64_t draws_ = 0;
    bool force_visible_ = false;
public:
    bool Init(uint32_t w, uint32_t h) override { return inner_->Init(w, h); }
    void Clear(const gpu::State& s, uint32_t n, const uint8_t* r, uint32_t f, uint32_t c, float z, uint32_t st) override { inner_->Clear(s, n, r, f, c, z, st); }
    void Draw(const gpu::State& s, uint32_t p, uint32_t b, uint32_t n, const uint8_t* i) override {
        ++draws_;
        const auto before = inner_->cap_i_;
        inner_->Draw(s, p, b, n, i);
        if (inner_->cap_ && inner_->cap_i_ == before) {
            std::fprintf(inner_->cap_, "skipped draw vs %08X ps %08X count %u rt %08X base %u\n",
                s.vs_handle, s.ps ? s.ps->handle : 0, n, s.rt.data, s.base_vertex);
            if (s.vs) for (unsigned r = 0; r < 16; ++r) {
                const auto& in = s.vs->inputs[r];
                if (in.stream < 0 || !in.type || in.type == 2) continue;
                const auto& stream = s.streams[in.stream];
                std::fprintf(inner_->cap_, " skipped input v%u stream %d type %X vb %08X data %08X stride %u offset %u\n",
                    r, in.stream, in.type, stream.vb, stream.vb ? kernel::R32(stream.vb + 4) : 0, stream.stride, in.offset);
            }
        }
    }
    void Copy(const gpu::Surface& s, const gpu::Surface& d, uint32_t n, uint32_t r, uint32_t p) override { inner_->Copy(s, d, n, r, p); }
    void Present(const gpu::Surface& s) override {
        if (std::filesystem::exists("capture")) {
            std::filesystem::remove("capture");
            setenv("XBE_CAPTURE_DIR", "captures", 0);
            gpu::g_capture = true;
        }
        if (std::filesystem::exists("raw_off")) {
            std::filesystem::remove("raw_off"); gpu::SetRawVertices(false);
        }
        if (std::filesystem::exists("raw_on")) {
            std::filesystem::remove("raw_on"); gpu::SetRawVertices(true);
        }
        if (std::filesystem::exists("force_visible")) {
            std::filesystem::remove("force_visible"); force_visible_ = true;
        }
        inner_->Present(s);
        const auto frame = ++harness::g_frames;
        if (frame % 60 == 0) {
            LOGI("gpu probe: frame %llu draws %llu front %08X", (unsigned long long)frame, (unsigned long long)draws_, s.data);
            logx::Flush();
        }
        if (!std::filesystem::exists("snap")) return;
        std::filesystem::remove("snap");
        auto* target = inner_->EnsureTarget(s, false);
        auto output = inner_->ctx_.CreateImage({.format = VK_FORMAT_B8G8R8A8_UNORM, .w = 640, .h = 480, .target = true});
        if (!target || !output) return;
        inner_->Barrier(*target->res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        inner_->BeginRendering({output.get(), nullptr}, 640, 480);
        inner_->SetFullViewport(640, 480);
        float uv[4] = {0, 0, 1, 1};
        inner_->Blit(*target->res, uv, {.gamma = inner_->gamma_, .rtv = output->desc.format});
        inner_->bound_rt_ = inner_->bound_ds_ = nullptr;
        std::vector<uint8_t> pixels;
        if (!inner_->ReadBack(*output, 640, 480, pixels)) return;
        char path[80];
        std::snprintf(path, sizeof(path), "gpu_output_%u.tga", ++snaps_);
        const unsigned char header[18] = {0,0,2,0,0,0,0,0,0,0,0,0,128,2,224,1,32,40};
        if (auto* file = std::fopen(path, "wb")) {
            std::fwrite(header, 1, sizeof(header), file);
            std::fwrite(pixels.data(), 1, pixels.size(), file);
            std::fclose(file);
        }
        LOGI("gpu probe: snapshot %s", path);
    }
    void Sync(uint32_t d, uint32_t n) override { inner_->Sync(d, n); }
    void SyncLate(uint32_t d) override { inner_->SyncLate(d); }
    bool DeliverLate(uint32_t d) override { return inner_->DeliverLate(d); }
    void RecordLate(uint32_t d) override { inner_->RecordLate(d); }
    void Flush() override { inner_->Flush(); }
    void SetGamma(const uint8_t* r) override { inner_->SetGamma(r); }
    void ClearReport() override { inner_->ClearReport(); }
    void GetReport(uint32_t a) override {
        if (force_visible_) {
            kernel::W32(a, uint32_t(harness::g_frames.load())); kernel::W32(a + 4, 0);
            kernel::W32(a + 8, 640 * 480); kernel::W32(a + 12, 0);
        } else inner_->GetReport(a);
    }
    uint32_t Fence() override { return inner_->Fence(); }
    bool FencePending(uint32_t a) override { return inner_->FencePending(a); }
    void WaitFence(uint32_t a) override { inner_->WaitFence(a); }
    bool Busy(uint32_t a) override { return inner_->Busy(a); }
    void WaitResource(uint32_t a) override { inner_->WaitResource(a); }
};
}
std::unique_ptr<gpu::Backend> WrappedCreateBackend() asm("__wrap__ZN3gpu13CreateBackendEv");
std::unique_ptr<gpu::Backend> WrappedCreateBackend() { return std::make_unique<CaptureBackend>(); }
