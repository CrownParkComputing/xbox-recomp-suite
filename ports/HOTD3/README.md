# The House of the Dead III (Chihiro)

Native Linux and Android ARM64 configuration for the matching arcade executable.
Linux first-stage gameplay and mouse fire/pump were verified in the standalone
port. The Android APK builds and input tests pass; on-device boot/gameplay
remain unverified.

Put your extracted game in `ports/HOTD3/assets`, retaining `hod3xb.xbe` and its
original folders. Main executable SHA-256:
`89a0e7778002205c114a91bc1797291344b2afeea8b5c7d5085cce7e47dc71de`.
The extraction tools are retained under `tools/` and require your matching local
media/security inputs; none of those files are included.

From the suite root:

```sh
python3 tools/build_linux.py HOTD3 --generate-only --jobs 4
python3 tools/build_android.py HOTD3 --jobs 4
bash android/run-android.sh HOTD3
```

Select the extracted folder on Android. Tap Coin, then Start. Touch the game
area to aim/fire, and tap Reload to pump the shotgun. A mouse uses left fire
and right reload; Backspace inserts a coin and Enter starts. Settings/F10/R3
opens the shared paused settings panel.

Build Linux with `python3 tools/build_linux.py HOTD3 --jobs 4`. Native notes
from local commit `1c7c48b` are preserved in
[docs/LINUX_SNAPSHOT.md](docs/LINUX_SNAPSHOT.md). Cabinet persistence, second-player
controls, sound fidelity and full-game completion remain open.
