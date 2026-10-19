// texture registers pushed by a title, as the headers the renderer reads: the D3D texture and palette a stage is bound to
#include "check.h"
#include "gpu/nv2a_texture.h"

using namespace gpu::nv2a;

TEST("nv2a texture: methods 0x1B00 to 0x1BFF name a stage and a register, others none") {
    uint32_t stage = 9, reg = 9;
    CHECK(TextureMethod(0x1B00, stage, reg));
    CHECK_EQ(stage, 0u);
    CHECK_EQ(reg, uint32_t(kOffset));
    CHECK(TextureMethod(0x1BE0, stage, reg));  // stage 3 palette
    CHECK_EQ(stage, 3u);
    CHECK_EQ(reg, uint32_t(kPalette));
    CHECK(!TextureMethod(0x1AFC, stage, reg));
    CHECK(!TextureMethod(0x1C00, stage, reg));
    CHECK(!TextureMethod(0x17FC, stage, reg));
}

TEST("nv2a texture: a linear render target bound as a texture keeps its size and pitch through the registers") {
    // Burnout 3's 640x480 scene target: X8R8G8B8 linear (0x1E), 2560 byte pitch
    TextureHeader h{0x00050001, 0x01D24000, 0, 0x00011E29, 639 | 479 << 12 | (2560 / 64 - 1) << 24};
    TextureRegs r = FromTexture(h);
    CHECK_EQ(r.offset, 0x01D24000u);
    CHECK_EQ(r.rect, 640u << 16 | 480u);
    CHECK_EQ(r.control1 >> 16, 2560u);
    CHECK(r.control0 & kControl0Enable);
    TextureHeader back = ToTexture(r);
    CHECK_EQ(back.data, h.data);
    CHECK_EQ(back.format, h.format);
    CHECK_EQ(back.size, h.size);
}

TEST("nv2a texture: registers pushed one by one make the texture the title described, offset last or first") {
    TextureRegs r;
    CHECK(Write(r, kFormat, 0x00011E29));
    CHECK(Write(r, kControl1, 1280u << 16));
    CHECK(Write(r, kImageRect, 320u << 16 | 240u));
    CHECK(Write(r, kOffset, 0x01CFC000));
    CHECK(Write(r, kControl0, kControl0Enable));
    TextureHeader t = ToTexture(r);
    CHECK_EQ(t.data, 0x01CFC000u);
    CHECK_EQ(t.size & 0xFFF, 319u);
    CHECK_EQ((t.size >> 12) & 0xFFF, 239u);
    CHECK_EQ(((t.size >> 24) + 1) * 64, 1280u);
    CHECK_EQ(t.common & 0x70000, 0x40000u);  // a texture
}

TEST("nv2a texture: a swizzled texture has no size word, its log2 sizes are in the format") {
    // A8R8G8B8 swizzled (0x06), 256x256, one level
    uint32_t format = 0x00010629 | 8u << 20 | 8u << 24;
    TextureRegs r;
    Write(r, kFormat, format);
    Write(r, kOffset, 0x00400000);
    Write(r, kImageRect, 640u << 16 | 480u);  // stale from a linear texture before: ignored
    Write(r, kControl1, 2560u << 16);
    TextureHeader t = ToTexture(r);
    CHECK_EQ(t.size, 0u);
    CHECK_EQ(t.format, format);
}

TEST("nv2a texture: address modes, filters and border colours are not header fields") {
    TextureRegs r;
    CHECK(!Write(r, 0x08, 0x00010101));  // address
    CHECK(!Write(r, 0x14, 0x02022000));  // filter
    CHECK(!Write(r, 0x24, 0xFF000000));  // border colour
}

TEST("nv2a texture: a palette's address and colour count survive the register both ways") {
    for (uint32_t code = 0; code < 4; code++) {  // 256, 128, 64, 32 colours
        PaletteHeader h{0x00030001 | code << 30, 0x03D1C240, 0};
        uint32_t reg = FromPalette(h);
        CHECK_EQ(reg & ~0x3Fu, 0x03D1C240u);
        PaletteHeader back = ToPalette(reg);
        CHECK_EQ(back.data, h.data);
        CHECK_EQ(back.common >> 30, code);
        CHECK_EQ(back.common & 0x70000, 0x30000u);  // a palette
    }
    // the dma bits the title sets are not part of the address
    CHECK_EQ(ToPalette(0x03D1C240 | 1).data, 0x03D1C240u);
}
