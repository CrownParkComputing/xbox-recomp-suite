# OutRun 2 (Chihiro 2.000)

Native Linux and Android ARM64 configuration for the matching extracted arcade
archive. Linux first-course acceleration, steering and braking were verified
in the standalone port. The Android APK builds; on-device boot/gameplay and
touch driving remain unverified.

From the suite root, extract your matching local archive or supply its files in
`ports/OutRun2/assets`, with `outrun2.xbe` and the original `Media` directory:

```sh
python3 ports/OutRun2/tools/prepare_game.py /path/to/your.zip --output ports/OutRun2/assets --report ports/OutRun2/.local/extracted.json
python3 tools/build_linux.py OutRun2 --generate-only --jobs 4
python3 tools/build_android.py OutRun2 --jobs 4
bash android/run-android.sh OutRun2
```

Main executable SHA-256:
`d98a7efc0ee4ba48b6e938743fa309a9fc774c05713ef735e67d1b17b9bbbd1b`.
The Android picker expects the extracted folder, not the ZIP. Game files stay
outside the APK.

Tap Coin then Start. Hold Left/Right to steer, Gas to accelerate/confirm and
Brake to slow. Multiple fingers can steer and accelerate together. The physical
controller uses its left stick and triggers. On Linux, Backspace inserts a coin,
Enter starts, arrows steer/drive, W accelerates and Q brakes. Settings/F10/R3
opens the shared paused settings panel.

Build Linux with `python3 tools/build_linux.py OutRun2 --jobs 4`. Native notes
from local commit `32f8454` are preserved in
[docs/LINUX_SNAPSHOT.md](docs/LINUX_SNAPSHOT.md). The virtual motor completes cabinet
checks without physical force feedback. Cabinet storage is volatile. One
concurrent headless repeat stalled during selection; repeated-start reliability,
complete race progression and sound fidelity remain open.
