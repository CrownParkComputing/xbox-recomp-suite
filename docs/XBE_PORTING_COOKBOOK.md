# Xbox XBE Recomp Porting Cookbook

A repeatable, evidence-led workflow for converting another original Xbox title with Xbox XBE Recomp. This covers the x86 XBE and Direct3D 8 SDK in `sdk/`; Xbox 360 Xenon/PPC ports use a different toolchain and are out of scope.

The goal for each title is a reproducible local build from the owner's game files, with no retail files or generated game code in the repository. Treat a port as a sequence of gates: identify the exact executable, translate it correctly, bring up the guest/runtime, then prove graphics, audio, input, saves, and packaging independently.

## Start a new port

1. Copy `templates/game` into a new game repository. Add the SDK as `sdk/` (copy or submodule), set the title, XBE and executable names in the build scripts, and add one `add_xbe_image(...)` entry for each XBE the title uses.
2. Keep the user's XBE, disc image, extracted files, keys, generated C++, objects and executable out of version control. Build from the user's local `assets/` directory. Check `.gitignore` before the first build and before every commit.
3. Run `xbe_symbols` on the exact `default.xbe`. Record title identity and hashes locally. Confirm the reported XDK build and select its matching `sdk/xdk/<build>/signatures.toml` and `layout.toml`. Stop if the SDK build has no layout; do not borrow a different build's offsets.
4. Review every unresolved or ambiguous XDK symbol. A zero address is a trapping stub, not a successful match. Use another owned title or known SDK copy to improve signatures, then regenerate. Keep game-specific hooks in `src/game`; never patch generated files as the durable fix.
5. Build and run the headless harness before diagnosing windowing or GPU output. This separates CPU translation, imports, kernel behavior and game-data setup from the host renderer.
6. Reach clean code generation: inspect function boundaries, jump-table targets, exception handling and unsupported instructions. Add only evidence-backed function/data/switch overrides to the title config. Rebuild after changing a gate input; stale generated output can make a failed build look successful.
7. Bring up in bounded steps: entry point, first guest calls, file access, first present, stable menu, then interactive gameplay. Record the last useful guest call, frame count and failure class for each run.
8. Validate each user-facing system: controller mapping and hotplug, audio signal and continuity, save/reload, display modes, graceful exit, and any secondary XBE/image transition.
9. Before calling the port complete, verify the intended executable, run from both extracted files and the supported disc/package form where applicable, and check the repository for accidental game content or build products.

## XDK builds with no SDK signatures

`xbe_symbols` stops with "no signatures for these library builds" when `xdk/<build>/signatures.toml` is missing. This snapshot has a 5849 signature set and layouts for 5849 plus partial 4721/4531 layouts. Use this order:

1. **Owned titles first.** Run `xbe_symbols` without `--out` on each owned `default.xbe` to read its library builds. Only a complete config for the same build can produce signatures; do not reuse another build's layout.
2. **Public sources.** Searched on GitHub for 4531 data and found none: upstream `TGP482/XboxXBE-Recomp`, the `Maslin0/XboxXBERecomp` fork (`dsteal`), `crazyriddler/FinestHourRemastered`, `TGP482/SCDA-XBERecomp`, `abilliontokens/urbz-recomp`, `parkerallan/doa3-recomp`, `gamboozino/ngbrecomp` and `nworkers/reJSRF` all ship only 5849 or none. `PatrickvL/DxbxOlderSvnImport` has 4531 XBE dumps but no signature or layout data. Re-check before relying on this list. Also search by title: the games in the Dxbx dumps on 1.0.4531 (Silent Hill 2, Morrowind, Enclave, Hitman 2: Silent Assassin, Crazy Taxi 3, Conflict: Desert Storm) have no Xbox recomp port on GitHub, only PC mods. Other Xbox recomp projects (`nemonicicon/ts3FPrecomp`, `Maslin0/DoubleSteal-XBERecomp`, `AsukaNeo/Exonerated`, `FridiNaTor1/FullSpectrumWarriorTHXBOX`) use XbSymbolDatabase for their XDK addresses and have no 4531 layout.
3. **XbSymbolDatabase addresses.** `Cxbx-Reloaded/XbSymbolDatabase` (MIT) has OOVPA tables per XDK build, including `OOVPADatabase/D3D8/4531.inl`. Build its CLI, then:

   ```
   XbSymbolDatabaseCLI assets/default.xbe > found.txt
   python3 sdk/tools/symbols/xbsdb_import.py found.txt <sdk> config/game.toml
   ```

   The importer fills `[symbols]` and `[data]` only. It does not write `[xbe]`, so write that by hand from the XBE header (title ID at the certificate's +0x8, entry point XOR the retail key `0xA8FC57AB`, executable sections by flag bit `0x4`) and leave `xdk =` out until a layout exists. Its output is addresses, not proof: review every zero (trapping stub), every ambiguous match and every duplicate OOVPA warning in the XBE's code.
4. **Layout is not imported.** `xdk/<build>/layout.toml` holds struct offsets that must be proven from the target's code, as the 5849 file was. Method used for 4531 (`xdk/4531/layout.toml`, partial):
   - Take the XbSymbolDatabase addresses for the D3D functions that touch the device (SetTexture, SetRenderTarget, SetViewport, SetTransform, SetScreenSpaceOffset, BlockUntilVerticalBlank, Swap, SetPixelShader, SetVertexShader).
   - Disassemble each one and read the `[reg+disp]` accesses to the device pointer. A store of a shader or surface handle, a compare against a neighbouring field, or a copy of 16 dwords with a `type*64` index each identify a field.
   - Cross-check with the same field in a second build's layout (4721 and 5849) to catch layout drift. Do not copy a value that the target's code does not show.
   - Record each key with the instruction address. Leave unproven keys out; the runtime traps on them where they are first needed.
   Until a layout covers the draw path, treat graphics as blocked.
5. **Trying another build's signatures** is only a diagnostic. Copying 5849 signatures to a 4531 folder matched 45 of 238 functions for Splashdown (XDK 4531), several only by weak constants. Do not commit that as a signature set.

Record the build, the importer counts and the open zeros in `PORT_STATUS.md`.

## Graphics diagnosis

When a frame is wrong, capture it and the draw/state evidence before changing the renderer. Keep a known-good frame and a short repeatable route. Change one state at a time and compare the same scene or the same submitted draw list. Do not use a clean boot, a working menu, or a high frame rate as proof that gameplay rendering is correct.

Use this order:

1. Confirm the guest issued the expected draw and selected the expected render target. If it did not, trace the title/game hook, resources and guest state before changing the backend.
2. Check target dimensions, viewport, scissor, screen-space offset, render-target transitions, depth/stencil state and texture formats against the guest values.
3. Inspect the translated vertex/pixel shader inputs and outputs, including constants and vertex layout. Verify coordinates before applying a global shift, flip or scale.
4. Compare the host result with the SDK CPU/reference renderer or a known-good capture when available. Reduce the failing case to a small draw/state sequence.
5. Make the narrowest fix at the layer that diverges, preserve the capture as a regression case, and recheck another title using that shared path.

For Android/Vulkan, treat surface orientation as an explicit transform. Keep the platform's supported swapchain transform when required by the driver, then rotate the final game image in the presentation blit. Rotate the host settings UI consistently. Test on the physical GPU: an identity transform or clear path that appears valid in code may still fail in the device driver.

Do not “fix” clipped text by moving every label until vertex positions, viewport/scissor and screen-space offsets have been captured. Pause menus, standings tables and HUD often share a text path; a shared coordinate bug should be fixed once at its producer or transform.

## Lessons from the current ports

These are observations to carry forward, not claims that every port is finished:

- **BLACK:** the Linux/Vulkan port documents a renderer mapping with negative-height Vulkan viewports to preserve D3D's vertical convention and winding. Keep a cross-backend or CPU-reference comparison in the graphics workflow; matching API names does not guarantee matching pixels. Its Linux build also needs the shader compiler library beside the executable.
- **Burnout 3:** keep title hooks and runtime changes separate. A title-specific behavior belongs in `src/game`; a reusable XDK or renderer correction belongs in the SDK and should be checked against other ports before it is copied downstream.
- **Links 2004:** keep local game staging independent from source and build output. Confirm the selected disc/extracted data before debugging a loader or graphics failure; do not remove large staging assets as routine cleanup without checking their owner.
- **Need for Speed: Hot Pursuit 2:** on the Retroid/Adreno Android build, a clear-only legacy Vulkan render pass crashed at command-buffer end. Full-surface clears now use transfer clear commands. The device also required retaining its current swapchain transform and rotating the final blit; requesting identity caused another driver crash. Test transforms and clears on hardware, and preserve a screenshot/capture for orientation checks. The Xbox-labelled Retroid pad already exposes SDL mappings for both sticks, triggers, shoulders, face buttons and stick clicks; reserve R3 for the host settings menu only when that is the intended control contract. Original pause and standings text alignment remains an open graphics issue and must not be marked fixed until captured and verified.
- **Project Gotham Racing (PGR):** graphics remain an explicit bring-up gate. The port README's graphics section is blank, so do not infer rendering support from successful compilation or generic SDK claims. Establish a first stable frame, verify the 3D scene and UI independently, and keep missing or distorted content on the port's open-issues list until device/gameplay evidence closes it.

Across the ports, distinguish three cases before editing graphics code: the title did not issue the draw; the guest draw/state is wrong; or the host translation/presentation differs from the guest. Captures should let the next person repeat that distinction without rediscovering it.

## Runtime and SDK changes

- Put reusable fixes in the canonical SDK, then synchronize each port's vendored `sdk/` copy with the supported sync workflow. Record deliberate per-port divergences so a sync cannot silently erase them.
- Rebuild every affected port after a shared change. A successful compile proves only that it links; run the affected title far enough to exercise the changed path.
- Preserve title-specific behavior in hooks/config instead of adding title-ID branches to common kernel or renderer code unless multiple titles demonstrate a shared contract.
- Add a focused regression at the lowest layer that reproduces the failure. Keep at least one title-level smoke route for the behavior that the unit case cannot cover.
- Keep diagnostic switches narrow and reversible. Capture a baseline, change one variable, collect evidence, then remove or disable temporary instrumentation before release.

## Completion record for each conversion

Keep a concise `PORT_STATUS.md` with:

- XBE title ID, XDK build, entry point, local file hashes, and supported input layout;
- build command and required local assets/tools;
- current gate and evidence for CPU, kernel, graphics, audio, input, persistence and packaging;
- known issues, each with a reproduction route and the next discriminating check;
- shared SDK changes and which other ports were rebuilt or checked.

Use precise states such as **builds**, **boots**, **presents**, **renders menu**, **renders gameplay**, and **playable**. Do not collapse them into “working.” A port is ready for the next conversion when its recipe is reproducible from clean generated output, its open graphics and audio issues are stated, and its local-only game data stays out of the deliverable.

## Source-suite recipe and Android lessons (2026-10-19)

This checkout is one source repository containing seven ports, the tested shared
SDK, Splashdown's explicit overlay, common SDL/Android integration, and pinned
third-party source trees. Use `tools/prepare.py` and the commands in
[BUILDING.md](BUILDING.md). Their paths are relative to the checkout; they do not
require the original working directories. Original game files and generated
translation remain private inputs, not versioned source.

### Preserve the SDK variant that was actually tested

Five ports use `sdk/`. Splashdown's tested variant is derived with
`overlays/splashdown-sdk.patch` into ignored `.local/sdks/`. Do not blindly sync
Splashdown's 4531-specific rendering/audio changes into other titles. Keep
reusable controller/document fixes in the shared SDK; record deliberate variant
differences explicitly. Applying the old seven-patch history on top of the
already prepared SDK applies changes twice.

### Document ownership and session cleanup

Android document grants are the storage interface. Duplicate the supplied ISO
file descriptor, use `pread`, and keep ownership explicit. Extracted SAF trees
need case-insensitive directory lookup and bounded owned descriptors. A 64-entry
LRU can release evicted descriptors while retaining a shared reader for in-flight
reads. Test partial reads, eviction/reopen and complete descriptor destruction.

Persist the granted URI and media kind before starting the dedicated game
process. On later launch, validate that selection and boot directly. If it is
unavailable, show the picker; an older private copy must not silently mask the
failure. End the native `:game` process on minimize/close so every guest thread,
GPU allocation, audio stream and document handle is released. Check that the
native PID disappears and reopening produces a different one without a picker.

Migrate data to SD by comparing every file hash, booting from the selected tree
with the internal game copy quarantined, and only then deleting the verified
duplicate. Keep saves and settings. A successful copy command alone does not
establish a safe migration.

### Controller ports and menu wraparound

Splashdown's four-item menu stayed on Arcade because XInputOpen returned handles
for three disconnected ports. The guest reused the first pad's report on those
failed polls, applying four identical navigation steps and wrapping to Arcade.
Reject opening absent controllers. Test the HLE contract: one connected port,
three absent handles, bounded report writes, and closed-handle behavior.

### Spray is a shader/state problem before it is a physics problem

The game issued point-list particle draws, but the runtime discarded vertex
point size and generated no stage-3 sprite coordinates. Its older 4531 state
indices also differed from later XDKs. Mapping the actual deferred/complex state
ranges, carrying point size through the vertex output, and expanding correctly
textured quads restored spray in a moving race. Test fixed/programmed point size,
size limits, texture coordinates, and two render scales. Do not compensate by
disabling depth testing, blending, or visibility checks globally.

### Audit audio by source and output

Instrument a moving race, not only a menu. Confirm the music decoder supplies
PCM, packet consumption advances, the own-engine voice changes pitch, water and
ambient voices have gain, and final mixed samples are nonzero. Then verify the
Android output stream is active/unmuted. Splashdown passed those presence checks;
DSP/reverb and velocity Doppler fidelity still remain incomplete. Nonzero PCM
proves signal presence, not complete original-console sound.

### Legacy Vulkan and framebuffer lifetime

BLACK's custom renderer needed format/MSAA-compatible legacy render passes and
KHR/EXT dispatch on Adreno 650. Dropping *all* cached framebuffers during first
swapchain configuration destroyed offscreen framebuffers referenced by the
current, unsubmitted command buffer. During swapchain recreation, release only
framebuffers holding the old swapchain views; retire offscreen caches after the
owning frame fence. Rotate the final blit and ImGui data consistently with the
swapchain's required pre-rotation. Test a real device, not only Lavapipe.

The shared renderer exposed the same lifetime fault on L.A. Rush's first Android
frame: Adreno crashed in `vkEndCommandBuffer` after three offscreen framebuffers
had been discarded. The shared renderer now marks swapchain-backed framebuffer
keys and uses selective cleanup. Device logs confirm that all three offscreen
framebuffers survive initial swapchain creation; intro/title playback and a
fresh-process relaunch then succeed.

### Keep completion claims narrow

Boot, intro playback, menu rendering, gameplay rendering, audio fidelity and
performance are separate gates. For example, a report of a solo Career session
first needs its mode identified: qualifying and an actual opponent race are
different evidence. Confirm the main-race route before changing AI creation or
visibility. Record the latest unresolved user observation instead of marking an
entire mode playable from a title-screen capture.


### L.A. Rush: overlapped reads and XACT initialization

Follow the engine's completion path when a resource inflater fails. L.A. Rush
pipelines eight DVD reads; returning synchronous success skipped its byte-count
collection and produced an underflowed compressed input length. Preserve each
request's IO_STATUS_BLOCK, event and offset, and return STATUS_PENDING for
asynchronous handles even when the host completed their work immediately.
Synchronous handles still return success. Compare decompressed resources with an
independent decoder, and exercise multiple outstanding reads in an integration
test.

A success stub for XACT engine initialization hid the next set of faults and
created null sound-bank pointers. Restore initialization before adding null
checks. Its public DirectSound calls need host bridges for stream creation,
SetPitch, Use3DVoiceData, Set3DVoiceData, GetVoiceProperties and mix-in routing.
GetVoiceProperties writes a 0x5C-byte structure; it is not the 0x38-byte calculated
3D data structure. Keep XACT's light-HRTF math callback initialization even when
hardware DSP processing is replaced by a host mixer.

The APU reference clock uses 48 kHz samples and 100 ns time units. Preserve
callee-saved registers and test counter wrap; a clock stub that clobbers EBX can
look like an input/menu bug. Compare individual host controller fields rather
than padding bytes when deciding whether an XInput packet changed.


### L.A. Rush: damaged vehicle streams and merged tails

After impact, the body disappeared while its driver and HUD remained. Frame
traces showed FLOAT3 vehicle shaders receiving zero vertex-buffer bindings.
The game uses SetVertexShaderInputDirect for deformed meshes rather than normal
SetStreamSource bindings. Preserve its sixteen input descriptors, per-stream
buffer/stride/byte offset, and buffer references. Restore ordinary bindings when
the declaration is null, and carry stream byte offsets through both raw and
converted vertex assembly and the render thread snapshot.

Hook the callable entry: in this executable 0x251EE0 copies the direct declaration
and jumps to 0x252470. The recompiler merges that tail into the entry's generated
function; a separate override named for the interior target will not run. Check
the generated function mapping and actual call sites before choosing a hook.
Verify an impact after implementing the bridge; a stationary intact car exercises
a different vertex path.

### PGR: engine buses, sound-mode signatures and clipping

Capture a race with opponents when investigating thunder-like engine noise.
PGR routes several engine layers into mix-in buffers; those buses carry volume
and spatial placement. Ignoring SetOutputBuffer plays every layer directly.
Apply bus levels through the routing chain, reject cycles, and test a muted bus,
a distant car and several simultaneous inputs using the actual public APIs.

Equal argument counts do not identify sound functions. In PGR's XDK 3925 image,
0xFADE8 forwards an integer mode to the voice's +0x58 field. Labelling it
SetMaxDistance turns mode zero into a zero distance and then infinite mixer gain.
Check the final field store and test the wrapper at its real guest address.
Reject non-finite/zero distances before they can poison the output.

Measure peaks before the final output stage as well as after it. A linked stereo
limiter preserves channel balance and waveform shape when many voices overload;
quiet blocks should pass unchanged and recovery should be gradual. This avoids
hard clipping while DSP dynamics/effects remain approximations.

### L.A. Rush: single-CPU IRQL protection on concurrent hosts

A driving crash in XACT's queue comparator was not an Android memory kill.
The original engine protects that queue by raising IRQL to DISPATCH_LEVEL, which
excludes competing threads on the single Xbox CPU. Updating KPCR.Irql alone does
not provide that exclusion when native host threads run concurrently. Serialize
entry across host threads and release only when lowering below DISPATCH_LEVEL;
nested raises must retain the lock. Test competing mutations and demonstrate that
the test fails with the old unlocked behavior, then exercise real driving past
the original failure time.


### Suite maintenance: pause every guest subsystem, retain a responsive host

R3 now has one contract across Linux and Android: open host settings and suspend
play; close settings and resume. BLACK connects its custom panel through the
same state, rather than bypassing the shortcut. Reserve the closing click too.
On Android, intercept `KEYCODE_BUTTON_THUMBR` before SDL's per-device mapping,
since controller clicks can arrive with a keyboard source. Suppress repeats.

Blanking guest input is insufficient: a vehicle keeps coasting and opponents
keep racing. Park guest workers at HLE/kernel/indirect-call boundaries, but let
an elevated IRQL region finish before parking its owner. Never park while
holding a host renderer or input mutex. The idle render queue must continue to
redraw the retained front surface and panel. Keep these redraws separate from
guest GPU present/fence numbering, and defer destruction of render targets for
settings changes until play resumes. An early R3 request must allow device
creation and a first renderable frame before suspending the guest.

Use one pause-aware monotonic clock for the guest tick count, performance
counter, RDTSC, relative waits and timer deadlines. Exclude elapsed panel time
from the guest system clock too. Stop vertical-blank callbacks and hold PCM
voice/stream positions; muting a running decoder loses music and streamed audio.
Host logging, window messages and UI animation continue on real time. Do not
inject Start to simulate a pause: it can instead unpause a title which was
already paused. Test both original pause states, repeated toggles and early
startup.

The portable regressions cover clock freezing/resumption, guest worker wakeup,
early-menu startup, elevated-IRQL exclusion and stereo/surround limiting.
PGR additionally exercises the public HLE mixer; BLACK exercises its separate
bus mixer and stereo/mono folding. Preserve these distinct mixer tests rather
than assuming a change to the shared DSOUND file reaches BLACK's replacement.
BLACK now validates bus targets/cycles and applies nested bus gain/distance;
its final limiter runs after folding so a valid 5.1 block cannot overload stereo.
This does not reproduce the full Xbox effects DSP or every bus channel matrix.

A shared SDK promotion requires building every overlay, not merely checking
that a patch applies. Splashdown retained its old `SetMode` handler after that
handler became shared, producing duplicate definitions. BLACK's Android adapter
likewise attempted to insert framebuffer members already supplied by the SDK.
Remove the duplicate adaptations; keep title-specific behavior in explicit
overlays and keep all seven builds on the audited base.

### Android: foreground state is asynchronous

Starting an ordinary service from a game Activity's `onCreate` can race Android's
foreground UID transition. The observed result was
`BackgroundServiceStartNotAllowedException` before native boot. Start the optional
task-removal guard after window focus, catch background-start rejection, and
retain dedicated-process teardown independently. Test a cold launch after an
APK update, a launch from sleep, Home/reopen and the remembered SD grant.

### Cleanup: one active source checkout and reproducible outputs

Keep the combined repository as the active source, one pinned dependency build,
one current Linux build and APK per title, private retail inputs and saves, and
compact verification records. Archive unique local source changes and save data
before removing superseded builds. Distinguish Xbox `E:` saves from regenerable
`X:`/`Y:`/`Z:` caches. Never remove a signing key or asset directory as build
cleanup, and never let ignored retail/generated files enter a source commit.
A clean checkout and a verified full Git bundle preserve the source history.


Before recursive cleanup, inspect every candidate directory for signing keys,
including keys stored inside ignored `build/` folders. Preserve them privately
and compare their public certificate against the installed APK before removing
anything. Source archives which exclude build products and credentials do not
back up those keys. Without the original key an Android update requires a
reinstall, save restoration and renewed SD document permission; do not silently
uninstall an existing app to overcome a signature mismatch.

Keep a title's executable mapping consistent with CMake's actual output name.
BLACK's `Black` mapping differed from its Linux `BLACK` executable; Linux image
relaunch is case-sensitive, so the mapping now uses the actual output name.
