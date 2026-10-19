# Verification record — 2026-10-19

## Source import checks

- Built `xbe_recomp`, `xbe_symbols`, and `iso_extract` using the checked-in
  Zydis/Zycore, toml++ and ImGui sources, with the existing pinned DXC binary cache.
- Built and ran Splashdown's prepared SDK test target on Lavapipe:
  **76 cases, 0 failed, 0 failed checks**.
- Compiled the Android ARM64 SDL, glslang, SPIRV-Tools and SPIRV dependencies
  directly from `third_party/` using NDK 28.2.13676358.
- Staged every title's Android Java/manifest/native sources without game data;
  the compatibility staging entry point also succeeds.
- Compared the prepared Splashdown runtime, tests and XDK layouts byte-for-byte
  against the working SDK snapshot used for its previous device build.
- The Git index audit passes: pinned dependency source hashes match; no retail
  executable/image magic, private game input/output directories, generated port
  code, APKs, signing keys or credential patterns are present.

- Rebuilt BLACK's complete ARM64 native library and signed APK using the suite's
  own dependency builds and private, ignored local retail/generated inputs.
  APK signature/alignment checks pass; the APK and key are not versioned.

A source build check does not replace on-device gameplay and audio/performance checks.

## Existing implementation/device evidence

The six pre-import APKs were installed on the Retroid Pocket Flip 2 (Android 13,
Adreno 650, Vulkan 1.1.128). All remembered their granted SD media and restarted
with fresh native processes after minimizing. NFS was also tested with a
selected path temporarily unavailable: the picker returned, then normal direct
boot resumed after the path was restored.

Splashdown's native Hawaii race shows rider, water, HUD and restored particle
spray. Menu navigation past Arcade is fixed. Its moving-race mixer has music,
engine and water/ambient signal, with an active unmuted Android stereo output.
Its earlier six native smoke programs passed, including 10 controller, 11
render-state and 28 audio checks. DSP/reverb and velocity Doppler fidelity remain
incomplete. The latest spray change still needs explicit handheld comparison.

BLACK reaches its intro/title with legacy render passes and correct screen
orientation. PGR1, Links, NFS and Burnout show intro playback from their SD
folders; Burnout also gets past loading. Full gameplay/performance of those
sibling ports was not established by these boot checks.

The user reported a solo first Career race after skipping qualifying. A native
reproduction of Normal / Andy / Maui with the explicit **X: Skip**, then
**A: Start Race** sequence shows fifth place, a HUD labelled **Race**, and visible
AI opponents. The qualifying session has a HUD labelled **Qualify** and is solo.
The handheld report still needs its HUD/mode confirmed; no AI code change was
made and Career is not marked fully verified.

Game screenshots, sound captures, file manifests and local retail-derived build
products are kept in the original private verification workspace and are not
included in this public source import.


## L.A. Rush addition

The seventh source port has an Android arm64 APK installed on the Retroid Pocket
Flip 2 (Android 13, Adreno 650). All 2,132 files in its SD game folder matched the
local SHA-256 manifest. The launcher retained its document-tree grant and folder
selection. Intro video, the title screen, and a Home/reopen cycle were verified;
the native PID exited on Home and a new PID booted without the picker.

Initial device boot exposed an offscreen framebuffer lifetime bug: creating the
first swapchain destroyed framebuffers still referenced by the unsubmitted
command buffer, crashing Adreno in `vkEndCommandBuffer`. The title's SDK overlay
now removes only swapchain-backed framebuffers on view recreation. The corrected
APK retained three offscreen framebuffers and rendered past the former crash.

Desktop Vulkan validation used the real retail guest code on Lavapipe, with
intro video/audio, profiles, Story cutscenes, Beverly Hills driving, acceleration,
reverse, steering, pause and map. Vehicle meshes remain visible after two impacts
following the SetVertexShaderInputDirect bridge. Five integration programs pass
202 checks: 66 file I/O, 12 clock, 76 input, 32 PCM mixer/voice routing, and 16
custom vertex input/byte offset/reference lifetime checks. APK signature and
16 KiB ZIP alignment checks pass. Full races, mission completion and handheld
performance remain unvalidated. See ports/LARush/PORT_STATUS.md.
