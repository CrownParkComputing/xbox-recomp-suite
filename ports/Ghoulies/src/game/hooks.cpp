// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "xdk/hle.h"
#include "xdk/dsound_voice.h"

// XACT uses the public DSOUND voice-data APIs on host mixer objects that have no
// Xbox APU voice behind them. L.A. Rush solved this with the bridges in
// overlays/larush-sdk.patch (dsound_voice.h); its working hooks are in
// ports/LARush/src/game/hooks.cpp. XTLID export ids are stable across XDK builds,
// so the same ids give these XDK 5659 addresses. The 0x1C-spaced thunks at
// 0x1C73A3/BF/DB are the buffer trio and the 5-spaced ones at 0x1C73FC..0x1C7441
// are the stream setters, matching L.A. Rush's 0x268401/1D/39 and
// 0x26845A/95/9A/9F clusters.
#define VOICE_BRIDGE(address, expr)                  \
    X86_FUNC(sub_##address) {                        \
        ctx.eax = (expr);                            \
        ctx.esp += 12;                              \
    }

// IDirectSoundBuffer: GetVoiceProperties, Set3DVoiceData, Use3DVoiceData
VOICE_BRIDGE(001C73A3, dsound::GetVoiceProperties(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)))
VOICE_BRIDGE(001C73BF, dsound::Set3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)))
VOICE_BRIDGE(001C73DB, dsound::Use3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8) != 0))

// IDirectSoundStream: SetPitch, GetVoiceProperties, Set3DVoiceData, Use3DVoiceData
VOICE_BRIDGE(001C73FC, dsound::SetPitch(kernel::R32(ctx.esp + 4), int32_t(kernel::R32(ctx.esp + 8))))
VOICE_BRIDGE(001C7437, dsound::GetVoiceProperties(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)))
VOICE_BRIDGE(001C743C, dsound::Set3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)))
VOICE_BRIDGE(001C7441, dsound::Use3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8) != 0))

// XTLID 0xB0076 (stream SetPitch) and 0xB0098 (SetOutputBuffer)
VOICE_BRIDGE(001C71F3, dsound::SetPitch(kernel::R32(ctx.esp + 4), int32_t(kernel::R32(ctx.esp + 8))))
VOICE_BRIDGE(001C7A96, dsound::SetOutputBuffer(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)))

// Bring-up accommodations below, from before the L.A. Rush bridges were wired.
// They accept guest APU helpers that still run on opaque host objects; revisit
// each once XACT is fully bridged and audio is verified by ear.

// The host DS object has no guest COM vtable; this aggregated-interface AddRef
// helper would call through [obj] -> [vtbl+4]. Keep the interface pointer the
// constructor stores and skip only the reference-count call.
X86_FUNC(sub_001C6B0F) { kernel::Return(ctx, kernel::R32(ctx.esp + 4), 1); }

// Voice-property helpers reached from stream setters. Each reads [this+0x10]
// or [this+0x0C]; the mixer holds no such structure.
// 0x1C6674 is CDirectSoundStream_GetStatus, now handled by the shared mixer's
// STREAM(GetStatus) via the config symbol, so no hook is needed here.

X86_FUNC(sub_001C7731) { kernel::Return(ctx, 0, 2); }
X86_FUNC(sub_001C5F95) { kernel::Return(ctx, 0, 2); }
X86_FUNC(sub_001C5D92) { kernel::Return(ctx, 0, 2); }
#define VOICE_NOOP(address, nargs) X86_FUNC(sub_##address) { kernel::Return(ctx, 0, nargs); }
VOICE_NOOP(001C5DAB, 2)
VOICE_NOOP(001C5DC7, 2)
VOICE_NOOP(001C5DDA, 2)
VOICE_NOOP(001C5DED, 2)
VOICE_NOOP(001C5E00, 2)
VOICE_NOOP(001C5E23, 2)
VOICE_NOOP(001C5E40, 1)
VOICE_NOOP(001C5E51, 2)
VOICE_NOOP(001C5E64, 2)
