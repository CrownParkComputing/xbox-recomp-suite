#pragma once
// The NV2A's texture registers of one stage, as a title or the library's lazy state commit writes them into the push
// buffer (methods 0x1B00 + 0x40 * stage), and the D3D headers the renderer reads a texture and a palette from.
// Pure: no guest memory, so both directions can be tested.
#include <cstdint>

namespace gpu::nv2a {

constexpr uint32_t kTextureMethods = 0x1B00, kTextureStride = 0x40, kTextureStages = 4;
// registers of a stage, by method offset
enum TextureReg : uint32_t {
    kOffset = 0x00,    // physical address of the texels, as a D3D header's Data
    kFormat = 0x04,    // the D3D header's Format: dma, cube, dimensions, colour format, levels, log2 sizes
    kControl0 = 0x0C,  // bit 30 enables the stage
    kControl1 = 0x10,  // pitch of a linear texture in bits 16 to 31
    kImageRect = 0x1C, // width << 16 | height of a linear texture
    kPalette = 0x20,   // physical address | length code << 2 (256, 128, 64 or 32 colours) | dma
};
constexpr uint32_t kControl0Enable = 1u << 30;

struct TextureRegs {
    uint32_t offset = 0, format = 0, control0 = 0, control1 = 0, rect = 0, palette = 0;
};

// the stage and register a method writes, false for any other method
bool TextureMethod(uint32_t method, uint32_t& stage, uint32_t& reg);
// a register write; false for a register the headers do not carry (address modes, filters, border colour)
bool Write(TextureRegs& r, uint32_t reg, uint32_t value);

// D3DPixelContainer words: Common, Data, Lock, Format, Size
struct TextureHeader {
    uint32_t common, data, lock, format, size;
};
// D3DPalette words: Common (size code in the top two bits), Data, Lock
struct PaletteHeader {
    uint32_t common, data, lock;
};

// the registers a D3D texture header sets, the stage enabled
TextureRegs FromTexture(const TextureHeader& h);
TextureHeader ToTexture(const TextureRegs& r);
// the palette register a D3D palette header sets, and back
uint32_t FromPalette(const PaletteHeader& h);
PaletteHeader ToPalette(uint32_t palette);

}  // namespace gpu::nv2a
