> Historical standalone Linux validation snapshot. Android integration and current commands are described in ../README.md.

# Virtua Cop 3 status — 20 October 2026

The supplied MYSTRO Xbox ISO now builds as a native Linux SDL3/Vulkan
application. Original startup screens, mission maps and animated stage previews
on mission select render.
Player 1 has absolute mouse aiming, left-button fire, right-button reload,
middle-button slow motion and Enter for Start. Complete gameplay, correct
sound and Android packaging remain unverified.

## MYSTRO media and executable

- Source: `Virtual Cop 3 (XBOXOG) - Chihiro (Patched by MYSTRO).iso`,
  489,029,632 bytes; SHA-256
  `9c92cc24d36b5c9cb4463b76c06be5df173ecae7b28680da34a5e0cfa3faf204`.
- XISO extraction: 348 files, 494,655,178 logical bytes. All file sizes and
  SHA-256 hashes were reverified against the private manifest.
- `default.xbe`: 2,174,976 bytes; SHA-256
  `832b6b786ae44acca64ad10aa82621ce86a8e209866259e78821e3d3c396e83c`.
- Title ID zero, title `Virtua Cop 3`, base `0x10000`, retail-key entry
  `0xB6407`, kernel thunk `0x140200`, 13 sections, XDK 5028 libraries.
- Its executable body matches the included `vc3.bak`; eight header bytes
  select retail rather than debug decoding. It is a different build from the
  original Rev B XDK 5455 executable, not merely that executable with a patch.
- 332 of 337 shared media paths are identical to the Rev B extraction. Changed
  shared files are `boot.id`, `firmware.bin`, `firmware2.bin`, `capdat_j.bin`
  and `spr_common.xts`.

`tools/prepare_mystro.py` checks exact ISO/XBE hashes and refuses to overwrite
an extraction. Inputs, assets, the manifest, generated C++, SDK copies, test
captures and build outputs are ignored. No game content is committed.

## Native implementation and evidence

`config/mystro.toml` and `config/xdk-5028.toml` are independent of Rev B.
The device, streams, texture state, presentation and audio-effect offsets were
derived from this executable's instructions. Unknown offsets remain absent;
no XDK 5849 layout is substituted. Translation reports 6,697 functions, 318
jump tables, 11 callbacks and 36 overlapping decodes, with unsupported library
instruction traps still present. This is not clean translation.

The clean suite is pinned at `9f957d6584a3e8f81d7410bcbd79ed342e52cc51`.
Build tools copy its SDK privately, apply six recorded patches and compare
that copy byte-for-byte against a fresh patched tree. The shared SDK remains
unchanged:

1. `chihiro-memory.patch` enables 128 MiB main RAM.
2. `flush-guest-push.patch` drains guest commands before the original visibility
   reader polls, and delivers completed Vulkan reports on Flush. Actual sample
   counts are preserved; the regression checks 200 covered pixels without a
   Present call.
3. `buffer-envelope.patch` implements the verified 4096-units-per-octave pitch
   conversion. Unsupported MCPX buffer envelopes return `E_NOTIMPL` with a
   diagnostic instead of entering a hardware driver with a host voice object.
4. `indexed-up.patch` implements the reached five-argument indexed user-memory
   draw, expanding 16-bit indices into the fenced vertex upload ring.
5. `controller-capabilities.patch` supplies a checked 25-byte capability report
   for the host's controller handles. The original XDK routine expects a USB
   driver object and cannot interpret these handles.
6. `target-alpha.patch` makes XRGB texture reads opaque in fixed-function and
   programmable shaders. The movie decoded correctly, but CPU overlays into a
   GPU target preserved zero alpha. Sampling that target as XRGB incorrectly
   returned transparent pixels. The regression renders RGB with stored alpha
   zero, checks opaque XRGB blending and preserves ARGB transparency in both
   shader paths.

The native build resolves every kernel import. Derived VBlank count/callback
fields enable the existing 60 Hz callback thread. The original graphics
initialization and four observed constructor-registered callbacks execute.
The game's own Xbox controller path is selected after its cabinet-capacity
probe; `VCOP3_INPUT=arcade` preserves the original detection. Its 640×480
canvas is retained instead of the cabinet helper's 1024×768 override.

Mouse aim is applied after the original player-1 controller gun update at
`0x7B09C`, writing both final and raw coordinates. The original firing/reload
logic remains. Button taps are consumed only inside the game's
`XInputGetState` call at `0x7C843`, excluding XPP's background connection
checks. The mapping inverts the renderer's centered presentation rectangle
and uses SDL logical coordinates, so resizing, letterboxing and display
density do not introduce a separate aim scale. Focus loss and graphics menus
clear held/pending buttons.

After the original Sofdec copy at `0x82843`, the completed CPU write is marked
dirty before the texture is sampled again. The guest decoder and original
preview drawing remain in use; the displayed frames come from the supplied
movie. Native captures verify all three animated previews are visible.

Validation completed:

- Native application builds and displays region, SEGA, mission maps and all
  three animated mission-select previews. A bounded 52-second run reaches
  rendered 3D action without a fault; full playability is not established.
- Both executable variants build as CPU-only diagnostic harnesses.
- Both mouse tests pass: coordinate bounds, letterboxing, display density,
  invalid dimensions, brief taps, background polls, SDL button events,
  reload priority, focus loss, graphics menus and player-2 isolation.
- Renderer suite: 23 cases, zero failures, including real GPU visibility
  reports after Flush and opaque XRGB target reads. Its unsupported MSAA case
  explicitly skips.
- Five FATX extraction tests pass; original disc CRC and extracted hashes pass.

The first mouse-driven scene transition exposed a crash in original
`DrawIndexedVerticesUP` at `0x10A780`, reaching a null shader object in
`0x109DE0`. The indexed-draw patch and the independently derived fixed-function
shader address `0x119B20` address that observed path. The subsequent native
probes continue through the previously failing path into a 3D scene.

## Remaining gates

Establish a stable interactive 3D scene, verify sight/hit alignment, reload,
slow motion and sound in gameplay, then test sustained play and persistence.
Audit unsupported instruction traps, symbol candidates and audio features.
Android builds and device performance follow the Linux compatibility work.
The original Rev B executable remains a separate investigation below.

---

# Original Rev B investigation

This section records the original GD-ROM profile. Its CPU-only harness starts
the guest threads and reaches a startup wait; it has no demonstrated frame.
The MYSTRO executable above uses a different XDK and a separate native build.

## Identified media

Local source: `~/Downloads/Virtua Cop 3 (World) (Rev B).zip` (998,951,696 bytes).
It contains three BIN tracks and one CUE sheet. Track 3 is raw MODE1/2352;
its sector address gives ISO start LBA 45,000. Its boot header reports
`VIRTUA COP 3`, product `GDX-0003B`, version `V3.000`, dated `20030521`;
the ISO volume is `VIRTUACOP3`.

| Root file | LBA | Bytes |
| --- | ---: | ---: |
| BFZ.BIN | 77,404 | 256 |
| VC3.BIN | 77,405 | 482,873,344 |
| BFZ.1GB | 313,183 | 256 |
| VC3.1GB | 313,184 | 482,938,880 |
| XBAMGD.BIN | 548,994 | 12,288 |

Both VC3 containers have encrypted headers, with no clear FATX or XBE header.
All five root files were extracted into ignored `.local/gdrom/`; the complete
data track's ZIP CRC32 `0d2de9f1` passed. Per-file SHA-256 hashes and inspection
metadata are recorded privately in `.local/disc.json`.
The separate `~/Downloads/vcop3.zip` supplied afterward contains the matching
80-byte `317-0354-com.data` response dump. Its SHA-1 matches MAME's definition.
The PIC chooses `BFZ.BIN`, which names `VC3.BIN`. DES decryption produces a FATX
superblock with 32 sectors per cluster. FATX extraction produced 340 files
(479,771,356 bytes); all extracted sizes and SHA-256 hashes were rechecked.
The image and file manifest remain private under `.local/`.

## Executable identity

`boot.id` identifies `SBFZ`, `SEGA-AM2 Co.,LTD`, `VIRTUA COP 3`, main executable
`\vc3.xbe` and test executable `\vc3_t.xbe`.

- `vc3.xbe`: 2,654,208 bytes; SHA-256
  `32a68a2d2a218d4cb056c04e65e004f0513169df5a641b8ad1035dafb29766c8`.
- `vc3_t.xbe`: 40,960 bytes; SHA-256
  `581b5ab504627d9b03ddc21a4721a3ee30f1dcb95b9b5d801e182cd29d92cb18`.
- Main certificate: title ID `0x00000000`, title `vc3 release`.
- XBE base `0x10000`, debug-key entry `0xBDC4D`, kernel thunk `0x1B2FE0`.
- Main libraries: XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL and LIBCMT,
  all reporting `1.0.5455`. Retail entry-point decoding is incorrect here.

## Translation and native execution

The suite's `xbe_symbols` rejects XDK 5455 because it only has 5849 signatures.
The local XbSymbolDatabase CLI at commit
`20eced544726f5558c5a408458f38a086cc4e543` found candidate addresses; the suite's
importer populated 185 of 257 expected symbol/data names. The CLI also found
JVS entry points, recorded in private `.local/xbsdb-vc3.log`. These are candidates
requiring review, not proof that every match or calling convention is correct.

`config/game.toml` selects the game's code sections and uses a partial,
independently verified XDK 5455 layout in `config/xdk-5455.toml`. DOLBY is DSP
data and is excluded. Translation currently emits 11,889 functions, 335 jump
tables and 18 callback entries. There are 143 overlapping decodes and 516
unsupported-instruction trap sites concentrated in library sections. Their
code/data boundaries still need auditing; generation is not marked clean.

The headless executable `out/harness/VCop3` builds with the pinned SDK plus the
isolated `patches/chihiro-memory.patch`. SDK files outside this workspace are
unchanged. Bounded native runs verified:

- XBE parsing, sections, kernel thunk resolution and dispatch registration.
- The entry thread and CRT-created guest startup thread execute.
- Guest drive mapping and startup file requests execute.
- The original 64 MiB run stopped at unknown XPP offsets. Offsets 0, 4 and 8
  were proven from the game's XGetDevices/XGetDeviceChanges instructions.
- The 128 MiB build gets past that check and reports controller port mask 1.
- A 12-second run reaches a startup wait with no presents. Debugger sampling
  shows the main guest thread waiting indefinitely in `kernel::Wait`; the
  current capture does not yet identify the full guest caller chain.

Kernel resolution reports five missing imports: `ObOpenObjectByName` (243),
`NtCreateIoCompletion` (191), `NtSetIoCompletion` (227), `NtRemoveIoCompletion`
(223), and `IoQueryVolumeInformation` (76). Resolution warnings do not prove
these imports have been called. Physical allocation probes of `0x8000000`,
`0x7F10000` and `0x7F01000` fail; the corresponding MYSTRO code uses these as cabinet-capacity probes, not
ordinary allocations. The cause of the Rev B wait remains unresolved. Preserve `.local/first-boot.log`, `.local/boot-128.log` and smoke logs
when investigating.

The five extractor tests pass: fragmented files and a partial final cluster,
path escape rejection, cluster cycle rejection, truncated chains and wrong PIC
rejection. Disc CRC, extracted file hashes and private input ignore rules pass.

## Next gates

1. Capture the full guest caller of the startup wait and the allocation retry
   path. Resolve the observed contract rather than forcing the wait to succeed.
2. Audit the XbSymbolDatabase candidates, calling conventions and library
   code/data boundaries; eliminate false decodes before claiming clean output.
3. Derive the remaining D3D/DSOUND layouts and globals from this exact XBE.
   Unknown fields remain trapping; no 5849 layout is substituted.
4. Implement and validate the Chihiro media-board, cabinet/JVS and gun-coordinate
   contracts the game actually reaches. Establish first present, stable menu,
   then interactive gameplay as separate gates.
5. Validate sound, input, complete gameplay and persistence on Linux before
   Android packaging and device performance checks. The test XBE has not been
   translated or launched yet.

## Hardware differences to account for

[MAME's Chihiro driver](https://github.com/mamedev/mame/blob/master/src/mame/sega/chihiro.cpp)
documents an Xbox-derived board with 128 MB RAM, media/network boards and JVS
I/O. The suite's `sdk/runtime/kernel/memory.h` fixes physical RAM to
`64u << 20`; this workspace's private SDK copy changes that to `128u << 20`
using the recorded patch. This enables the correct address range but does not
establish complete physical-memory accounting or successful game startup. The media-board DIMM variants are distinct from Xbox main RAM.
The existing CPU and GPU implementation is a plausible starting point, but its
compatibility with this particular executable is not established.

The shared SDK remains unchanged. The Rev B profile has no demonstrated gameplay.
