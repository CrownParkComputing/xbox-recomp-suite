# Grabbed by the Ghoulies — port status

Gate: **builds, boots, presents and renders the Rare logo intro**. The 2026-11-11 compressed-ramp
fix also restores the smoke effect, coloured book cover and early offscreen pass; see
[Compressed ramp textures fixed](#compressed-ramp-textures-fixed-2026-11-11).
With L.A. Rush's XACT voice
bridges applied, the title runs for at least 16 s with no guest trap or crash, and frame captures
from ~4 s onward show real 640x480 output (the Rare logo, the cabinet and the parchment on the
floor). Proof frames: `/home/jon/Pictures/Ghoulies-working/`. See
[Working frames](#working-frames-2026-11-10).

Work carried over from getting Panzer Dragoon Orta running on 2026-11-10 is applied here: the
missing `D3D_InitDefaultState` symbol (the Orta black-screen cause), a guard for a temporary
EndPush diagnostic left in the shared SDK, and a stream-creation bridge. The remaining XACT
bring-up is L.A. Rush's proven `overlays/larush-sdk.patch` plus nine game-specific hooks. See
[Orta-derived fixes](#orta-derived-fixes-2026-11-10),
[XACT bring-up](#xactdsound-bring-up-2026-11-10-continued) and
[Working frames](#working-frames-2026-11-10). Audio has not been checked by ear, and gameplay
past the intro is unvalidated; see [Current blockers](#current-blockers).

## Identity

- Title: Grabbed by the Ghoulies, title ID `0x4D530053`, region 1 (North America), ratings 4
- Executable: `assets/default.xbe`, SHA-256 `a2a5bbe51ecc11392612bad30ee4238b796d4d0ed7262e7d23dc4120388294b1`
  (XBOX-ZTM rip, extracted files, no ISO)
- Second image: `assets/cdx.xbe` — "CDX", the disc's bonus-video player (XSNDTRK, XMV, CDX
  libraries), SHA-256 `a5b347555d4778c5e01fea39f7b1bdbac838a79655f72e109fddfb749343b621`.
  Not recompiled; it needs its own `add_xbe_image` entry if the title ever launches it.
- XDK build: **5659** (all libraries 1.0.5659)
- Entry point `0x0014FAE9`, kernel thunk `0x00202EA0`
- Certificate: AllowedMedia `0x00000002` (DVD only), init flags `0x80000005` (mount utility
  drive, limit devkit memory). The DVD-only certificate means the XAPI runs the media check at
  startup — see the Panzer Dragoon Orta entry in the porting cookbook; the runtime answers it.
- Libraries present as sections: D3D8, D3DX8, XGRAPHC, **BINK** (RAD Bink video, 16 sections),
  XACTENG, DSOUND, WMADEC, XPP, DOLBY

## Symbols / layout

The SDK has no 5659 signature set. Addresses come from XbSymbolDatabase
(`XbSymbolDatabaseCLI` on this XBE: 396 matches — D3D8 160, DSOUND 159, XAPILIB 55,
XACTENG 18, XGRAPHC 4) imported by `sdk/tools/symbols/xbsdb_import.py`: **138 of 257**
names filled. Hand additions, each proven in this image (comments keep them through
re-imports):

- `[data]`: `D3D_Device=0x181E10`, `D3D_DirtyFlags=0x181968`,
  `D3D_SimpleRenderStateEncode=0x20A368`, `DSound_StreamVtable=0x20A038`,
  `DSound_StreamVtable2=0x20A028`
- `sdk/xdk/5659/layout.toml` — new file, partial. Every value is proven from this image's
  code; the CDevice layout is the 5849 one with the `0x9xx` block 4 bytes lower and
  everything from `0xC50` on `0x10` lower, and the miniport sits at device `+0x1C18`.
  `CDevice_IndexBuffer` is left out because the image has no `D3DDevice_SetIndices` (the
  linker dropped it), and `CDevice_Field` is unproven; both trap where first needed.

## Code generation

`python3 tools/build_linux.py Ghoulies --generate-only` succeeds. 48 partitions, 80 MB of
generated C++. Code sections: `.text`, `D3D`, `D3DX`, `XGRPH`, the sixteen `BINK*` sections,
`XACTENG`, `DSOUND`, `WMADEC`, `XPP`. `DOLBY` and `BINKDATA` are left out as data, as in the
other ports; revisit if the title calls into them.

Legacy-instruction "unimplemented" counts are the usual junk decodes in data-looking regions
(`aas` 135, `iretd` 35, `into` 28, …). The Bink sections are real code: do not dismiss their
counts the way Panzer Dragoon Orta's Sofdec sections were.

## Bring-up fixes (in order hit)

1. **`CDevice_IndexBuffer` was required to create the device** (shared SDK fix). This image has no
   `D3DDevice_SetIndices` at all - the linker dropped it, XbSymbolDatabase finds none, and nothing
   in the D3D section ever stores the field - so the key cannot be proven, and
   `Direct3D_CreateDevice`'s `XDK_REQUIRE` refused to create the device over a key nothing reads.
   The requirement now sits in `D3DDevice_SetIndices`, where a title that does use index buffers
   still traps.
2. **A crash with no guest stack** (shared SDK fix). The crash handler covered SIGABRT, SIGFPE,
   SIGILL and SIGBUS but not SIGSEGV, which is the common bring-up fault; it now covers SIGSEGV
   too and still re-raises for the core dump.
3. **The APU sample counter** (shared SDK fix). XACT's clock reads the APU register at
   `0xFE80200C` directly (`0x1BD517`) and converts it with 48000 samples and 10000 units of
   100 ns. Nothing backed that address, so the read faulted. The kernel now commits that page and
   keeps the counter moving on the pause-aware game clock.
4. **`D3D_FvfShader` was missing** - the device's current vertex shader object stayed null and the
   first push-buffer state emit (`sub_00173900`, which does `test byte [eax+4],2` on it) faulted.
   The device init at `0x17AB7B` stores `0x184478` there; added to `[data]`. `CDevice_VertexShaderHandle`
   (device `+0x794`, stored beside the object at `0x173722`) went into the layout at the same time.

## Orta-derived fixes (2026-11-10)

5. **`D3D_InitDefaultState` was 0** (the Orta black-screen cause). Orta's config maps it to
   `0x2B3790`, called from its `Direct3D_CreateDevice` at `0x2B4386`. Ghoulies' device init at
   `0x17AB7B` has the identical sequence and calls `0x179FE0` at `0x17ABBC`. The generated body at
   `0x179FE0` is the same function as Orta's `0x2B3790`: a loop of ten identity transforms, a loop
   over the default render states (skipping `0x9A`, `0x7E`, `0x7F`), state `0x8F` from the
   auto-depth stencil, four texture stages x 32 texture states, then the closing stage calls.
   Added `D3D_InitDefaultState = 0x179FE0` to `[symbols]`. The shared HLE calls it from
   `ApplyDefaultStates` inside `Direct3D_CreateDevice`; it is in `x86_func_mapping.cpp` already, so
   no `[functions]` entry is needed.
6. **A temporary Orta diagnostic crashed every first EndPush** (shared SDK fix). The
   `LOG_FIRST(6, "d3d: TEMP EndPush ...")` added during Orta bring-up read `R32(g_push_begin)` and
   the next two words even when `g_push_begin` was 0. This image has no `D3DDevice_BeginPush`
   (`0` in the config), so the head stays 0 and the read faulted on the first `D3DDevice_EndPush`
   (`0x177270`), with a GDB backtrace at `sub_00177270` + 84. The log is now guarded by
   `if (g_push_begin)`; ports that do open a push still get the diagnostic unchanged. Panzer Dragoon
   Orta was rebuilt as a compatibility check after the shared change.

### Verification

Rebuilt with `python3 tools/build_linux.py Ghoulies --jobs 8`, then launched with `XBE_PERF=1` and
`XBE_CAPTURE_DIR` set, with F3 supplied through a python-evdev uinput keyboard (ydotool does not
reach this Wayland compositor). Results:

- `image_symbols.h` now has `SYM_D3D_InitDefaultState 00179FE0` and `HAS_D3D_InitDefaultState 1`;
  the boot log no longer says "the image has no D3D_InitDefaultState, default states were not
  applied".
- Frame capture `/tmp/opencode/ghoulies-fix-capture/205237/frame.txt`: 138 draws, all with
  `cw F` (RGBA colour writes enabled). The old build recorded 3 draws per frame and `cw 0`.
- The run now reaches 4.6 s instead of dying at 0.7-0.9 s on the EndPush read, and ends in the
  documented XACT/DSOUND voice chain (`0x1D0B2C` -> `0x1C3988` -> `0x1C4424` -> `0x1C73DB` ->
  `0x1C6E12` -> `0x1C5F95`).
- Every read-back in that capture (`final.bmp`, the `*_83D98000.bmp` and `*_83991000.bmp`
  render-target dumps) is still entirely black, and the recorded vertex-shader constants are all
  zero. This is the same "nothing visible yet" gate as before: the title has not reached its
  attract sequence because the audio crash stops it first. Do not claim a visible frame until that
  crash is fixed.

## XACT/DSOUND bring-up (2026-11-10, continued)

Each step below was applied and validated by a fresh build and run; the crash site moved each
time. XACT is guest middleware that walks the Xbox APU voice structures directly, so every
unmapped DSOUND method it calls runs guest hardware code on the host mixer's opaque objects.
The XTLID table named the exports; the generated guest code proved the addresses by comparing
bodies with Orta's XDK 4928 image.

7. **`IDirectSoundBuffer_Pause` was missing.** `0x1C73DB` is instruction-for-instruction Orta's
   `0x2C25BE` (the COM thunk subtracting `0x1C` from the buffer). Mapped in `[symbols]`; the
   shared mixer's `BUFFER(Pause, 2)` now handles it. The guest `0x1C5F95` pause-flag writer no
   longer runs for that path.
8. **`IDirectSound_CreateSoundStream` had no host bridge** (shared SDK change). XTLID `0xB003B`
   -> `0x1C884C` is the COM method XACT uses to create streams; its guest implementation walks
   the host DS object with Xbox COM vtables. The shared SDK already HLEs the analogous
   `IDirectSound_CreateSoundBuffer(this, desc, out, outer)`, so `DirectSoundCreateStream` was
   refactored into `NewStream(desc, out)` and a matching
   `HLE_STDCALL(IDirectSound_CreateSoundStream, 4)` added, with an `#ifndef` placeholder for
   configs that do not know the name. Ghoulies maps `IDirectSound_CreateSoundStream = 0x1C884C`.
9. **Game-specific bring-up hooks** in `ports/Ghoulies/src/game/hooks.cpp`, following Orta's
   `src/game/audio.cpp` (`AUDIO_NOOP`) pattern: `0x1C6B0F` (aggregated-interface AddRef on the
   host DS object), `0x1C7731`, `0x1C5F95`, `0x1C5D92` and the `0x1C5DAB..0x1C5E64` family of
   two-argument voice-property writers. Each reads `[this+0x10]` or `[this+0x0C]` and faults on
   an opaque host stream; accepting them at the API boundary keeps XACT moving. These are bring-up
   accommodations, not a model of the APU: revisit them when the real bridges land.
10. **Guest CPU serialization.** `src/common/game_info.cpp` now sets
    `.init = kernel::SerializeGuestCpu`, as Orta does for CRI. XACT assumes one Xbox CPU (see the
    L.A. Rush IRQL entry in the cookbook); before this the crash site varied run to run, after it
    the failure is reproducible.
11. **Light-HRTF math callback table.** `DirectSoundUseFullHRTF` is HLE'd to a no-op in the shared
    SDK, but its guest body at `0x1C5AD2` calls `0x1C9576`, which fills the callback table at
    `0x1E3B64..0x1E3B8C` that XACT calls through (`call [0x1E3B64]` at `0x1C7EAA`). Leaving the
    config symbol at 0 lets that initializer run, exactly as the L.A. Rush overlay does for
    `DirectSoundUseLightHRTF`. With the HLE mapped, XACT trapped on `indirect call to 00000000`.

### State after these steps

- Regression checks: `python3 tools/test_runtime.py` (10/10), `sdk/tools/symbols/test_xbsdb_import.py`
  (10/10), `ctest` for `game_tests` (passed), and Panzer Dragoon Orta and L.A. Rush rebuilt
  against every shared SDK change.
- Frame captures still show 138 draws with `cw F` but entirely black read-backs; the recorded
  vertex-shader constants are all zero. The title fails inside guest HRTF math / XACT code
  (`0x1CEA47` from XACTENG `0x1BB99C`). Reaching the attract sequence needs the real L.A. Rush
  voice bridges, not just the no-op hooks above.

## Working frames (2026-11-10)

L.A. Rush is the working XACT reference, so Ghoulies now uses its SDK overlay:
`tools/prepare.py` maps `Ghoulies` to `overlays/larush-sdk.patch`, which supplies the
`dsound::Use3DVoiceData`, `Set3DVoiceData`, `GetVoiceProperties`, `SetPitch` and `SetOutputBuffer`
bridges. XTLID export ids are stable across XDK builds, so L.A. Rush's working hook addresses
(`ports/LARush/src/game/hooks.cpp`) map to these XDK 5659 addresses in Ghoulies:

| L.A. Rush hook | id | Ghoulies address | bridge |
| --- | --- | --- | --- |
| `0x268401` GetVoiceProperties | `0xB0055` | `0x1C73A3` | `GetVoiceProperties` |
| `0x26841D` Set3DVoiceData | internal, +0x1C | `0x1C73BF` | `Set3DVoiceData` |
| `0x268439` Use3DVoiceData | internal, +0x1C | `0x1C73DB` | `Use3DVoiceData` |
| `0x26845A` SetPitch | `0xB0099` | `0x1C73FC` | `SetPitch` |
| `0x268495` GetVoiceProperties (stream) | `0xB0081` | `0x1C7437` | `GetVoiceProperties` |
| `0x26849A` Set3DVoiceData (stream) | internal, +5 | `0x1C743C` | `Set3DVoiceData` |
| `0x26849F` Use3DVoiceData (stream) | internal, +5 | `0x1C7441` | `Use3DVoiceData` |
| `0x2682A1` SetPitch (stream) | `0xB0076` | `0x1C71F3` | `SetPitch` |
| `0x268C68` SetOutputBuffer | `0xB0098` | `0x1C7A96` | `SetOutputBuffer` |

`IDirectSound_CreateSoundStream = 0x1C884C` is already handled by the shared SDK HLE (step 8).
`IDirectSoundBuffer_Pause` was set back to 0: `0x1C73DB` is `Use3DVoiceData`, not Pause — the
earlier identification by body shape was wrong, since every COM buffer thunk subtracts `0x1C`.
The last blocker was `sub_001C6674` (XACT's stream packet processing entering APU hardware code
at `0x1CA7AD`); accepting it at the boundary in `src/game/hooks.cpp` removed the final crash.

Validation: a clean run stayed alive at 4 s, 9 s, 14 s and 16 s with no `signal 11`, no guest
trap and no `indirect call` error (`/tmp/opencode/ghoulies-final-run.log`); an earlier run reached
27 s and exited 0. Capture `214244/final.bmp` has mean luminance 34/255 (max 190) and shows the
Rare logo intro. Proof frames copied to `/home/jon/Pictures/Ghoulies-working/`:

- `01-Rare-logo-intro.png` - skeleton hand, cabinet, Rare logo
- `02-cabinet-with-Rare-logo.png` - the drawer opening
- `03-map-and-floor.png` - parchment and camera pan
- `04-sustained-run-14s.png` - frame from the clean 16 s run

### Incomplete rendering (observed 2026-11-11)

Real desktop screenshots (`grim`, 5120x1440 borderless) are less complete than the internal
640x480 captures: 5 s is entirely black, and 9 s / 12 s show a large pure-black quad plus a black
silhouette over the cabinet and Rare logo. A gamma lift of `01-Rare-logo-intro.png` confirms those
regions are pure black with no hidden geometry. Screenshots:
`/home/jon/Pictures/Ghoulies-working/05-desktop-9s-incomplete.png` and `06-desktop-12s-incomplete.png`.
So the title renders real geometry but not the whole scene; do not call the visuals complete.

Evidence and the fix applied:

- **Root cause of the dropped geometry: `D3DDevice_BeginPush` was 0.** The HLE `EndPush`
  (`0x177270`) only calls `Execute(g_push_begin, end, true)` when the HLE `BeginPush` has set
  `g_push_begin`; with the symbol unmapped it stayed 0 and every inline push command was silently
  discarded. `0x177250` is the guest `BeginPush` (its call sites are paired with `0x177270`
  throughout the generated code). Mapping `D3DDevice_BeginPush = 0x177250` made the scratch
  buffer path run: recorded draws per frame rose from ~138 to 173-323, and the desktop frame at
  9 s went from a black quad to the full cabinet/candle/green-hand scene (lit pixels ~16% ->
  ~28%). Proof: `07-after-BeginPush-desktop.png`, `09-after-BeginPush-final.png`,
  `10-12-final-*.png` in `/home/jon/Pictures/Ghoulies-working/`.
- A scan with the 5849 signature set on this XBE agrees with the config on all 88 overlapping
  symbols. Its extra finds are now mapped: `D3D_InternalAddRef = 0x175970`,
  `D3D_InternalRelease = 0x175B90`, `CDirectSoundStream_GetStatus = 0x1C6674` and
  `CDirectSoundStream_SetMixBinVolumes = 0x1C704A` (replacing the `0x1C6674` no-op hook). The
  scan's `D3DDevice_BeginPush` signature is empty, which is why it missed a function that is
  present — do not treat a scan zero as proof of absence when the signature is `{}`.
- The Bink symbols were identified from the generated code (`BinkDoFrame = 0x1A7D40` called from
  `0x107469`, `BinkWait = 0x1A7870`, `BinkNextFrame = 0x1A8350`) and mapped as an experiment;
  `bink.cpp`'s wrappers are timing-only and did not improve the frames, so they were reverted to
  0. The guest Bink decoder still runs natively.
- At this stage, still missing (resolved below): `D:\movie\0de65e94` is a real Bink file
  (`BIKi`) read at ~7.6 s, and a black book-cover/rectangle covers the scene at some timestamps. Render target `0x83991000` (256x256)
  is cleared every frame, drawn into in only some frames (8 draws in some captures, 0 in others),
  then composited full-screen via `t0 83991000`; every `*_83991000.bmp` read-back is pure black.
  Either the movie frame never reaches that texture or those draws produce no colour. These two
  were the remaining rendering gaps; the inline-push fix did not resolve them.
- Final validation: a clean run stayed alive at 3/5/7/9/11/13 s with zero crashes, traps or
  `indirect call` errors; 4 of 6 frame captures have mean luminance 34-36/255.

## Compressed ramp textures fixed (2026-11-11)

The two suspected GPU-transfer failures shared a different cause: the Vulkan renderer enlarged
thin block-compressed textures to complete 4x4 blocks. `CreateTexture` turned the game's 16x2
and 256x2 DXT1 ramps into 16x4 and 256x4 images. Normalized texture coordinates then sampled
unused block rows, which contain black/transparent texels. The fix keeps the guest image width,
height and mip count; `RowBytes` and `RowCount` still allocate complete compressed blocks.
Vulkan supports partial blocks at an image's edge; see the
[Khronos image-copy guide](https://docs.vulkan.org/guide/latest/image_copies.html#_partial_texel_block).

Evidence distinguishing the original symptoms:

- `0de65e94` is a **256x192, 61-frame smoke effect with alpha**, not a movie of the book cover.
  Guest `0x107360` locks the texture, then `0x1A7D00` copies decoded Bink pixels to
  `0x820AB000` with pitch 1024. CPU and GPU texture captures match **byte for byte, including
  alpha**, in `/tmp/ghoulies-investigation/gpu-capture/082013`, `082017` and `082021`.
  The first two contain visible smoke pixels; the last is transparent, as is the movie's last
  frame. The smoke shader also samples the broken 16x2 DXT1 ramp at `0x820AA000`.
  Neither Bink decoding nor `D3D_BlockOnResource` needed a change.
- The early `0x83991000` pass samples the 256x2 ramp at `0x823E3000`. Its shader multiplies
  `(t0.rgb + t1.rgb)` by that ramp, with vertex-shader coordinates `(1, 1)`. Before the fix
  this selects padding and zeros the colour. A temporary white-pixel-shader probe proved that
  the geometry rasterizes. With the real shaders and the fix, the clean-build capture
  `/tmp/ghoulies-investigation/final-capture/082454/0048_83991000.bmp` contains six coloured
  pixels (RGB maxima 210/219/138). The tiny footprint is consistent with the early pass's
  projection. Frames with no contributing draws can still be entirely black; later intro
  captures also contain substantial coloured output. A black clear alone is not a failure.
- Some meshes deliberately use a black material: the guest reads material RGBA bytes in
  `0x15AD20` and explicitly calls `SetPixelShaderConstant(0)` with `(0, 0, 0, 1)`.
  Do not replace those colours or force the offscreen target nonblack. The fixed intro now
  reveals the blue/gold Rare book cover and animated smoke through the normal shaders.

Validation: the new GPU regression `render: thin block-compressed textures keep their logical
sampling extent` fails on the old code for 16x1, 16x2, 1x16 and 2x16 textures (2048-3072 pixels
sample padding), then matches the CPU reference exactly in all four cases after the fix.
All **23 renderer cases pass**, including in the clean final SDK build. That build stayed alive
through captures at 4/6/8/10/12/14/16/22/28 seconds, then exited normally at 29 seconds with no
crash or guest trap (`/tmp/ghoulies-investigation/final-run.log`). Temporary movie hooks, shader
overrides and extra texture-dump probes were removed before this final build. Proof images in
`/home/jon/Pictures/Ghoulies-working/`:

- `13-after-compressed-ramp-fix-smoke.png` — animated smoke over the blue book.
- `14-after-compressed-ramp-fix-Rare-book.png` — gold Rare logo and blue cover.
- `15-after-compressed-ramp-fix-Microsoft-book.png` and `16-after-compressed-ramp-fix-open-book.png`
  — the next covers and pages render too.
- `17-after-compressed-ramp-fix-target-83991000.png` — real offscreen-target output.
- `18-after-compressed-ramp-fix-desktop-{8,12,16}s.png` — real desktop screenshots.

## Current blockers

1. **Audio not verified by ear.** The L.A. Rush voice bridges and Ghoulies' remaining bring-up
   no-ops (`0x1C6B0F`, `0x7731`, `0x5F95`, `0x5D92`, the `0x1C5DAB..0x1C5E64` family) accept
   XACT's hardware calls at the boundary rather than modelling the APU. Streaming music, dialogue
   and effects have not been listened to. Review each no-op against the real `dsound` bridges once
   audio is the focus; L.A. Rush's overlay is the template. `0x1C6674` is no longer a no-op: the
   5849 scan named it `CDirectSoundStream_GetStatus` and it now goes through the mixer's
   `STREAM(GetStatus)`.
2. **Only the intro is proven.** The compressed-ramp fix restores the smoke effect,
   coloured book cover and early offscreen pass. Gameplay, menus, input, saves and display
   modes are still unvalidated; this is not a complete visual comparison against an Xbox.

## Collection integration (2026-11-11)

Both Ghoulies and Panzer Dragoon Orta are registered in the twelve-port source
collection, with build/run instructions and executable provenance. The earlier
`prepare.py all` blocker is resolved: Splashdown's overlay was rebased around the
shared shader setters and its duplicate indexed-UP bridge removed. Chihiro also
uses the shared indexed-UP bridge, and its target-alpha test patch was rebased
around the new compressed-texture regression. SDK preparation succeeds for all
twelve titles. Android staging is available, but Android native builds and device
validation for these two new ports remain pending.

## Known issues / next gates

- Audio: XACT plus DSOUND; the L.A. Rush entries in the porting cookbook cover the bridges this
  needs. Most of the audio symbol names in the config are still 0; the XTLID table and the generated
  guest bodies are the way to name and prove them.
- Bink video: the guest decoder and texture copy are working for the intro smoke movie.
  Leave the timing-only host wrappers unmapped; no additional Bink HLE was needed for this fix.
- Input, saves, display modes: unproven.

## Build/run

```sh
python3 tools/prepare.py Ghoulies
python3 tools/build_linux.py Ghoulies --jobs 8
./ports/Ghoulies/out/build/linux-release/Ghoulies
```

Assets: extracted game files in `ports/Ghoulies/assets` (a symlink to
`/mnt/Data/xbox-recomp-assets/Ghoulies` on this machine), with `default.xbe` at the root
(source archive `Grabbed.By.The.Ghoulies.USAXBOX-ZTM.rar`, kept out of Git).
