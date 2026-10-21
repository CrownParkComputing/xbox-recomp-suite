> Historical standalone Linux validation snapshot. Android integration and current commands are described in ../README.md.

# HOTD3 status — 20 October 2026

The Linux SDL3/Vulkan build reaches interactive 3D gameplay in the first stage.
Coin insertion and Start enter the game, and mouse fire consumes shotgun shells.
The corrected right-button pump input restores shells, with subsequent shots
and visible enemy damage confirmed in the first corridor.
A bounded 52-second run continues through the death/continue screen and exits
cleanly when stopped. Full-game completion, sound fidelity, persistence and
Android packaging remain unverified.

## Exact media

- Source archive: `House of the Dead 3, The (World).zip`, 436,368,610 bytes.
- GD-ROM GDX-0001, V1.004, dated 20021029, title `THE HOUSE OF THE DEAD 3`.
- Track 3 is raw MODE1/2352 at LBA 45,000. Complete ZIP CRC `0214aecb` verified.
- The matching PIC response is `317-0348-com.data`, 80 bytes, SHA-1
  `40dbbc092bc9f99b8d2ae67fbefacd62184f90ec`, matching MAME's Chihiro definition.
- `BFN.BIN` selects `HOD3AC_S.BIN`. DES decryption produces FATX with
  16 KiB clusters and data offset 69,632. Decrypted image SHA-256:
  `36743841ed333a28e4592173638b21442f690d1236cd4c61dafb4570146b8af9`.
- Extracted 36 files, 373,429,062 bytes. All sizes and SHA-256 hashes rechecked.
- Main `hod3xb.xbe`: 2,191,360 bytes, SHA-256
  `89a0e7778002205c114a91bc1797291344b2afeea8b5c7d5085cce7e47dc71de`.
- Debug-key entry `0xB8BAC`, base `0x10000`, title ID zero, XDK 4831 libraries,
  13 sections. `acdemoT.xbe` is a separate 40,960-byte image, not translated.

Private manifests and inspection evidence are under `.local/`.

## Runtime work

`config/xdk-4831.toml` records structure offsets with their instruction evidence
from this executable. XbSymbolDatabase supplies symbol candidates; manually
verified additions carry comments in `config/game.toml`. Unknown fields remain
absent. The current translation discovers 8,278 functions, 33 overlapping decodes
and 50 unsupported-instruction trap templates. No kernel imports are missing.
This is not a clean translation; unvisited paths can still trap.

The pinned SDK is privately prepared with the six VC3 overlays: 128 MiB RAM,
visibility Flush delivery, buffer envelope/pitch handling, indexed user-memory
draws, controller capabilities and opaque XRGB sampling. The shared SDK and
the VC3 project remain unchanged.

The original cabinet setup waits for physical JVS hardware. Its board-absent
branch skips initialization required by the credit manager. The host bridge
instead initializes the real board API critical section and original credit
manager, and supplies a node descriptor with two players, two coin slots,
four 16-bit analog channels and six outputs. The game's existing report readers
and capability checks remain active. Only player 1 has a host mouse binding.

The startup Reset call enters the NV2A miniport. The verified startup caller
recreates its targets through the host CreateDevice API, retaining the game's
initial 640×480 canvas. Other reset callers return `E_NOTIMPL`.

The cooperative scheduler uses the CRT's setjmp/longjmp pair. Its observed
continuation is explicitly translated; a host wrapper unwinds to the scheduler
frame with the matching saved EBP. The original CRT still restores guest
registers, stack and SEH state. This prevents an exited task from continuing
through obsolete host calls.

Mouse input uses SDL events and the renderer's presentation rectangle, including
letterboxing and display density. Brief taps survive background connection
polls; focus loss and graphics settings clear pending buttons. The cabinet
bridge converts aim to big-endian analog reports and preserves the game's
trigger/start processing. Coin insertion calls the original credit conversion.
The shotgun pump is game bit 3, supplied through the second player-switch byte's
bit 7. Game bit 1 instead marks an invalid gun position; it is not reload.

Coin audio and starting a session reached public DirectSound wrappers missing
from the initial symbol import. Verified `SetPitch`, `SetEG`, `SetMixBinVolumes`
and `SetMode` addresses now use the existing host handlers. Buffer envelopes
still report `E_NOTIMPL`. Internal graphics reference handlers are mapped using
this executable's resource layout, avoiding the resource-release warnings seen
during early startup.

## Validation and open work

Both mouse tests and five FATX extraction tests pass. Native and headless builds
pass. The final 18-second CPU-only run reaches 840 frames after scripted coin
and Start input with no empty frames or guest trap; its timeout is intentional.
Native testing combines injected SDL events with real mouse input through the
same event watch and cabinet poll, with screenshots retained privately.

Verify sustained play, sight/hit alignment, sound and credit behavior. The
virtual cabinet does not yet provide hardware EEPROM/backup persistence or
player-2 controls. Audit remaining instruction traps and additional image/reset
paths before packaging Android builds or claiming the game fully playable.
