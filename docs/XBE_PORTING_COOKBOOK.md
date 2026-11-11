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

### Panzer Dragoon Orta: a refused host action hides a skipped XAPI init (2026-11-05)

Orta booted to a black screen and, after 11–38 s, asked to launch an image with an empty path.
The empty path was the clue: `XLaunchNewImageA(NULL, ...)` is `XapiBootToDash`, and the XAPI only
reaches it from a failure. The chain, all proven in the image:

- The certificate's AllowedMedia (`cert + 0x9C`) is `0x00000002`, DVD only. `XapiInitProcess` then
  verifies the media before it finishes: it opens `\Device\Cdrom0` and sends MODE SENSE(10) for the
  drive's vendor page `0x3E` through `IOCTL_SCSI_PASS_THROUGH_DIRECT` (`0x4D014`), five times.
- A zero-filled answer reads as `STATUS_UNRECOGNIZED_MEDIA`, so the XAPI booted to the dashboard.
  The runtime refuses that launch, the call returns, and the title keeps running — with the rest of
  its process initialization never done: no utility drive (`Z:`), no `T:`/`U:` links, no parental
  ratings check, no static initializer table.
- The runtime now answers that one mode page in `NtDeviceIoControlFile`. Only the three bytes the
  XAPI reads are known from its own code; the rest of the vendor page stays zero.

Two lessons carry forward. First, read the certificate before blaming the title: AllowedMedia and
the XBE init flags (header `+0x124`: bit 0 mounts the utility drive, bit 1 formats it) decide which
of these paths a title takes at all. Four more ports in this suite ship AllowedMedia `0x2`.
Second, a host refusal is a silent fork in the guest's control flow, not an endpoint. Every
"refused"/"not supported" line deserves the question of what the title had queued behind it.

### Enumerating a directory: "*.*" arrives as an empty pattern

With `Z:` mounted, Orta still spun: 21 865 opens of one sprite in 12 s, each preceded by a failed
create of `z:\sprite\<name>.tmp`. The parent folder did not exist, and the title never created it,
because its cache mirror walks `d:\` with `FindFirstFile("d:\*.*")` — and the XAPI zeroes the
`FileName` string's length for the pattern `*.*` (`0x22E3E5` in this image). FATX reads an empty
pattern as every entry; `NtQueryDirectoryFile` matched it literally and returned nothing, so the
walk found no folders to mirror and every cached write below one failed forever.

`NtQueryDirectoryFile` now treats an empty pattern as `*`. Expect the same shape elsewhere: a
title that enumerates its own files, finds none, and then fails somewhere far from the enumeration.

### Counting a spin, then placing it

The useful order for a title that is running but making no progress:

1. `XBE_PERF=1` separates "stuck" from "ticking but drawing nothing": the per-second line reports
   draws, vertices, uploads and texture counts, and a frame loop with `0 draws` is a different
   problem from a frozen one.
2. `strace -f -e trace=openat` (or `mkdirat`, `renameat`) counts a retry loop without touching the
   build and names the exact path being retried. Kill the traced process, not just `strace`, or the
   title keeps running and the next run's log and cache partition are shared with a ghost.
3. A temporary, env-gated stack log in the kernel export that the loop calls places it in the
   title's own code (`LogGuestStack` prints the return addresses on the guest stack). Remove it
   once the guest function is identified; the generated `x86_recomp.*.cpp` then reads like a
   disassembly listing and the surrounding logic is usually obvious.
4. F3 dumps every guest thread's stack, and with `XBE_CAPTURE_DIR` set it also captures a frame.
   It needs the game window focused. On this Wayland desktop `ydotool` never reaches the
   compositor, but a uinput device created directly (python-evdev: a virtual keyboard, or a pad
   that SDL picks up from `/dev/input`) does.

An XDK 4928 layout now exists at `sdk/xdk/4928/layout.toml`, proven from Orta's draw path; every
proven field matched 4721.

### Grabbed by the Ghoulies: name the exports from .XTLID, not only from OOVPA (2026-11-05)

An XBE keeps a `.XTLID` section: pairs of (XTL export id, address) for every library function the
linker kept. The high 16 bits are the library (8 = D3D8, 0xB = DSOUND, …) and the ids are stable
across XDK builds, so the SDK's `signatures.toml` entries - which record `xtlid` per name - resolve
names in an image whose build has no signature set at all. On Ghoulies (XDK 5659) that matched 41
names: 40 agreed with XbSymbolDatabase's OOVPA scan exactly and one was found that OOVPA missed.
Use it as a cross-check on every imported address before trusting a config, and as the source for
the ones OOVPA cannot match.

The ids are also assigned in alphabetical order within a library, so an id with no SDK name can
still be placed between two named neighbours: `0xB0055` sits between `IDirectSoundBuffer_GetStatus`
and `_Lock`, which makes it `GetVoiceProperties`, and `0xB0076` between `_SetOutputBuffer` and
`_SetPlayRegion`, which makes it `SetPitch`. Treat that as a hypothesis to confirm against the
function's own code, not as proof.

Because the ids are stable they also carry a *working port's* hooks across to a new one. L.A. Rush's
XACT voice bridges are game-specific `X86_FUNC(sub_<address>)` hooks; each of those addresses is a
`.XTLID` entry, so look the id up in the new XBE's `.XTLID` and you have the new address without
disassembling it. Internal thunks that are not exported sit at the same fixed offsets from an
exported neighbour (`+0x1C`, `+5` in this pair of images), so cluster shape resolves those too.
This is how Ghoulies (XDK 5659) got L.A. Rush's `Use3DVoiceData`, `Set3DVoiceData`,
`GetVoiceProperties`, `SetPitch` and `SetOutputBuffer` bridges; verify a couple of the derived
bodies against the generated C++ before trusting the whole batch. Compare the body of a COM thunk
to name it carefully: every `IDirectSoundBuffer` method subtracts `0x1C` from the interface
pointer, so that shape alone does not distinguish `Pause` from `Use3DVoiceData`.

An absent `D3DDevice_BeginPush` silently drops geometry. The HLE `EndPush` only executes the
inline push buffer when the HLE `BeginPush` has handed out a scratch pointer; with the symbol 0,
`g_push_begin` stays 0, `Execute` is skipped and every raw push command written between the pair
is lost. The title still draws its normal API geometry, so this looks like "some objects are
missing" rather than a crash. Find the guest `BeginPush` by pairing its call sites with `EndPush`
in the generated code (Ghoulies: `0x177250` / `0x177270`), then map it. A signature scan can
report `BeginPush = {}` — an empty signature is not evidence the function is absent. After
mapping, recorded draws per frame rose from ~138 to 173-323 and the missing candle flame, hand
and book detail appeared.

The rest of what this port needed on its first day, all in the shared SDK:

- **Do not require a layout key the image cannot have.** `Direct3D_CreateDevice` demanded
  `CDevice_IndexBuffer`, but this image has no `D3DDevice_SetIndices` - the linker dropped it, and
  no code in its D3D section ever stores the field. A key that only one function reads belongs in
  that function's `XDK_REQUIRE`, so a title that never calls it still boots.
- **Catch SIGSEGV.** The crash handler covered SIGABRT, SIGFPE, SIGILL and SIGBUS. A guest that
  walks off its address space is the common bring-up fault, and without the handler it died with no
  guest stack at all. It now covers SIGSEGV and still re-raises for the core dump.
- **Back the APU sample counter.** A title's audio engine may read the APU register at
  `0xFE80200C` directly instead of asking DSOUND for the time - Ghoulies' XACT clock does, and
  converts the value with 48000 samples and 10000 units of 100 ns. The kernel now commits that page
  and keeps the counter moving on the pause-aware game clock. Nothing else of the APU block is
  modelled; expect other registers to need the same treatment.
- **A null `D3D_FvfShader` kills the first draw.** The device's current vertex shader object stays
  null without it, and the first push-buffer state emit dereferences it. Find the device init's
  store into `CDevice_VertexShader` and record it in `[data]`; Panzer Dragoon Orta needed the same.

### Orta: movies and gameplay need the guest's CPU and API boundaries (2026-11-10)

Comparing with [wootbeer/ortarecomp](https://github.com/wootbeer/ortarecomp/tree/5f396c90ad7a59ac5a07204102bfbfab140d32f2)
exposed four gaps after the title rendered. CRI's priority-based lock assumes one
Xbox CPU: use a recursive host mutex for its regions and server passes, and give
up the guest CPU while waiting for that mutex. Orta now opts into guest execution
serialization, with kernel callbacks on the same CPU and cooperative suspension
at reacquisition. Its syscall-free spin helper needs an explicit yield too.

Then test teardown and active play. XAPI `GetExitCodeThread` reads the guest
ETHREAD's ExitStatus at `+0x120`; updating only a host field left movie teardown
waiting forever after its worker exited. `IDirectSoundBuffer_AddRef` and Pause
must be replaced when buffers are host objects. `DrawIndexedVerticesUP` must also
reach the host backend: leaving the original API running divided by uninitialized
hardware vertex state in Episode 1. For an indexed UP draw, upload through
`max(index) + 1`, use the caller's stream with base vertex zero, then restore the
prior stream and base vertex. The shared API now does this.

A translated body is not necessarily an indirect dispatch entry. Three graphics
methods in the vtable at `0x332D00` had bodies reached from preceding padding but
no mapping at the actual vtable addresses. Seed those actual addresses in
`[functions]` and inspect neighboring class tables for the same issue. Orta's
port status records the resulting movie and Episode 1 checks and their limits.

### Temporary diagnostics must be safe for every port (2026-11-10)

The Orta bring-up left `LOG_FIRST(6, "d3d: TEMP EndPush ...")` in the shared
`D3DDevice_EndPush`. Its arguments read `R32(g_push_begin)` and the next two
words unconditionally, so a port with no `D3DDevice_BeginPush` (Grabbed by the
Ghoulies, `0` in its config) faults on its very first `EndPush`: GDB showed
`sub_00177270 + 84` doing `mov (%rsi,%rdx,1),%r9d` with a zero head. Guard a
diagnostic with the condition that makes its reads valid, and rebuild another
port (Orta was rebuilt here) before assuming a shared change is harmless. A
`TEMP` log is still production code for the nine ports that share the SDK.

### Thin compressed textures keep their logical image extent (2026-11-11)

Ghoulies' decoded Bink smoke texture matched GPU memory byte for byte, including
alpha, yet the effect and an offscreen pass still appeared black. Both shaders
also sampled DXT1 ramps only two pixels high. The Vulkan renderer enlarged those
images from 16x2/256x2 to 16x4/256x4, exposing unused black block rows to normalized
texture coordinates. Keep the image extent and mip count from the guest; round
only compressed storage rows and byte counts to complete blocks. Vulkan permits
partial blocks at image edges, as described in the
[Khronos image-copy guide](https://docs.vulkan.org/guide/latest/image_copies.html#_partial_texel_block).

A useful regression gives valid texels one colour and padding texels another,
then compares rendered pixels against the logical image. The four thin-extent
cases fail before this fix and match the CPU reference exactly afterwards.
Capture the actual source texture before diagnosing a movie upload failure;
a black cleared target in a frame with no contributing draws is insufficient
evidence. See Ghoulies' port status for the resulting smoke/book-cover captures.
