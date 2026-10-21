#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"
#include "image_symbols.h"
#include "gpu/texture.h"

void VCop3_CommitMovieFrame(X86Context&, uint8_t*) {
    // The original Sofdec copy at 0x82843 has finished writing the locked
    // texture. Publish that completed CPU write before the previews sample it.
    const uint32_t texture = kernel::R32(SYM(VC3_MovieState) + 0x5C);
    if (texture) gpu::MarkDirty(texture);
}
