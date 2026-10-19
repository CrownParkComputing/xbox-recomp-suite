// Read-backs: GPU results into guest memory when the title locks a surface, at once (Sync) or from the newest
// finished copy (the late ring); and the F3 frame capture, which reads targets back the same way
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "gpu/vulkan.h"

namespace gpu::vk {

using namespace renderer;


// the target, shrunk to a guest sized BGRA image when it is not one already
VulkanRenderer::CopySource VulkanRenderer::PrepareCopySource(Target& t, const ShrinkOptions& o) {
    CopySource out{t.res.get(), nullptr};
    if (!out.res) return out;
    if (t.w != t.gw || t.h != t.gh || t.fmt != kBgra) {
        out.down = Shrink(*out.res, t.gw, t.gh, o);
        out.res = out.down.get();
    }
    return out;
}

// filters src into a w x h BGRA image (o.into when given), left as a copy source
ImageRef VulkanRenderer::Shrink(Image& src, uint32_t w, uint32_t h, const ShrinkOptions& o) {
    g_perf.shrinks++;
    ImageRef down = o.into;
    if (!down || down->desc.w != w || down->desc.h != h) down = ctx_.CreateImage({.format = kBgra, .w = w, .h = h, .target = true});
    if (!down) return nullptr;
    Barrier(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginRendering({down.get(), nullptr}, w, h);
    SetFullViewport(w, h);
    float uv[4] = {0, 0, 1, 1};
    Blit(src, uv, {.alpha = true, .taps = o.taps, .mul = o.mul});
    bound_rt_ = bound_ds_ = nullptr;
    Barrier(*down, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return down;
}

// a BGRA image to the CPU, rows packed; waits for the GPU
bool VulkanRenderer::ReadBack(Image& res, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    BufferRef rb = ctx_.CreateBuffer(uint64_t(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Readback);
    if (!rb) return false;
    Barrier(res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = Layers(res);
    c.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(Cb(), res.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb->buffer, 1, &c);
    Flush();
    rb->Invalidate();
    out.assign(rb->map, rb->map + size_t(w) * h * 4);
    return true;
}

// the CPU reads a surface the GPU drew: bring the pixels back
void VulkanRenderer::Sync(uint32_t data, uint32_t) {
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || !it->second.drawn || TargetStale(data)) return;
    PerfScope perf_sync{g_perf.sync};
    g_perf.syncs++;
    Target& t = it->second;
    if (diag::Perf()) LOG_FIRST(16, "perf: read-back of %08X (%ux%u guest, %ux%u host)", data, t.gw, t.gh, t.w, t.h);
    // src.down, when there is one, stays alive until ReadBack is done
    CopySource src = PrepareCopySource(t, {});
    if (!src.res) return;
    std::vector<uint8_t> px;
    if (!ReadBack(*src.res, t.gw, t.gh, px)) return;
    // colour targets are 32 bit linear in guest memory, the pitch rounds to 64 bytes
    uint32_t pitch = GuestPitch(t.gw);
    uint8_t* guest = static_cast<uint8_t*>(kernel::HostPtr(data));
    for (uint32_t y = 0; y < t.gh; y++) std::memcpy(guest + size_t(y) * pitch, &px[size_t(y) * t.gw * 4], t.gw * 4);
    t.read = std::move(px);
    t.drawn = false;
    t.sig = MemSig(t.data, t.bytes);
}

// A NOOVERWRITE lock: the console reads whatever the GPU has finished, so a surface locked every frame gets the newest
// finished copy (a frame or two old) and the CPU never waits on the GPU; one locked now and then still waits in Sync
void VulkanRenderer::SyncLate(uint32_t data) {
    if (!DeliverLate(data)) Sync(data, 0);
    RecordLate(data);
}

bool VulkanRenderer::DeliverLate(uint32_t data) {
    std::lock_guard lock(late_lock_);
    auto it = late_.find(data);
    if (it == late_.end()) return false;
    Late& l = it->second;
    uint64_t done = ctx_.Completed();
    uint64_t now = presents_seen_.load();
    int best = -1;
    for (int i = 0; i < kFrames; i++) {
        bool finished = l.fence[i] && l.fence[i] <= done;
        bool recent = now - l.frame[i] <= uint64_t(kFrames) + 1;
        bool newest = best < 0 || l.fence[i] > l.fence[best];
        if (finished && recent && newest) best = i;
    }
    if (best < 0) return false;
    if (l.fence[best] > l.delivered) {
        Buffer& rb = *l.rb[best];
        rb.Invalidate();
        uint32_t pitch = GuestPitch(l.gw);
        uint8_t* dst = static_cast<uint8_t*>(kernel::HostPtr(data));
        for (uint32_t y = 0; y < l.gh; y++) std::memcpy(dst + size_t(y) * pitch, rb.map + size_t(y) * l.gw * 4, l.gw * 4);
        l.delivered = l.fence[best];
    }
    return true;
}

void VulkanRenderer::ResetLate(Late& l, const Target& t) {
    for (auto& r : l.rb)
        if (r) CurrentFrame().buffers.push_back(r);
    Release(std::move(l.down));
    l = Late{};
    l.gw = t.gw;
    l.gh = t.gh;
}

Image* VulkanRenderer::PrepareLateSource(Target& t, Late& l) {
    ShrinkOptions reuse{.into = l.down};
    CopySource src = PrepareCopySource(t, reuse);
    if (src.down) {
        if (src.down != l.down) Release(std::move(l.down));
        l.down = src.down;
    }
    return src.res;
}

void VulkanRenderer::RecordLateCopy(Late& l, uint32_t slot, Image& src) {
    if (!l.rb[slot]) {
        l.rb[slot] = ctx_.CreateBuffer(uint64_t(l.gw) * l.gh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Readback);
        if (!l.rb[slot]) return;
    }
    Barrier(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = Layers(src);
    c.imageExtent = {l.gw, l.gh, 1};
    vkCmdCopyImageToBuffer(Cb(), src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l.rb[slot]->buffer, 1, &c);
    l.fence[slot] = fence_value_ + 1;  // the next Submit carries it
    l.frame[slot] = presents_;
    l.next = (slot + 1) % kFrames;
}

void VulkanRenderer::RecordLate(uint32_t data) {
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || TargetStale(data)) return;
    Target& t = it->second;
    PerfScope perf_sync{g_perf.sync};
    g_perf.lates++;
    // what DeliverLate or Sync left in guest memory is no CPU write to upload back
    t.sig = MemSig(t.data, t.bytes);
    t.read.clear();
    std::lock_guard lock(late_lock_);
    Late& l = late_[data];
    if (l.gw != t.gw || l.gh != t.gh) ResetLate(l, t);
    // a copy for a later lock, unless the GPU is a whole ring behind
    uint32_t slot = l.next;
    if (l.fence[slot] > ctx_.Completed()) return;
    Image* src = PrepareLateSource(t, l);
    if (!src) return;
    RecordLateCopy(l, slot, *src);
}

// ---------------------------------------------------------------- F3 capture

namespace {

constexpr int kBmpHeaderBytes = 54;

void WriteBmp(const std::filesystem::path& path, const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t hd[kBmpHeaderBytes] = {'B', 'M'};  // BITMAPFILEHEADER and BITMAPINFOHEADER, top down 32 bit
    auto put = [&](int o, uint32_t v) { std::memcpy(hd + o, &v, 4); };
    put(2, kBmpHeaderBytes + uint32_t(px.size()));
    put(10, kBmpHeaderBytes);
    put(14, 40);
    put(18, w);
    put(22, uint32_t(-int32_t(h)));
    hd[26] = 1;
    hd[28] = 32;
    put(34, uint32_t(px.size()));
    fwrite(hd, 1, kBmpHeaderBytes, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
}

}  // namespace

void VulkanRenderer::CapSave(const CapImage& img, const char* name) {
    ImageRef down = Shrink(*img.res, img.w, img.h, {.mul = img.mul});
    std::vector<uint8_t> px;
    if (!down || !ReadBack(*down, img.w, img.h, px)) return;
    WriteBmp(cap_dir_ / (std::string(name) + ".bmp"), px, img.w, img.h);
}

void VulkanRenderer::CapSave(Target& t, const char* name) {
    CapSave({.res = t.res.get(), .w = t.gw, .h = t.gh}, name);
}

// the frame moves to another target: save the one it leaves
void VulkanRenderer::CapStep(const Surface& rt) {
    uint32_t d = rt.header ? mem::VirtualFromPhysical(rt.data) : 0;
    if (d == cap_rt_) return;
    auto it = targets_.find(TargetMapKey(cap_rt_, false));
    if (cap_rt_ && it != targets_.end()) {
        char name[32];
        snprintf(name, sizeof(name), "%04u_%08X", cap_i_, cap_rt_);
        CapSave(it->second, name);
    }
    cap_rt_ = d;
}

void VulkanRenderer::CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PsoKey& k, const DrawConstants& c,
                                const DrawTextures& stages) {
    fprintf(cap_,
            "%04u draw rt %08X ds %08X prim %u count %u vs %08X ps %08X blend %u %X %X zen %u %X zwrite %u zbias %d %.2f cull %u cw %X c0 %.3f %.3f %.3f %.3f",
            cap_i_++, s.rt.data, bound_ds_ ? s.zs.data : 0, prim, count, s.vs_handle, s.ps ? s.ps->handle : 0, k.blend, k.src,
            k.dst, k.zen, k.zfunc, k.zwrite, k.zbias, k.zslope, k.cull, k.wmask, c.pc[0][0], c.pc[0][1], c.pc[0][2], c.pc[0][3]);
    for (int i = 0; i < 4; i++) {
        TextureDesc d;
        if (!s.textures[size_t(i)] || !Describe(s.textures[size_t(i)], d)) continue;
        fprintf(cap_, " | t%d %08X fmt %X %ux%u%s%s", i, d.data, d.format, d.width, d.height, stages.rt[i] ? " target" : "",
                stages.res[i] ? "" : " unbound");
    }
    fprintf(cap_, " | viewport %u %u %u %u scissor_count %u rect %d %d %d %d offset %.3f %.3f\n",
            s.viewport.x, s.viewport.y, s.viewport.w, s.viewport.h, s.scissor_count,
            s.scissors[0], s.scissors[1], s.scissors[2], s.scissors[3], s.ss_offset_x, s.ss_offset_y);
    if (s.vs) {
        for (unsigned i = 0; i < s.vsc.size(); ++i)
            fprintf(cap_, "     c%u %.6g %.6g %.6g %.6g\n", i,
                    s.vsc[i][0], s.vsc[i][1], s.vsc[i][2], s.vsc[i][3]);
    }
}

void VulkanRenderer::CaptureShader(const char* kind, uint64_t key, const std::string& src) {
    if (!cap_sh_.insert(key).second) return;
    char name[48];
    snprintf(name, sizeof(name), "%s_%016llX.hlsl", kind, (unsigned long long)key);
    if (FILE* f = fopen((cap_dir_ / name).c_str(), "w")) {
        fwrite(src.data(), 1, src.size(), f);
        fclose(f);
    }
    fprintf(cap_, "     %s\n", name);
}

void VulkanRenderer::FinishCapture(const Surface& front) {
    CapStep(Surface{});
    if (Target* f = FindTarget(front, false)) CapSave(*f, "final");
    fclose(cap_);
    cap_ = nullptr;
    LOGI("vulkan: frame captured to %s", cap_dir_.string().c_str());
}

void VulkanRenderer::StartCapture() {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%H%M%S", std::localtime(&now));
    std::error_code ec;
    cap_dir_ = std::filesystem::weakly_canonical(app::ExeDir() / diag::CaptureDir() / stamp, ec);
    std::filesystem::create_directories(cap_dir_, ec);
    cap_ = fopen((cap_dir_ / "frame.txt").c_str(), "w");
    cap_i_ = cap_rt_ = 0;
    cap_sh_.clear();
}

}  // namespace gpu::vk
