# Verification record — updated 2026-10-20

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
command buffer, crashing Adreno in `vkEndCommandBuffer`. The shared renderer
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

## PGR race audio and L.A. Rush driving crash

PGR's original mixer ignored `SetOutputBuffer`: engine layers bypassed the mix-in
buffers carrying car volume and 3D placement. It also misidentified the XDK 3925
`SetMode` wrapper at `0xFADE8` as `SetMaxDistance`; a mode value of zero became a
zero maximum distance and produced infinite/NaN gains once routing was enabled.
The configuration and host API now distinguish those calls. Distance setters
reject zero/non-finite distances. Bus gain, spatial placement, nested buses and
cycle rejection are covered by **67 public-API integration checks** linked to the
fresh Linux runtime. Linking the previous mixer into the same test fails at the
bus-attenuation assertion.

Native multi-car attract-race captures recorded pre-output peaks up to 7.125
before the fix. Routing restores attenuation, and a linked stereo peak limiter
keeps the final output below full scale without flattening overloaded samples.
Quiet output passes unchanged; release recovery and non-finite input handling
are tested. This is an output safeguard, not emulation of the original DSP effects.

The reported L.A. Rush Android crash was a SIGSEGV in XACT's event-queue comparator
after 128 seconds, not an Android memory-pressure exit. XACT raises IRQL to
DISPATCH_LEVEL to protect its queue on the Xbox's single CPU. The runtime now
serializes those elevated regions across host threads and preserves nested
levels. The regression test rejects the previous unlocked behavior. Together,
the concurrency and output-limiter tests pass **5 cases with no failed checks**.

The updated IRQL build remained alive during several minutes of Android Story
free roam, acceleration, steering, reverse and repeated impacts. Full missions,
every vehicle and long-term stability remain open. Both titles' Linux executables
and Android ARM64 libraries were rebuilt. The framebuffer lifetime fix is now
shared, including PGR's legacy-render-pass Android path.

After building PGR on Linux, run its audio checks without a window or audio device:

```sh
python3 ports/PGR1/tools/build_audio_smoke.py --run
```


## Seven-port maintenance and host settings pause (20 October)

All seven Android ARM64 APKs build from the combined source and the single
pinned SDL/glslang dependency build. The Linux builds now use the same prepared
SDKs. BLACK's Linux adapter was updated to pass its Vulkan context to the
shared transition/dependency helpers; its Android adapter no longer duplicates
SDK framebuffer members. Splashdown's overlay no longer duplicates `SetMode`.

Current checks: **86 SDK cases** on Lavapipe with no failed checks; **10 portable
host cases** for pause/clocks/IRQL/limiting; **67 PGR mixer integration checks**;
**5 BLACK mixer/folding cases**, **85 BLACK renderer cases**; and L.A. Rush's
**76 input / 32 audio checks**.
These results come from rebuilt test executables, separately from the earlier
historical verification above.

R3 opens a responsive panel over a retained frame and pauses guest calls,
clocks and PCM playback. It closes and resumes without injecting Start. Device
captures confirm PGR intro backgrounds remain unchanged while the panel is open.
L.A. Rush Story driving likewise retains both the scene and speed display;
closing resumes, and opening/closing over its original pause menu leaves the
original game paused. Burnout's loading/intro path and Home/reopen cycle were
also checked, including minimizing with the host panel open.

Full mission/race completion and subjective sound comparison across every title
remain separate checks. The original NFS pause/standings text alignment issue,
Splashdown spray comparison and handheld Career report are not marked resolved
by this shared-runtime maintenance.


All seven updated APKs are installed and copied to their SD game folders, with
matching SHA-256 hashes and current native payload/signature/alignment checks.
Six updated in place. Splashdown's build-local signing key was accidentally
removed during cleanup; after explicit user approval it was reinstalled using
the retained suite key. Its three restored save files match the verified backup,
and the game loaded them successfully. Renewing the same SD ISO grant restored
direct boot: Home released its native process, and reopening created a fresh
process without the picker. An Arcade race held both scene and lap timer while
R3 was open, and resumed when closed. Original APK/save backups remain private.

NFS and Links captures also retain an unchanged background while R3 is open;
BLACK's custom panel fits the Android viewport and responds to the same R3 key.
These are device smoke checks, not full game-completion claims.
