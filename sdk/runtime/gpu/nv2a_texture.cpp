#include "gpu/nv2a_texture.h"

#include "gpu/texture.h"

namespace gpu::nv2a {

namespace {

constexpr uint32_t kTextureCommon = 0x00040001;  // texture, one reference
constexpr uint32_t kPaletteCommon = 0x00030001;  // palette, one reference
constexpr uint32_t kPaletteSizeShift = 30;

bool Linear(uint32_t format) {
    const FormatInfo& f = Format((format >> 8) & 0xFF);
    return !f.swizzled && !f.compressed;
}

}  // namespace

bool TextureMethod(uint32_t method, uint32_t& stage, uint32_t& reg) {
    if (method < kTextureMethods || method >= kTextureMethods + kTextureStride * kTextureStages) return false;
    stage = (method - kTextureMethods) / kTextureStride;
    reg = (method - kTextureMethods) % kTextureStride;
    return true;
}

bool Write(TextureRegs& r, uint32_t reg, uint32_t value) {
    switch (reg) {
        case kOffset: r.offset = value; return true;
        case kFormat: r.format = value; return true;
        case kControl0: r.control0 = value; return true;
        case kControl1: r.control1 = value; return true;
        case kImageRect: r.rect = value; return true;
        case kPalette: r.palette = value; return true;
        default: return false;
    }
}

TextureRegs FromTexture(const TextureHeader& h) {
    TextureRegs r;
    r.offset = h.data;
    r.format = h.format;
    r.control0 = kControl0Enable;
    if (h.size) {  // linear: its size, the pitch in 64 byte units
        uint32_t w = (h.size & 0xFFF) + 1, height = ((h.size >> 12) & 0xFFF) + 1, pitch = ((h.size >> 24) + 1) * 64;
        r.rect = w << 16 | height;
        r.control1 = pitch << 16;
    }
    return r;
}

TextureHeader ToTexture(const TextureRegs& r) {
    TextureHeader h{kTextureCommon, r.offset, 0, r.format, 0};
    uint32_t w = r.rect >> 16, height = r.rect & 0xFFFF, pitch = r.control1 >> 16;
    if (Linear(r.format) && w && height && pitch >= 64)
        h.size = (w - 1) | (height - 1) << 12 | (pitch / 64 - 1) << 24;
    return h;
}

uint32_t FromPalette(const PaletteHeader& h) { return (h.data & ~0x3Fu) | (h.common >> kPaletteSizeShift) << 2; }

PaletteHeader ToPalette(uint32_t palette) {
    return {kPaletteCommon | ((palette >> 2) & 3) << kPaletteSizeShift, palette & ~0x3Fu, 0};
}

}  // namespace gpu::nv2a
