# L.A. Rush — native Xbox recompilation

Experimental Linux/Vulkan and Android arm64 port of L.A. Rush (Xbox title ID
`4D570025`, XDK 5849). The original local checkout was named SFRush-XBERecomp;
its executable and assets are L.A. Rush.

No retail executable, disc image, extracted game content, generated guest C++,
saves, APKs, or signing keys belong in the source repository. Supply your own
matching game. See [PORT_STATUS.md](PORT_STATUS.md) for actual validation and
remaining problems.

## Build from Xbox Recomp Suite

Place extracted game files under `ports/LARush/assets`, with `default.xbe` and
its folders together, then run from the suite root:

```sh
python3 tools/prepare.py LARush
python3 tools/build_linux.py LARush --generate-only
python3 tools/build_android.py LARush --jobs 4
```

The APK is written to `dist/LARush-arm64-debug.apk`. Omit `--generate-only` for
the desktop executable. The suite's building guide lists the required compiler,
Android SDK/NDK, Vulkan tools, and pinned dependencies, including SDL.

Android uses a physical controller. Choose an extracted game folder or ISO
through the shared SD document picker once. The selection is remembered while
accessible. Minimizing ends the dedicated native game process so the next launch
starts a fresh session.

## Runtime changes

The isolated `overlays/larush-sdk.patch` contains overlapped file completion,
stable XInput packet numbers, XACT voice support, and custom vertex stream binding. `src/game/hooks.cpp`
adapts this executable's APU clock and public sound APIs. The original XACT
engine initialization runs; it must not be replaced with a success stub.

## Diagnostic checks

After building the desktop target, `tools/build_probe.py` links the real guest
code and runtime to a headless Vulkan host. `--file-smoke`, `--clock-smoke`,
`--input-smoke`, `--audio-smoke`, and `--vertex-smoke` select focused integration checks. Run the
resulting executable with the extracted asset directory as its argument, from a
separate scratch directory so diagnostic saves stay separate from real saves.
The plain probe accepts the SDK harness `cmd`, `snap`, and `capture` files.
Its render switches are diagnostic controls and are not enabled in release APKs.

## Credits

Built on [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp),
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp), SDL, Zydis, toml++,
Dear ImGui, Vulkan Memory Allocator, and glslang. Their upstream licenses and
credits remain in the suite.
