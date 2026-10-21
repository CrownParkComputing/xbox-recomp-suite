# Virtua Cop 3 (MYSTRO)

Native Linux and Android ARM64 build configuration for the supported MYSTRO Xbox
ISO variant. Linux gameplay, mouse aiming and animated stage previews were
verified in the standalone port. The Android APK builds and input tests pass;
on-device boot/gameplay remain unverified.

Commands run from the suite root. Prepare the matching local ISO with a built
suite `iso_extract` tool, or supply its extracted files in `ports/VCop3/assets`:

```sh
python3 tools/build_linux.py Splashdown --tools-only
python3 ports/VCop3/tools/prepare_mystro.py /path/to/your.iso --suite .
python3 tools/build_linux.py VCop3 --generate-only --jobs 4
python3 tools/build_android.py VCop3 --jobs 4
bash android/run-android.sh VCop3
```

`default.xbe` must have SHA-256
`832b6b786ae44acca64ad10aa82621ce86a8e209866259e78821e3d3c396e83c`.
The Android picker accepts the matching ISO or its extracted folder. The APK
does not contain the executable or game data.

Touch the game area to aim/fire. Use Start, Reload and Slow on the overlay.
A mouse uses left fire, right reload and middle slow motion. Enter starts;
F10 or R3 opens settings. Full-game progression, Android rendering/performance
and sound fidelity remain open.

Build the Linux executable with `python3 tools/build_linux.py VCop3 --jobs 4`.
Native notes from local commit `a8f6497` are preserved in
[docs/LINUX_SNAPSHOT.md](docs/LINUX_SNAPSHOT.md). The old Rev B diagnostic variant
described there is not selected by this suite build.
