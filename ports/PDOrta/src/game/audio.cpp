#include "xdk/hle.h"

// These stream entry points execute their own Xbox object code rather than
// tail-jumping to a mapped CDirectSoundStream method. The software mixer uses
// the same voice state for buffers and streams, so its public buffer setters
// can handle the identical argument lists without touching Xbox COM fields.
#define STREAM_SETTER(address, method)                \
    X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_##method)); \
    X86_FUNC(sub_##address) { GUEST(IDirectSoundBuffer_##method)(ctx, base); }

STREAM_SETTER(002C39CE, SetAllParameters)
STREAM_SETTER(002C3A06, SetMaxDistance)
STREAM_SETTER(002C3A1E, SetMinDistance)
STREAM_SETTER(002C3A36, SetMode)
STREAM_SETTER(002C3A3B, SetPosition)
STREAM_SETTER(002C3A64, SetVelocity)
STREAM_SETTER(002C3AD5, SetI3DL2Source)

// The mixer does not model cones, per-voice Doppler, envelopes or LFOs.
// Accept these at the API boundary, as for the shared SetVelocity/SetFilter
// implementations, instead of running hardware methods on opaque handles.
#define AUDIO_NOOP(address, nargs) \
    X86_FUNC(sub_##address) { kernel::Return(ctx, 0, nargs); }

AUDIO_NOOP(002C24DA, 2)  // buffer SetLFO
AUDIO_NOOP(002C24F6, 2)  // buffer SetEG
AUDIO_NOOP(002C37F2, 4)  // buffer SetConeAngles
AUDIO_NOOP(002C3816, 5)  // buffer SetConeOrientation
AUDIO_NOOP(002C384B, 3)  // buffer SetConeOutsideVolume
AUDIO_NOOP(002C393D, 3)  // buffer SetDistanceFactor
AUDIO_NOOP(002C3961, 3)  // buffer SetDopplerFactor
AUDIO_NOOP(002C3985, 3)  // buffer SetRolloffFactor
AUDIO_NOOP(002C2687, 2)  // stream SetLFO
AUDIO_NOOP(002C2691, 2)  // stream SetFilter
AUDIO_NOOP(002C39D3, 4)  // stream SetConeAngles
AUDIO_NOOP(002C39D8, 5)  // stream SetConeOrientation
AUDIO_NOOP(002C3A01, 3)  // stream SetConeOutsideVolume
AUDIO_NOOP(002C3A8D, 3)  // stream SetDistanceFactor
AUDIO_NOOP(002C3AA5, 3)  // stream SetDopplerFactor
AUDIO_NOOP(002C3ABD, 3)  // stream SetRolloffFactor
