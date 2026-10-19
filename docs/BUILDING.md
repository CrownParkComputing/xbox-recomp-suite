# Building the source suite

## Host tools

Use a Linux x86-64 host with Python 3, Git, Clang/LLD, CMake 3.25+, Ninja,
pkg-config, SDL3 development headers, a Vulkan loader/headers and a Vulkan driver.
The existing host build scripts print package suggestions if tools are missing.
PGR1 and Links' desktop movie replacements additionally need FFmpeg development
packages (`libavcodec`, `libavformat`, `libswscale`, `libswresample`, `libavutil`).
Android retains the original guest movie routines and does not require FFmpeg.

All third-party **source** dependencies are checked in. Linux rendering uses a
separate DXC binary distribution with a fixed URL and SHA-256 in
`DEPENDENCIES.lock.json`; CMake downloads it when it is not cached. For offline
builds, unpack that exact distribution and set `DXC_SOURCE` to its root. Do not
check the downloaded distribution or compiled outputs into Git.

The host SDK prefers a compatible installed SDL3; its fallback and every Android
build use the checked-in SDL snapshot. OS graphics/audio libraries and Android
SDK/NDK/JDK installations remain prerequisites.

## Prepare and generate

```sh
python3 tools/prepare.py all
# Put the matching extracted game in ports/Splashdown/assets first.
python3 tools/build_linux.py Splashdown --generate-only --jobs 4
```

The shared SDK is used for five ports. Splashdown gets a derived SDK under
`.local/sdks/`, produced by applying `overlays/splashdown-sdk.patch` to `sdk/`.
The tool installs relative, ignored `sdk` links into each port. Do not apply the
older historical Splashdown patch series on top of this prepared SDK.
The original seven-patch history is retained under `ports/Splashdown/patches`.

Generated guest C++ lives in the port's ignored `generated/` directory. It must
come from the same local `default.xbe` whose configuration is in that port.
The source suite deliberately includes neither executable nor generated C++.

Tools alone can be built without a game:

```sh
python3 tools/build_linux.py Splashdown --tools-only
```

To extract a local image after building tools:

```sh
ports/Splashdown/out/build/linux-release/iso_extract --all /path/to/your.iso ports/Splashdown/assets
```

## Android

Verified tool versions in this snapshot: Android API 35 build platform, build
tools 36.0.0, NDK 28.2.13676358, and JDK 17. The APK minimum is Android 13/API 33
and the ABI is ARM64. Build paths can be changed with `ANDROID_SDK_ROOT`, `NDK`,
`JAVA_HOME`, and `BUILD_TOOLS`. The fallback SDK root is `/opt/android-sdk`.

```sh
python3 tools/build_android.py Splashdown --jobs 4
python3 tools/build_android.py BLACK --jobs 4
```

Packaging uses CMake and the Android SDK directly; Gradle is not required.

The first command builds SDL/glslang/SPIRV dependencies in
`.local/android-dependencies`. Native per-port builds and packaging live under
`.local/android/<title>`, and completed APKs are copied to ignored `dist/`.
Use `--stage-only` to inspect the per-port Java/manifest/native adaptations
without providing game files. `--deps-build /path/to/_deps` can reuse a matching
ARM64 dependency build; it must be made from the revisions in the lockfile.

The default debug key is generated privately at
`.local/keys/android-debug.keystore`. For an existing installation, set
`KEYSTORE=/private/path/to/the-original.keystore` before building; an update
requires the same key. Keep keys outside Git. Do not uninstall to get around a
signing mismatch: preserve saves and install with `adb install -r`.

```sh
adb install -r dist/Splashdown-arm64-debug.apk
adb shell am start -n com.splashdown.recomp/.LauncherActivity
```

## Verification

The original Splashdown implementation passed 76 SDK cases with both DXC and
the Android glslang adapter, plus six native smoke programs. That verification
is recorded as historical device/runtime evidence, distinct from clean-checkout
build checks for this source import. See `docs/VERIFICATION.md`.

Before committing or publishing:

```sh
python3 tools/audit_source.py
```

The audit checks the actual Git index for private inputs and secrets, verifies
vendored dependency hashes, and reports file count/size. Private build inputs,
SDK overlays, build products, saves and signing keys are ignored.


### L.A. Rush

Use `LARush` with the same preparation and build commands. Its SDK changes are
isolated in `overlays/larush-sdk.patch`; preparation checks and applies that patch
to a private SDK copy. The retail executable used for validation has SHA-256
`ffb5ac9c63592c571c31a05311dae137c72126f8f5836cfabdfc798185bfb4bd`.
Diagnostic checks are described in ports/LARush/README.md. The desktop build
reaches driving; Android compilation is verified and device testing is pending.
