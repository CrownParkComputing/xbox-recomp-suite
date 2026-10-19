# L.A. Rush port status — 2026-10-19

This is an experimental playable build, not a completed port.

## Verified on desktop Vulkan (Lavapipe)

- Retail game entry, intro XMV video/audio, title and menu navigation.
- New profile creation, loading the saved test profile, Story cutscenes.
- Hollywood/Beverly Hills loading, textured scenery, player vehicle and HUD.
- Acceleration, reverse, steering, pause/continue, and the map.
- Sound banks initialize through the original XACT engine. Streaming menu music,
  dialogue and gameplay output produce nonzero PCM.
- Kernel file integration: 66 checks; APU reference clock: 12 checks; controller:
  76 checks; actual PCM mixer/voice routing: 32 checks; custom vertex input: 16 checks.

## Fixed

- Overlapped DVD reads reported synchronous success without the byte-count path
  expected by the engine's eight-read pipeline. Large resources failed to inflate
  and the game showed a dirty-disc error. Completed asynchronous requests now
  return pending, preserving each request's status, bytes and event.
- The old hardware-clock stub froze time and clobbered EBX. The replacement
  advances the sample accumulator, preserves callee-saved registers and handles
  32-bit counter wrap.
- Padding in host controller structures caused spurious packet changes.
- A copied hook disabled XACT engine initialization. Restoring it required bridges
  for sound stream creation, pitch, calculated 3D data, voice properties and mix-in
  routing. Light-HRTF guest math initialization must also run.
- Gameplay effect callback 0x89830 was reached indirectly and needed an explicit
  recompiler entry point.

- Vehicle bodies disappeared after impact because custom vertex streams were
  absent from host state. SetVertexShaderInputDirect now binds the declared
  streams and byte offsets, retains their buffers and restores ordinary bindings
  on reset. Damaged meshes render after two reproduced impacts, including reverse
  and steering. The trace no longer drops the vehicle draws for missing streams.

## Still open

- Android APK compiles and is signed/aligned, but this title has not been installed
  or tested on the handheld because no ADB device is connected.
- Full races, mission completion, every vehicle, performance on Android, and
  longer gameplay sessions are not validated. Xbox DSP effects/reverb, envelopes
  and LFOs remain incomplete in the shared software mixer.

Only private scratch test profiles were used. Generated code, GPU captures, game
resources and test saves stay outside the source snapshot.
