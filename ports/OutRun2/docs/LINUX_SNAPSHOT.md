> Historical standalone Linux validation snapshot. Android integration and current commands are described in ../README.md.

# OutRun 2 status — 21 October 2026

The experimental Linux native build reaches interactive selection screens and
a rendered first-course race. Sustained play, course completion, sound fidelity
and all optional cabinet features still need verification. No Android package
or GitHub remote is configured for this standalone project.

## Matching input

- Local archive: `OutRun 2 (2.000)(2003)(JPN,USA,EXP)[Sega Chihiro][TP].zip`,
  347,955,269 bytes. This is an already extracted/decrypted archive, not a
  BIN/CUE GD-ROM dump. Its original patch provenance is not established.
- Extracted 369 files, 373,310,558 bytes; all ZIP CRCs checked and all extracted
  sizes and SHA-256 hashes rechecked against the private manifest.
- Main `outrun2.xbe`: 2,306,048 bytes, SHA-256
  `d98a7efc0ee4ba48b6e938743fa309a9fc774c05713ef735e67d1b17b9bbbd1b`.
- Debug-key entry `0x9C043`, image base `0x10000`, title ID zero, XDK 5558.
  The separate `Testmode.xbe` is not translated.

## Runtime integration

`config/xdk-5558.toml` records offsets verified from this executable, with
instruction addresses. Symbol candidates come from XbSymbolDatabase; manual
additions record their ABI evidence. Unknown layout fields remain absent.
Translation currently discovers 13,381 functions, 43 overlapping decodes,
12 C++ catch blocks and 236 jump tables. Many unsupported instruction templates
remain on unvisited paths; this is not a fully verified translation.

The private SDK is prepared from the pinned suite revision using six recorded
overlays: 128 MiB memory, guest push-buffer visibility, audio envelope/pitch
handling, indexed user-memory draws, controller capabilities and opaque XRGB
sampling. The shared suite SDK and other ports remain untouched.

The cabinet bridge supplies EEPROM, backup SRAM, controller firmware storage,
host UTC in the RTC's BCD format, released test/service inputs and a JVS driving
node. The original acLib initialization, storage checksums, credit conversion,
capability checks, report readers and analog calibration still run. The virtual
board exposes two players, two coin slots, eight analog channels and eight
outputs, with host controls connected to player one. Storage starts erased and
is volatile; this does not execute real I/O-board firmware.

The title's steering-motor packet boundary supplies reset/readiness/centering
responses so the original cabinet test can complete. Motor status reports no
mechanical fault; physical force feedback is not provided.

The disc worker's idle SuspendThread call at `0xA9D95` is replaced with a 1 ms
sleep at that call site. This avoids a lost-wakeup stall when submitting and
worker threads run concurrently on the host. The game's original worker,
file reader and decoder continue to perform the reads. All other suspend calls
retain their original behavior.

The console debugger OutputDebugStringA interrupt is routed to the title log.
The CRT's indirect thread-notify callback is explicitly translated. Verified
DirectSound wrappers use the runtime handlers; sound fidelity is unverified.

## Remaining checks

Five imported kernel functions have no implementation: ObOpenObjectByName,
NtCreateIoCompletion, NtSetIoCompletion, NtRemoveIoCompletion and
IoQueryVolumeInformation. They have not been observed on the tested path.
The archive lacks `ext_time_tbl.tgt` and `ext_diff_tbl.tgt`; those optional
configuration reads currently fail normally. Additional modes, networking,
cabinet persistence, physical wheels and complete race progression remain
untested.

Private logs, frame captures, scripted inputs, disassembly and media manifests
are retained under `.local/` and are excluded from source commits.

## Validation

Native and headless builds pass, along with five archive-preparation tests.
A bounded 74-second native run reaches the first course, accelerates from
89 to 119 km/h in captured frames, accepts steering, and reports the original
game's normalized accelerator/brake transitions from 0 to 255. Left and right
steering produce -127 and +127. Releasing acceleration and applying the brake
is also captured. The process exits cleanly when the test stops it.

Headless input testing reaches race mode and verifies the same normalized
steering and pedal values. One simultaneous headless repeat remained in the
selection/loading mode; repeated-start reliability needs further testing.
Full-game completion and a performance benchmark are not claimed.
