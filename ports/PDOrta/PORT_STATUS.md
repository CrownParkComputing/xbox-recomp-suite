# Panzer Dragoon Orta — port status

Gate: **builds, boots, plays movies and supports active Episode 1 gameplay**.
The black screen was caused by a missing `D3D_InitDefaultState` symbol: the renderer
received real draws with colour writes disabled. Mapping it to `0x002B3790` restores
visible output. The reference comparison also exposed guest scheduling, thread
exit status, audio API and indexed UP drawing gaps, now fixed below. The complete
game is not validated; see the remaining limits.

The title completes XAPI process initialization, mounts its cache partition, and
builds the Z: cache from disc on first boot (~44 s, 128 files). With a warm cache,
the logos/title render at 60 fps and the attract movie starts about 45 s after boot
at 30 fps. Movies now advance through different scenes; skipping the opening
movie completes CRI teardown and loads Episode 1.

## Identity

- Title: Panzer Dragoon ORTA, title ID `0x53450007`, region USA (XBOX-ZTM rip)
- Executable: `assets/default.xbe`, SHA-256 `c1af4eafccc02e11105d606b177263216cb7aa71bf1fbe8486ddfc3b1b848d2c`
- Secondary image: `assets/PD1.xbe` (Panzer Dragoon 1 bonus), SHA-256 `bf395d499a133e3f4b7c39f34a29d40b210827113d6ee9021b891a15552d86cb` — not yet recompiled; a second `add_xbe_image` entry is still needed if the title launches it
- XDK build: **4928** (all libraries 1.0.4928; both XBEs report the same builds)
- Entry point `0x231E6C` (= `mainCRTStartup`), kernel thunk `0x30C8A0`
- Certificate: AllowedMedia `0x00000002` (DVD only), init flags `0x05` (mount utility drive,
  limit devkit memory), game region 1, ratings 3

## Symbols / layout

The SDK has no 4928 signature set. Addresses come from XbSymbolDatabase
(`OOVPADatabase/*/4928.inl`, verbose scan: 446 matches — D3D8 174, DSOUND 205,
XAPILIB 62, XGRAPHC 4) imported by `sdk/tools/symbols/xbsdb_import.py`:
156 of 257 names filled. Hand additions (all proven in the image, comments in
`config/game.toml` keep them through re-imports):

- `DirectSoundCreate = 0x2C49ED` — the game calls it `(guid=NULL, ppDS, unk=NULL)`
- `D3D_InitDefaultState = 0x2B3790` — called at `0x2B4386`; initializes ten transforms, render states and texture stages
- `[data]`: `D3D_Device=0x2BDEB0`, `D3D_DirtyFlags=0x2BD714`, `D3D_IndexData=0x2BD710`,
  `D3D_SimpleRenderStateEncode=0x310B18`, `D3D_FvfShader=0x2BDD88`,
  `DSound_StreamVtable=0x32DD9C`, `DSound_StreamVtable2=0x32DD90`
- `xdk/4928/layout.toml` — new file; every field proven from disassembly of the
  draw-path functions and every proven value matches the 4721 layout (miniport at
  device +0x23C0). `CDevice_Field` (+0x2590) left unproven; traps if needed.

Open zeros worth reviewing next: all CRT math (`_CIacos`, `crt_*` — original code
runs, fine), `DirectSoundCreateBuffer`, `DirectSoundUseLightHRTF`,
`XAudioDownloadEffectsImage`, `D3DDevice_Present`,
`IDirectSound_CommitEffectData`, `IDirectSoundBuffer_*` subset, all `XNet*`/`XOnline*`
(unused offline), `XPP_MemoryUnitType` (only gamepad + IR dongle device types exist).

## Code generation

`python3 tools/build_linux.py PDOrta --generate-only` → 18463 functions,
842 863 instructions, 2 833 600 of 3 127 444 code bytes, 555 jump tables.
`[functions]` has `0x2918E0` (vertical-blank callback reached via
`D3DDevice_SetVerticalBlankCallback`) plus 19 virtual-method entries proven from
the class tables at `0x331F00..0x333820`. Their bodies had been translated from
preceding padding, but dispatch lacked the actual addresses in those tables.
Scene setup first trapped calling `0x27FE40` from `0x446F2`; all three missing
methods of graphics vtable `0x332D00` are now explicit entries.

Correction to an earlier note: the `PSGSFD00`/`PSGSFD_I`/`PSGSFD_B`/`PSGSFD_P` sections are **not**
data. They are the CRI Sofdec MPEG player and are executable (flags 0x26/0x36/0x16/0x06); their
code is translated (e.g. `sub_002F9100`) and it is called from the image's own movie code
(`sub_00250FF0` calls `0x2FC800`). Treat the "unimplemented" counts there as suspect rather than
junk until the movie renders. `MMATRIX` is likewise an executable section.

## Bring-up fixes (in order hit)

1. APU MMIO crash: guest DSOUND ran its hardware thread — fixed by mapping
   `DirectSoundCreate`/`DirectSoundDoWork` (HLE owns the DS object now).
2. `NewBuffer` desc=NULL crash: came from mislabeling `DirectSoundCreate` at
   `0x2C4980`; the correct address is `0x2C49ED`.
3. COM aggregated-interface crash: the title stores the DS interface and
   AddRef/Releases/QueryInterfaces it through thunks that read `[I-8]` as a vtable.
   `src/game/hooks.cpp` fabricates `FakeObject`/`FakeInterface` (stub vtable of the
   XDK's own S_OK stubs), `FixDsStore` ([[hook]] at `0x21E4D3`) rewrites the stored
   pointer after `DirectSoundCreate`, and `sub_0021E430` reimplements the title's own
   QI helper (IID at `0x32D3F8`).
4. `IDirectSound_SetAllParameters` (`0x2C43BD`) is a delegating thunk the shared HLE
   doesn't implement — stubbed `S_OK` in `src/game/hooks.cpp`.
5. `D3D_FvfShader` was zero → `CDevice_VertexShader` null on first BeginPush — filled
   from the device init store at `0x2B4348`.
6. **Media check failed, so the XAPI abandoned process initialization** (shared SDK fix).
   The certificate allows DVD media only, so `XapiInitProcess` (`sub_0023272B`) calls the media
   verification at `sub_0023263C`: it opens `\Device\Cdrom0` and sends MODE SENSE(10) for the
   drive's vendor page `0x3E` through `IOCTL_SCSI_PASS_THROUGH_DIRECT` (`0x4D014`), five times,
   and requires page byte 1 == 1 with bytes 0 and 2 non-zero. The runtime zeroed the buffer, so
   the title read `STATUS_UNRECOGNIZED_MEDIA` and called `XapiBootToDash` (`sub_002325D7` →
   `XLaunchNewImageA(NULL, launchdata)`) — this is the "`XLaunchNewImageA` after 11–38 s" that was
   previously recorded as the blocker; the path is empty because the dash is requested, not `PD1.xbe`.
   The runtime refuses the dash launch and the title carried on with its initialization abandoned:
   no `Z:` mount, no `T:`/`U:` links, no ratings check, no static initializers.
   `sdk/runtime/kernel/files.cpp` now answers that mode page in `NtDeviceIoControlFile`.
7. **`FindFirstFile("...\*.*")` enumerated nothing** (shared SDK fix). The XAPI zeroes the
   `FileName` string's length when the pattern is `*.*` (`0x22E3E5`), and FATX reads an empty
   pattern as "every entry". `NtQueryDirectoryFile` matched it literally, so the title's cache
   mirror walk (`sub_0003E1D0` over `d:\`) found no directories and created none of the matching
   folders on `Z:`. Every cached write below a folder then failed
   (`z:\sprite\w_pdx01.spr.tmp`, disposition `FILE_OVERWRITE_IF`, parent missing), and the sprite
   loader retried its source open forever: 21 865 opens of `d:\sprite\w_pdx01.spr` in 12 s, and
   488 997 failed `z:\laser.txb.tmp` creations in the run before that. That spin was the black
   screen. `NtQueryDirectoryFile` now treats an empty pattern as `*`.

Fixes 6 and 7 are in the shared SDK and affect every port. Links 2004 was rebuilt and A/B'd
against the same build without them: same milestones, same per-second draw counts, no regression
(its own `Z:` mount already worked; it now also logs the mode-sense answer). The other nine ports
have no build in this checkout and must be rebuilt before any claim about them.

## Black-screen diagnosis and verification (2026-11-10)

The earlier perf logs proved that Orta submitted inline textured quads and decoded
non-zero movie pixels. They did not prove that the GPU could write those pixels.
A frame capture of the old build (`/tmp/pdorta-reference-check/174142/frame.txt`)
shows both logo draws with `cw 0`: every colour channel masked out.

`Direct3D_CreateDevice` is replaced by the shared HLE, which calls the image's
`D3D_InitDefaultState` only when its address is configured. Orta's config had zero.
The generated body at `0x002B3790` initializes ten identity transforms, loops over
render-state defaults, and initializes all four texture stages. The original
CreateDevice calls it at `0x002B4386`. Configuring this symbol lets the existing
shared initialization path execute it; no renderer workaround is needed.

After rebuilding, the same draws have `cw F` (RGBA enabled). Captures show the
Smilebit logo (`/tmp/pdorta-default-state-check/174300/final.png`), the complete
Panzer Dragoon Orta title screen (`174324/final.png`), and the attract-movie
picture (`174346/final.png`). Build: `cmake --build
ports/PDOrta/out/build/linux-release --target PDOrta -j 6`.

The inline draw path already works: BeginPush/EndPush reaches the shared runtime's
NV2A `INLINE_ARRAY` parser and submits the vertices. The reference port implements
this with game-specific hooks instead. Adding its hooks here would duplicate draws.
Both ports use the same USA executable hash; render-state and texture-state
indices checked against the reference agree for XDK 4928.

## Reference comparison and fixes (2026-11-10)

Reference inspected: [wootbeer/ortarecomp](https://github.com/wootbeer/ortarecomp),
commit `5f396c90ad7a59ac5a07204102bfbfab140d32f2`. A fresh fetch of `origin/main`
in this session returned the same commit.

- **CRI synchronization / movie progression:** the reference's
  [cri_hle.c](https://github.com/wootbeer/ortarecomp/blob/5f396c90ad7a59ac5a07204102bfbfab140d32f2/src/guest/cri_hle.c)
  serializes CRI's lock/unlock (`0x233BD0` / `0x233C10`) and server passes
  (`0x236BC0`, `0x236080`) with a recursive host mutex. Its notes describe the
  same first-frame movie stall without this protection. `src/game/cri.cpp` now
  supplies these four overrides. A contended mutex wait releases the guest CPU
  before blocking, and reacquires it before continuing guest code. Captures at
  66 s and 85 s show the island flyover and a character close-up, respectively
  (`/tmp/pdorta-cri-check/175527/final.bmp` and `175547/final.bmp`), establishing
  movie progression after the change.
- **One guest CPU:** the reference enables a guest CPU lock, yields it around
  blocking waits, and handles CRI's spin thread and cooperative suspension.
  `kernel::SerializeGuestCpu`, enabled by Orta's `GameInfo::init`, now serializes
  guest execution and guest callbacks from `CallGuest`. Ordinary kernel calls
  release the CPU, elevated-IRQL regions keep it, and vblank waits yield it.
  Suspension is checked before and after acquiring the CPU; suspended threads
  park without owning it. CRI's spin helper at `0x233CB0` yields every millisecond.
  This is opt-in; other ports retain their existing execution model.
- **Input:** the reference delays controller enumeration by one second and replaces
  `XInputGetCapabilities` (`0x30C5BF`) and `XInputPoll` (`0x30C83C`). Our shared XAPI
  has neither helper: the original capabilities code dereferences the host's opaque
  handle as a USB device record and fails. Orta now has game-specific replacements
  in `src/game/input.cpp`, plus delayed connection reporting in
  `src/common/game_info.cpp`. A build/launch check with both replacements accepts
  Start and leaves the title (`10 -> 11 -> 99 -> 100`, opening-story scene), unlike
  the previous runs, which ignored Start and reached attract mode 127. The menu
  check now also reaches New Game, Normal difficulty, the opening story,
  the opening movie and Episode 1; movement, shooting and pause/resume work.
- **Thread exit status:** movie teardown initially hung in `0x2341C0` even after
  its worker exited with `0xF0000001`. XAPI `GetExitCodeThread` at `0x22D503`
  references the ETHREAD and directly reads signal state at `+4` and ExitStatus
  at `+0x120`. `MarkExited` only updated its host status. It now writes the guest
  ExitStatus before signaling. The next run exits all four CRI workers and
  proceeds `127 -> 128 -> 134 -> 93 -> 18 -> 19` into Episode 1.
- **Audio object boundaries:** Episode 1 loading crashed through
  `IDirectSoundBuffer_AddRef` (`0x2C12AE`), which subtracted `0x1C` from an opaque
  host buffer and called an Xbox COM vtable. The shared mixer now implements
  buffer AddRef and Pause; Orta maps those, stream GetStatus (`0x2C1EB7`), buffer
  SetMixBinVolumes (`0x2C2566`) and SetMode (`0x2C38B3`). `src/game/audio.cpp`
  routes the remaining stream spatial setters to the existing mixer APIs and
  accepts unsupported cone/Doppler/envelope/LFO controls without entering
  hardware code. The symbol importer now recognizes BUFFER/STREAM wrappers,
  so these API names survive a later import.
- **Indexed UP draws:** the Episode 1 intro rendered, then trapped at
  `0x2AF1EB` dividing by zero inside the original `DrawIndexedVerticesUP`.
  This public five-argument API (`0x2AF170`) was missing from the SDK. Its new
  HLE uploads vertices through the existing UP ring, using max index + 1 for
  the source span, and submits the original indices to the shared backend.
  It restores the previous stream and base vertex after recording the draw.

### Gameplay verification

The final build was run under GDB with `XBE_LOG_VERBOSE=1 XBE_PERF=1` and
`XBE_CAPTURE_DIR=/tmp/pdorta-gameplay-v3`. Log:
`/tmp/pdorta-gameplay-v3.log`. New Game / Normal, story skip and movie skip
reach Episode 1 at 66.086 s. It continues through the opening flight and active
combat, including movement and shooting supplied through a real uinput keyboard.
At 255 s the scene still runs at 60 fps with no guest trap or missing indirect
call (over three minutes after Episode 1 loaded, including a pause/resume cycle).
This is a bring-up check, not a completed episode.

Evidence:

- Movie progression: `/tmp/pdorta-cri-check/175527/final.bmp` and
  `/tmp/pdorta-cri-check/175547/final.bmp` (different scenes and hashes).
- Active flight/combat at 114 s:
  `/tmp/pdorta-gameplay-v3/181013/final.bmp` and
  `/tmp/pdorta-episode1-active.png` (dragon, terrain, rain, enemies, effects,
  HUD and radar).
- Pause menu: `/tmp/pdorta-gameplay-v3/181201/final.bmp`.
- Back in active combat after resume:
  `/tmp/pdorta-episode1-resume-confirm.png`.

## Remaining limits

- Episode 1 is the gameplay bring-up target; later episodes, bosses, saves and
  the complete game have not been validated in this runtime.
- Audio output has not been checked by ear against hardware. The shared mixer
  still omits cones, Doppler, envelopes, LFOs and the DSP image's actual effects;
  accepting their APIs does not implement those effects.
- **Bonus game:** `PD1.xbe` still needs its own recompiled image.

A boot log with zero missing kernel imports does **not** rule out these gaps:
CRI is guest middleware, and the original XAPI USB helpers can execute and fail
without calling an unimplemented kernel export.

Focused checks: the two guest CPU concurrency/yield cases pass in `game_tests`;
the existing IRQL, dispatcher-queue and vertex-program render checks pass (the
last matches the CPU reference with zero differing pixels). The symbol importer
passes all ten cases, including wrapped audio APIs and hand-proven addresses.
Links 2004 also rebuilds successfully against the shared-runtime changes; it was
not launched in this session, so that is a compatibility build check only.

## Build/run

```sh
python3 tools/prepare.py PDOrta
python3 tools/build_linux.py PDOrta --jobs 8
./ports/PDOrta/out/build/linux-release/PDOrta
```

Assets: extracted game files in `ports/PDOrta/assets` with `default.xbe` at the root
(source archive `Panzer.Dragoon.Orta.USA.XBOX-ZTM.rar`, kept out of Git).

Diagnostics used here, all reversible: `XBE_LOG_VERBOSE=1` for the full log, `XBE_PERF=1` for the
per-second draw/upload counters, `strace -f -e trace=openat` to count a retry loop, and F3 (guest
thread stacks, and a frame capture with `XBE_CAPTURE_DIR` set). F3 needs the game window focused;
on this Wayland desktop a uinput device created directly (python-evdev) reaches the compositor and
SDL, while ydotool does not.

## Collection integration (2026-11-11)

Orta and Ghoulies are registered in the twelve-port suite, including Linux
recipes, executable provenance and explicit Android validation limits. Temporary
black-screen method counters and frame-driver dumps were removed from
`src/game/hooks.cpp` before publishing; the functional DirectSound, CRI and input
hooks are retained. Both Linux targets rebuild against the collection's shared
SDK, and Android source staging succeeds without claiming an Android native
build or device check.
