#include <chrono>
#include <mutex>
#include "xdk/hle.h"
#include "xdk/dsound_voice.h"
#include "xdk/d3d8.h"

namespace {
// Xbox APU counter, 48 kHz, with its natural 32-bit wrap.
uint32_t ApuSampleCounter() {
    static const auto start = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    return static_cast<uint32_t>(static_cast<uint64_t>(us) * 48 / 1000);
}

}

// SetVertexShaderInputDirect supplies sixteen {stream, offset, type, reserved}
// descriptors plus {vertex buffer, stride, byte offset} for each bound stream.
X86_FUNC(sub_00251EE0) {
    const uint32_t declaration = kernel::R32(ctx.esp + 4);
    const uint32_t result = d3d::SetVertexShaderInputDirect(ctx, declaration,
        kernel::R32(ctx.esp + 8), kernel::R32(ctx.esp + 12));
    if (!result) {
        if (declaration) std::memmove(kernel::HostPtr(0x262BD4), kernel::HostPtr(declaration), 256);
        kernel::W32(SYM(D3D_Device) + 0xB00, declaration ? 0x262BC1 : 0);
    }
    kernel::Return(ctx, result, 3);
}

// XACT's hardware sample clock is converted to 100 ns reference time. Keep
// the accumulator and its 32-bit wrap behavior, and preserve callee-saved regs.
X86_FUNC(sub_00279D50) {
    static std::mutex clock_lock;
    std::lock_guard guard(clock_lock);
    const uint32_t self = ctx.ecx;
    const uint32_t now = ApuSampleCounter();
    const uint32_t previous = kernel::R32(self + 0x144);
    uint64_t delta = 0;
    if (now >= previous || (now < 100000 && previous > 0x40000000))
        delta = uint32_t(now - previous);
    const uint64_t samples = kernel::R64(self + 0x148) + delta;
    kernel::W32(self + 0x144, now);
    kernel::W64(self + 0x148, samples);
    const uint64_t reference = (samples * 10000 / 48000) * 1000;
    const uint32_t out = kernel::R32(ctx.esp + 4);
    kernel::W64(out, reference);
    ctx.eax = uint32_t(reference);
    ctx.edx = uint32_t(reference >> 32);
    ctx.esp += 8;
}

// IDirectSoundBuffer::Use3DVoiceData(this, enabled) toggles precomputed Xbox
// APU coefficients. Host voices already use the software spatializer and do
// not contain the private hardware voice pointer that this wrapper follows.
X86_FUNC(sub_00268439) {
    ctx.eax = dsound::Use3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8) != 0);
    ctx.esp += 12;
}

X86_FUNC(sub_0026841D) {
    ctx.eax = dsound::Set3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8));
    ctx.esp += 12;
}

// IDirectSoundStream uses the same public voice-data contract.
X86_FUNC(sub_0026849A) {
    ctx.eax = dsound::Set3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8));
    ctx.esp += 12;
}
X86_FUNC(sub_0026849F) {
    ctx.eax = dsound::Use3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8) != 0);
    ctx.esp += 12;
}
X86_FUNC(sub_002682A1) {
    ctx.eax = dsound::SetPitch(kernel::R32(ctx.esp + 4), int32_t(kernel::R32(ctx.esp + 8)));
    ctx.esp += 12;
}

// IDirectSound::CreateSoundStream(this, description, output).
X86_FUNC(sub_00269A1E) {
    const uint32_t description = kernel::R32(ctx.esp + 8);
    const uint32_t output = kernel::R32(ctx.esp + 12);
    const uint32_t result = GUEST_CALL(DirectSoundCreateStream, ctx, {description, output});
    kernel::Return(ctx, result, 3);
}

X86_FUNC(sub_0026845A) {
    ctx.eax = dsound::SetPitch(kernel::R32(ctx.esp + 4), int32_t(kernel::R32(ctx.esp + 8)));
    ctx.esp += 12;
}

X86_FUNC(sub_00268C68) {
    ctx.eax = dsound::SetOutputBuffer(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8));
    ctx.esp += 12;
}
X86_FUNC(sub_00268401) {
    ctx.eax = dsound::GetVoiceProperties(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8));
    ctx.esp += 12;
}
X86_FUNC(sub_00268495) {
    ctx.eax = dsound::GetVoiceProperties(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8));
    ctx.esp += 12;
}
