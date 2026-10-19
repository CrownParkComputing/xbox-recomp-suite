# Splashdown XBE Recomp - Port Status

## Identity
- Source: Splashdown.USA.XBOX-ZTM.rar (extracted to assets/, local only, not in git)
- default.xbe SHA-256: 261b89b68bb68c9f087d0fafa2edfe6978555a8cc5d08c377c0be8085bb97cb1
- Title ID: 0x4947001B
- Entry point: 0x13A80B (retail XOR key 0xA8FC57AB applied to the header value)
- XDK build: 4531 (XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, LIBCPMT all 1.0.4531)

## Gate
**Android and Linux boot into the native game, and racing renders the rider,
water, HUD, and particle spray.** The latest ARM64 APK is installed on the
Retroid Pocket Flip 2. Android reads the selected USA ISO directly from SD,
remembers its grant, and boots without the picker when that location is present.
Closing or minimizing ends the dedicated game process; reopening starts clean.
The main-menu navigation wraparound and missing point-sprite spray are fixed.

- Full recompile succeeds: 9,559 functions, 596,285 instructions, 286 jump tables (includes extra entry point `0x19E004`, discovered as an indirect callback during the Hawaii load route).
- Native Linux build and headless harness build succeed.
- Symbols: 138 of 255 configured function symbols are nonzero, including the manually identified audio and drawing entries below; remaining zero entries may still trap if called.
- Layout is partial; the VBlank counter and shader-definition offset are now evidence-backed.

## Android sessions, controller navigation, spray, and audio (2026-10-19)

- `android/app/src/main/java/org/xberecomp` supplies the shared ISO/folder picker
  and dedicated-process lifecycle. Persisted Android read grants serve ISO
  descriptors or an extracted document tree without copying data to internal
  storage. The tree reader owns a bounded 64-descriptor cache; tests cover
  case-insensitive paths, eviction, partial reads, and descriptor destruction.
- Splashdown's Arcade-only navigation was four identical events: XInputOpen
  returned handles for disconnected ports, and the game reused the first
  controller's report for those ports. XInputOpen now rejects absent controllers.
  A native HLE smoke test verifies one controller, three absent ports, report
  bounds, and closed-handle behavior (10 checks).
- Flat wakes were missing point sprites. The 4531 render-state layout starts
  deferred states at 83 and complex states at 118; the renderer expected the
  later layout. State translation now handles API setters and inline writes,
  including point-sprite enable at old index 109 and scale at 110. An 11-check
  native smoke test verifies that translation against the actual guest fields.
- The generated shaders carry point size and expand point geometry at the
  requested size with texture-stage-3 sprite coordinates. Fixed and programmed
  size, clipping to the size limits, and 1x/2x rendering are regression-tested.
  A moving Hawaii race now has visible spray/wakes behind both sides of the ski;
  comparison captures are under `out/verification/android/`.
- A real moving race was instrumented for audio. The 44.1 kHz stereo music
  stream queues and consumes decoded song08.wma PCM; engine ADPCM voices vary
  pitch with speed, and water/ambient voices have nonzero gain and mixed output.
  The final mix is nonzero, and Android AudioFlinger reports the app's active,
  unmuted 48 kHz stereo output. `race-audio.wav` contains ten seconds of the
  native mixer. This establishes that those sources are present, not original
  Xbox audio fidelity: DSP/reverb and velocity Doppler remain incomplete.
- All 76 SDK tests pass with both DXC and the Android glslang adapter on
  Lavapipe. All six runtime smoke programs pass, including 28 audio checks.
  `patches/README.md` documents the seven reproducible SDK patches.
- The same launcher/lifecycle is built and installed for Burnout 3, Links 2004,
  NFS Hot Pursuit 2, PGR1, and BLACK. Existing data for the first three was
  verified file-by-file on SD before private asset copies were removed; saves
  and settings are retained. SFRush is excluded as requested. Device results
  and APK identities are in `out/verification/android/summary.json`.

## Menu presentation fix (2026-10-19)
- Root cause: the recompiled display reset (`sub_00163BD0`) replaces the device's surface pointers. Draws went to guest header `0x16E56C`, data `0x037BC000`, while `PresentFrame` continued presenting the original host header `0xB0000C60` and its unrelated allocations. Movie playback used a separate copy into the presented surface, hiding this mismatch during the intro.
- The runtime now follows the reset's surface-pointer table before presenting, getting a back buffer, or persisting the display. Initial device creation fills the front, back, and additional back-buffer pointers consistently.
- New proven layout key: `CDevice_BufferCount = 0x2078`. The reset at `0x163C39` stores `BackBufferCount + 1`; teardown at `0x163B4A` bounds its surface-pointer loop with that count. The table contains back, front, then additional back buffers.
- To isolate this port's changes, `sdk/` now links to `.local/sdk`, copied from `/home/jon/XboxXBE-Recomp` without its Git metadata. The shared SDK checkout was not modified. The source changes are preserved in `patches/4531-reset-presentation.patch`; for another SDK checkout, apply with `git -C <sdk-checkout> apply <absolute-path-to-patch>`, then rebuild/recompile.
- Verification: native `Splashdown` and the headless harness build. With `SPLASH_SKIP_MOVIES=1`, the reference renderer shows the main menu; a single `A 1` command loads `UI/char_der.res` and displays rider selection without either null guard enabled. Captures: `out/verification/main-menu.png` and `out/verification/rider-select.png`.
- SDK tests: all 73 cases now pass with the bundled Lavapipe software Vulkan driver (`VK_DRIVER_FILES=/opt/android-sdk/emulator/lib64/vulkan/lvp_icd.json`). Earlier default-driver runs had blocked all 22 render cases. An SDL desktop window remains unavailable inside the sandbox.

## Racer confirmation and course loading fixes (2026-10-19)
- The selected racer's voice line called the unmapped `CDirectSoundStream_GetStatus` at `0x17FAC7`, proven by the stream vtable at `0x1E7640 + 0xC`. The original function dereferenced an Xbox-only voice pointer inside a host stream, crashing in `sub_00186F16`. Mapping the host status query fixes the cause.
- Course audio next called unmapped per-voice distance, Doppler, rolloff, and mode setters. Their original bodies also expected Xbox voice objects. The public buffer and stream entry points now update host voice state. Rolloff affects attenuation; distance and Doppler factors are retained, but velocity-based pitch remains unimplemented.
- The next failure was `DrawIndexedVerticesUP` at `0x15F710`. Its guest copy loop consumed uninitialized private vertex-packing fields and read `0x3FA13C3E`. The host now expands the supplied 16-bit indices into the transient vertex stream. The obsolete diagnostic override at this address was removed.
- Recorded course geometry then reached `RunPushBuffer` at `0x162320`, whose original implementation dereferenced an absent GPU scheduling pointer. The host now executes recorded method packets, including indexed elements and array draws. Non-null fixup objects remain unsupported and return `E_NOTIMPL`; the verified Hawaii route does not use them.
- Missing intro video also exposed an ordering bug: a queued clear/draw could overwrite decoded movie pixels before presentation. Movie copies now flush older rendering work first. A threaded framebuffer regression passes with the flush and fails specifically at the movie-pixel check without it. Desktop intro video/audio still need verification.
- Focused tests (`tools/check_runtime.sh`): `tools/audio_smoke.cpp` passes 28 checks covering stream status, host 3D setters, invalid handles, and release. `tools/indexed_draw_smoke.cpp`, also run with `tools/threaded_d3d_smoke.cpp`, verifies indexed triangles, recorded indexed/array draws, and movie pixels against actual framebuffer values.
- Integration: the route runs for 90 seconds with `SPLASH_SKIP_MOVIES=1`, without either null guard, loading Hawaii and continuing after all reproduced crashes. Capture: `out/verification/hawaii-after-racer.png`. This is evidence of course rendering and continued execution, not proof of complete gameplay correctness.
- SDK changes are retained in `patches/4531-reset-presentation.patch` (graphics) and `patches/4531-audio-voices.patch` (audio), both checked against the original shared SDK. `run.sh` launches the rebuilt native executable with normal movie playback and verbose logging, clearing the three diagnostic workaround variables.

## Black race screen / gamma fade fix (2026-10-19)
- The race framebuffer contained course geometry and HUD, but the renderer received an all-zero gamma ramp at the end of loading. Applying that ramp makes every output RGB value black. The reference renderer ignores gamma, which is why its earlier race capture did not reveal the problem.
- The retail `GetGammaRamp` at `0x15E0B0` copies 768 bytes from `device + 0x2498 + slot * 0x300`, with the active slot read at `device + 0x2AA0`. The retail setter at `0x15E070` writes the same storage and at `0x15E09E` marks `device + 0x2A98 + slot * 4` pending. These prove `CDevice_GammaRamps = 0x2498` and `CDevice_GammaPending = 0x2A98`.
- Supplying those layout keys activates the runtime's existing identity initialization and setter mirroring. The game's original getter now saves the actual brightness curve, so loading fades restore it instead of restoring zero. Fades remain enabled.
- `tools/gamma_smoke.cpp` calls the original retail getter before the first setter, then saves a custom curve, fades to black, presents, and restores it. The old build fails at the initial identity check; the fixed build passes. It is included in `tools/check_runtime.sh`, alongside the passing audio and threaded drawing/movie checks.
- Vulkan integration reuses the native executable's renderer, raw vertex, and render-thread objects with a headless app/input host. Lavapipe renders the course; a diagnostic offscreen blit runs the actual Vulkan gamma shader and reads the result back. Before the fix, the last ramp sum was 0; after the fix it returns to 86,087, with all three ramp endpoints at 255. Capture: `out/verification/hawaii-vulkan-output.png`. This verifies the GPU rendering/gamma path without claiming desktop swapchain or complete gameplay correctness.
- Native build succeeds; all 71 SDK cases pass using Lavapipe. The layout change is included in `patches/4531-reset-presentation.patch`, checked against the shared SDK. Remaining visible issues include missing water/rider rendering.

## Missing rider and water fixes (2026-10-19)
- **Rider:** `LoadVertexShaderProgram` updated only the program used by the next declaration bind. The title legitimately caches its declaration and skips `SelectVertexShaderDirect` when just the program changes, so later draws kept using the previous vertex program. The host now replaces the active program immediately while retaining the declaration. Each queued draw keeps an immutable shader snapshot; program addresses are tracked so loading an inactive slot does not change the active one.
- **Generated water vertices:** the title renders wave data into colour/depth targets, then aliases that GPU memory as vertex buffers without a CPU lock. Vulkan's input assembler previously uploaded stale guest bytes. It now synchronizes targets used by the bound vertex inputs. Integer depth readback preserves Xbox D24S8 byte order (depth in bits 8–31, stencil in bits 0–7) and supports linear D16. Reads at higher render scales select the centre host sample for each guest texel.
- **Water depth:** the title sets `D3DZB_USEW` (`ZEnable = 2`) during water generation. Treating it as ordinary Z made reconstructed water vertices fall hundreds of units below the course. The generated pixel shader now writes interpolated clip W in the depth surface's range. Vulkan compilation already requests Direct3D position-W semantics, so the depth is `i.pos.w * surf.z`. The [xemu NV2A depth implementation](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/glsl/psh.c#L958) independently documents the W-depth behavior; the port's implementation is tested against explicit framebuffer expectations.
- **Verification:** `tools/shader_reload_smoke.cpp` draws with two programs under one declaration and checks framebuffer colours, queued-draw immutability, and inactive-slot loading. The original runtime fails at the program-replacement check; the fixed runtime passes. `tools/check_runtime.sh` includes it and creates a fresh threaded-runtime wrapper to avoid the harness reusing an object that included an older `.cpp` file.
- Two new Vulkan SDK cases verify W-depth ordering and colour/depth target aliases as vertex inputs, at 1x and 2x render scales, including D24S8 stencil bytes and D16. All 73 SDK cases pass with Lavapipe; audio, gamma, and threaded drawing/movie smoke checks also pass.
- Integration uses the native render thread, raw vertex path, Vulkan shaders, and GPU gamma pass. The capture `out/verification/hawaii-rider-water-vulkan.png` shows the rider and water together; `hawaii-rider-water-later.png` shows the later race state. Neither the diagnostic depth/blend override nor the prototype alias/visibility overrides is enabled. This establishes the headless Vulkan path; desktop swapchain output still needs a fresh user run.
- Active-program changes are preserved in `patches/4531-reset-presentation.patch`; Vulkan, shader, and renderer-test changes are in `patches/4531-race-rendering.patch`. Both apply alongside `patches/4531-audio-voices.patch` to the original shared SDK. The alias synchronization uses GPU readbacks; performance optimization and floating-depth/swizzled/offset vertex aliases remain future work.

## Race-preview map, text, and video composition (2026-10-19)
- The game's `BinkCopyToBuffer` destination is a texture. The former hook also sent every decoded frame to the display-copy fallback. For the 288x288 Hawaii preview this put raw video in the top-left corner; marking the display dirty then uploaded stale CPU framebuffer pixels over the GPU-rendered map and text.
- A Vulkan capture before the display copy contains the complete Arcade interface, including the map, event/course/difficulty/lap text, button labels, and the masked circular video panel. The displayed capture from that same run loses this interface. This isolates the problem to the movie workaround rather than missing texture files or sound.
- `src/game/hooks.cpp` now compares the movie dimensions with the current render target. Only full-screen, zero-offset movies use the display-copy fallback. Embedded previews use the title's existing texture draw and mask. Opening movies still require the fallback; removing it entirely was tested and regressed their picture.
- Hawaii.bik contains only a 288x288 video stream (965 frames, 32.198 seconds), with no audio stream. The game supplies the menu audio separately. Headless mixer captures contain nonzero stereo audio during both Intro.bik and the course preview. These establish mixer output, not desktop speaker routing.
- An 85-second native Vulkan integration run with normal movie playback shows the corrected interface (`out/verification/course-preview-fixed.png`) and then enters the race with rider and water visible (`preview-to-race-vulkan.png`). `course-preview-before.png` records the former failure. Mixer metrics for intro, preview, and race are in `course-preview-audio.json`; the route log and executable hash are retained in `summary.json`. The native executable has been rebuilt; the existing audio, gamma, threaded drawing/movie, and shader reload smoke checks pass.

## Layout (xdk/4531/layout.toml in the SDK, partial)
Proven from this XBE's code, with the instruction addresses in the file:

| Key | Value | Evidence |
|---|---|---|
| CDevice_Push / PushThreshold | 0x0 / 0x4 | SetTexture 0x15F042 |
| CDevice_Flags | 0x8 | SetPixelShader 0x160CC4, Swap 0x161DC8 |
| CDevice_Time | 0x30 | SetTexture 0x15F013 |
| CDevice_Textures | 0xA78 | SetTexture 0x15F000, 0x15F039 |
| CDevice_Transforms | 0x750 | SetTransform 0x15E140 |
| CDevice_RenderTarget / Depth / BackBuffer | 0x2070 / 0x2074 / 0x207C | SetRenderTarget 0x15E550 |
| CDevice_BufferCount | 0x2078 | frame buffer reset 0x163C39; teardown loop 0x163B4A |
| CDevice_FrontBuffer | 0x2080 | GetViewportOffsetAndScale 0x15EEA8 (BackBuffer + 4, as in 5849) |
| CDevice_SwapCount | 0x2AD8 | Swap 0x161DD9, 0x161DF6, 0x161E61 |
| CDevice_GammaRamps / GammaPending | 0x2498 / 0x2A98 | GetGammaRamp 0x15E0C7; SetGammaRamp 0x15E070, 0x15E09E |
| CDevice_PixelShader / VertexShader | 0x370 / 0x380 | SetPixelShader 0x160B34, SetVertexShader 0x160388 |
| CDevice_Viewport | 0x9D0 | SetViewport 0x15EFA3 to 0x15EFB5 |
| CDevice_ScreenSpaceOffset | 0x9E8 | SetScreenSpaceOffset 0x15F4B3, 0x15F4C3 |
| CDevice_VBlankEvent | 0x2448 | BlockUntilVerticalBlank 0x15E2EB |
| CDevice_VBlankCallback | 0x2444 | SetVerticalBlankCallback 0x15E2C0 |
| CDevice_VBlankCount | 0x2474 | read 0x161501, conditional increment 0x161515 |
| DirtyFlags_Texture | 0x4800 | SetTexture 0x15F06F |
| CDevice_BaseVertex | 0x1C | DrawIndexedVertices 0x15F8FE |
| CDevice_Palettes | 0xA88 | SetPalette 0x15F17D, 0x15F1B5 |
| CDevice_Size | 0x2B00 | Direct3D_CreateDevice 0x162536 (0xAC0 dwords) |
| TextureState_Stage | 32 | SetTextureStageStateNotInline 0x15D63B |
| Stream_Size / Stride / Buffer | 12 / 0 / 8 | SetStreamSource 0x15FF2D, 0x15FF66, 0x15FF78 |
| CDevice_ZScale / SuperSampleX / SuperSampleY | 0x450 / 0x458 / 0x45C | GetViewportOffsetAndScale 0x15CD05, 0x15CC5E, 0x15CC80 |
| CDevice_IndexBuffer | 0x38C | device teardown 0x1646BE releases it; the store is not located yet |
| CDevice_AutoDepth | 0x2088 | frame buffer reset 0x163B69 releases and clears it; the store is not located yet |

Layout has 43 keys, including the buffer count, XPP_Current/Changed/Previous 0x0/0x4/0x8, EffectsImage_Skip0/Skip1/Desc 0x804/0x80C/0x818, CDirectSoundStream_Vtable 0x0 and Vtable2 0x4; `xbe_recomp` accepts it and exits 0.

Config `[data]` (hand-set, kept by xbsdb_import.py because each has a comment): D3D_Device 0x16C4E0 (push pointer read by SetRenderState_Simple 0x15CBB0), D3D_DirtyFlags 0x16BD78, D3D_SimpleRenderStateEncode 0x1D3E50 (table indexed by SetRenderStateNotInline 0x15CBE0).

Still unknown: CDevice_BackBufferScale (the 0x468/0x46C pair is saved and restored around a call, not proven), DisplayFlags, Field; remaining DSOUND/XPP keys and some D3D symbols. `PixelShader_Definition = 0x8` is set and handled by the runtime.

## GitHub and title searches
- No public 4531 signature or layout data exists in the repos checked. Searched upstream TGP482, the Maslin0 fork, and other Xbox recomp projects, and searched by title for the 4531 games in the Dxbx dumps (Silent Hill 2, Morrowind, Enclave, Hitman 2, Crazy Taxi 3, Conflict: Desert Storm). Only PC mods came back.
- Other local tools checked: `RallisportChallenge2-decomp/xboxrecomp` (no XDK tables), `MANXFramework` (no layout data). No 4531 data in either.

## Historical initial boot investigation (superseded by current verification below)
- Harness built from `XboxXBE-Recomp/harness` with `-x '/gpu/vk_|/gpu/vulkan|/audio/sdl_audio|/core/keyboard_pad'` (those need DXC, ImGui, VMA and SDL headers the harness doesn't provide). The game repo's `sdk/` is a local symlink to `XboxXBE-Recomp` and is in `.gitignore`.
- **Boots to the Bink movie, then hangs (see the correction below).** Kernel imports resolved; XAPI controller ports; the title reads its files; the device is created (640x480, 2 buffers); the effects image loads; the DirectSound stream vtables resolve; the Atari logo Bink movie (`d:\AppStart\NTSC\Atari.bik`) starts.
- **Now draws (experiment):** `CDevice_VBlankCount = 0x2AE0` is an EXPERIMENT, not proven. No code in the XBE references 0x2AE0, so the runtime's vblank thread can run and call the game's callback at 0x133300, which increments the global the wait loop at 0x133310 is waiting for. With it, the game runs for 90 s: 1,046,280 frames, 972,932 draws, 73,348 empty frames. The real vblank counter field is still unknown and must be found before this is final.
- **Draw diagnosis (black screen):** the draw lists show a 640x448 textured quad (`tex0=00475710`, fmt 12, RT `0x037BC000`), fixed function (vs=0), with vertices (0,480,0.5), (0,448) and a white diffuse. The world matrix was all zeros, so every draw collapsed. D3D starts transforms as the identity, and the title's default-state init does not run here. `D3DDevice_SetTransform` takes XDK numbers (view 0, projection 1, texture 2-5, world 6-9). Fixed in `runtime/xdk/d3d8.cpp` `CreateDevice`, which now sets all ten to identity. The world is identity in the next draw list. The quad still does not appear: with the submitted view (translation -384,-256,1056) and projection, its vertices fall outside the clip volume, and the reference frames are still black. Next: check the vertex stream and the view/projection the title intends, and whether this quad is the Bink movie backdrop.
- **Stall is in movie playback (most likely):** every captured frame has the same single draw. The title reaches its intro movie `d:\UI\NTSC\Videos\Intro.bik` at about 20 s, after which only the movie's threads run. A scripted pad test (START and A presses at frames 300 to 2400) was delivered, but the draw list is unchanged. The 640x448 textured quad is the movie surface. The Bink entry points (`BinkDoFrame`, `BinkNextFrame`, `BinkWait`, `BinkGetSummary`) are zero in `config/game.toml`, because XbSymbolDatabase has no Bink signatures, so the runtime's Bink hooks in `runtime/xdk/bink.cpp` are inactive. Next: identify the Bink functions in the BINK section, or find what the movie loop waits for.
- **GitHub search for Bink (done):** no Xbox Bink signatures or Xbox-specific Bink code found. Results are PC Bink headers and wrappers (masseffect-binkw32, Far Cry SDK, Jedi Outcast, spidey-decomp), not usable for the 4531 BINK section. No Xbox recomp project or issue reports a stall on a Bink movie. All seven local ports (BLACK, Burnout3, SFRush, NFSHP2, Links2004, PGR1, Splashdown) leave `BinkDoFrame`, `BinkNextFrame`, `BinkWait`, `BinkGetSummary` at 0, so the Bink hooks are not what the other ports rely on.
- **Audio check:** the DirectSound stream functions are found for this build (for example `CDirectSoundStream_Process` 0x17FB18), and the movie's threads are waiting in `NtWaitForSingleObjectEx`, not in an unresolved call. Next: instrument the wait handles on the movie threads (`guest 38`, `guest 3C`) to see which event they wait for, and what should signal it.
- **Movie wait objects (instrumented, now removed from the SDK):** `NtWaitForSingleObjectEx` on the movie threads waits on unsignalled events `0x1040` (13 samples) and `0x10CC` (8 samples), with timeouts, so they poll. Mutants `0x110C`, `0x111C`, `0x106C` come and go between free and owned, so they are not the blocker. In the runtime, DirectSound signals a packet's event only in `Complete` (runtime/xdk/dsound.cpp), when the mixer consumes it. So the movie is most likely waiting on audio packets that never complete. Bink syncs to the audio position (runtime/xdk/bink.cpp), so a stalled audio position would stall the movie. Next: check whether the movie's DirectSound stream is started and its packets are queued, and whether the null audio stand-in (harness/null_audio.cpp) advances the play position.
- **Audio findings (instrumented, debug code removed):** the movie's DirectSound stream exists (a stream voice is created) but never becomes active: no `CDirectSoundStream_Process` call ever arrives, so no packets are queued and nothing is mixed. The mixer and null audio are running (other voices play). The movie is stalled before it submits audio.
- **Movie worker thread:** the movie threads start at `0x1BAA50`, a state machine that calls `0x139253` (a timed wait, which calls `0x13913D`) and `0x13911D` (`KeSetEvent`). It waits on one event handle and signals another. The worker stays in its wait because the event it needs is never set, so it never reaches `Process`. Next: find which thread should set the worker's wait event (the handle is in the object at `[esi+0x4]` or `[esi+0x8]`), and why that thread isn't signalling it.
- **Movie worker structures (read from guest memory at runtime):** worker 1 at guest `0x5F9B00` and worker 2 at `0x5F9B80`. Each is in state 1 (idle) with a timeout (`0x32` ms and `0xC8` ms) and two wait handles plus one signal handle: worker 1 waits on `0x1108`/`0x1100`, signals `0x1104`; worker 2 waits on `0x1118`/`0x1110`, signals `0x1114`. The waited-on request events `0x1108` and `0x1118` are unsignalled. So the workers are waiting for a request that the game's movie code (probably the main thread, through the Bink calls) never makes. Next: find the main-thread code that sets those request events, and why it doesn't run that path (the main thread is polling in `sub_00046250`, a frame-timing routine, and `sub_00091D70`).
- Missing files, not present in the disc RAR either: `D:\ui\ntsc\buttons.dxm`, `careerbox.dxm`, `jewelcolor.dxm`, `d:\UI\NTSC\Music.dxt`, `D:\fonts\info_a.dxm`, `info_c.dxm`, `title_a.dxm`, `title_b.dxm`.
- **Correction (earlier):** an earlier note said frames were presented. The log's frame count stops at about 1.57 s. A later 120 s run under gdb shows the game hung there, not presenting. Main thread "guest 14" spins in `sub_00133310`, a busy-wait that loops until the global at `0x2B4534` reaches its argument. Nothing in the game code increments that global, and it is not a kernel import slot (KeTickCount's slot is 0x1D3B94). The worker threads are idle in `NtWaitForSingleObjectEx`.
- **Likely cause:** the runtime's vertical-blank thread (runtime/xdk/d3d8.cpp `VerticalBlank`) only starts when `VBlankCount`, `VBlankEvent` and `VBlankCallback` are all known (`kVBlank`). `VBlankCallback` is now known (0x2444, from `SetVerticalBlankCallback` 0x15E2C0 and XbSymbolDatabase), but `VBlankCount` is not, so the thread still does not start and the game's wait never completes. Next: find the 4531 vblank count field, then check the wait at 0x13380D.
- Missing symbols that matter for drawing: `D3DDevice_BeginPush`/`EndPush`, `D3DDevice_Present`, `GetBackBuffer2`, `BlockOnFence`, `InitDefaultState` (the log says default states were not applied).
- **Fixed this session:** the first device reset crashed because `NewSurface` (runtime/xdk/d3d8.cpp) gave host surface headers one reference, and the reset releases the render target, depth, back buffer and front buffer slots, so the header reached zero and was freed through the guest heap (RtlFreeHeap 0x13C8AB expects an NT heap block; the crash was at 0x13AF99). The header now holds 4 references (`0x000D0004`), so the reset no longer frees it. This is a shared SDK change: SFRush builds from this SDK, BLACK has its own copy. The SFRush boot still needs checking.
- Config `[data]`: DSound_StreamVtable 0x1E7640 and DSound_StreamVtable2 0x1E7634, from the constructor at 0x181700.

## Current runtime verification
- Native Linux build succeeds. The user confirms native menus and rider selection work. Headless Vulkan rendering and the output gamma pass now produce a visible Hawaii course, rider, and water after the loading fade; the rebuilt desktop executable still needs a fresh user run.
- Xemu 0.8.136 is configured with the MCPX boot ROM, Flash ROM, HDD image, and `dvd_path=/home/jon/Downloads/Splashdown.iso` from the local emulator-files ZIP/settings. Launch parameters include the HDD and ISO; the VM ran for 195 seconds and the monitor reported it running. The user confirms the same game runs fully in Xemu; this session could not capture the visible output because Xemu's monitor lacks `screendump`.
- `src/game/hooks.cpp` forwards only full-screen decoded Bink frames to `d3d::SetMovieFrame`; embedded previews remain in the title's texture composition. `SPLASH_SKIP_MOVIES=1` is a test-only fast-forward; normal intro playback is 158.86 seconds.
- The brief-tap route loads the rider, opens `D:\UI\NTSC\Videos\Tracks\Hawaii.bik` (32.2 seconds), then loads `GamStart/load_e.res`, `D:\Scenes\Hawaii\HawaiiE1.res`/`HawaiiE2.res`, rider resources, and in-game audio. Earlier runs stopped at audio and draw-helper faults. The fixes above now reach visible course rendering, despite many missing-path probes still being logged.
- The optional `XBE_DISC_IMAGE` path and a matching direct-disc headless mode mount the ISO as D:. Earlier direct-disc runs reported the same Hawaii requests and `sub_0015F710` fault as extracted-folder runs. Those results ruled out the mount choice as the cause of that crash.
- The historical null faults in `sub_00186F16` and `sub_001804E8` came from unmapped audio APIs acting on host objects. Their caller mappings and host implementations now cover the verified route; neither diagnostic guard is enabled for current verification.
- All three downloaded ISOs were parsed with the Xbox disc reader. Each has 1,361 entries and exactly the same paths/sizes as `assets/`; `Main.res` and `HawaiiE1.res` hashes match. The first two XBE hashes match the extracted XBE; the third XBE differs by four header bytes but recompiles to the same 9,558 functions/596,285 instructions. None has standalone `.dxt` entries, yet the user confirms the same game runs fully in Xemu. Therefore the missing-path logs are not proof of an incomplete image; investigate Recomp's file/resource resolution and rendering before requesting more assets.
- Splashdown's loaded vertex program and direct vertex-shader selection are handled (`vs=16C2A1`, `ps=16C90C`); transforms initialize to identity. The formerly uniform gray reference frame was the stale presentation surface. Reference captures now show the menu and rider selection, with remaining rendering artifacts.
- The proven 4531 `CDevice_VBlankCount` is `0x2474` (not the old experimental `0x2AE0`).

## Open issues and next steps
Android build/use instructions are in `android/README.md`. The signed ARM64 APK
is `android/build/Splashdown-arm64-debug.apk`; it uses physical controllers,
direct SD ISO/folder access, remembered boot, and fresh native processes on
reopen. Current checks are in `out/verification/android/summary.json`.

1. Verify the rebuilt native race fade, controls, and intro video/audio on the desktop using `run.sh`.
2. Confirm the installed particle-spray change during handheld gameplay and compare remaining water/animation details with Xemu; profile render-target vertex readbacks on the desktop GPU.
3. Recorded push-buffer fixups, velocity Doppler, and Splashdown DSP/reverb fidelity remain incomplete.
4. Continue identifying XDK symbols/layout entries. The original shared SDK checkout has extensive pre-existing modifications; do not reset or clean it. This port uses the isolated `.local/sdk` copy.
