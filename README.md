# Xbox Recomp Suite

Source snapshots for seven original Xbox native ports, their Linux/Vulkan and
Android tooling, the shared Xbox XBE Recomp runtime, and the porting cookbook.
SDL and the other source dependencies are vendored with their upstream licenses
and exact revisions in [`DEPENDENCIES.lock.json`](DEPENDENCIES.lock.json).

**No retail game files, extracted content, ISOs/XBEs, generated game C++, saves,
APKs, or signing keys are included.** Supply your own matching game files to
build a port. This repository records experimental conversions; booting an intro
is not evidence of complete gameplay support.

| Port | Android evidence at this snapshot | Main limitation |
| --- | --- | --- |
| Splashdown | SD ISO boot and native race; menu and spray fixes installed | Spray comparison and Career handheld confirmation pending; native Career race has AI opponents; DSP/reverb incomplete |
| Burnout 3 | SD folder boot; loading completes and EA intro plays | Full gameplay/performance not revalidated here |
| Links 2004 | SD folder boot; intro plays | Full gameplay/performance not revalidated here |
| NFS Hot Pursuit 2 | SD folder boot; intro plays | Pause/standings text alignment remains open |
| PGR1 | SD folder boot; intro plays | Full race graphics/performance not validated here |
| BLACK | SD folder boot; intro and title screen | Full gameplay/performance not validated here |
| L.A. Rush | Android arm64 APK builds; desktop Story driving verified | Collision mesh fix verified on desktop; Android device test pending |

The shared Android launcher remembers the selected SD ISO/folder, skips the
picker while it remains accessible, and ends the native game process on
minimize/close. These behaviors were device-tested on the original six titles;
L.A. Rush uses the same launcher and awaits device validation. Its former local
folder name, SFRush-XBERecomp, was misleading: its game is L.A. Rush.

## Layout

- `sdk/`: the shared runtime, recompiler, XDK layouts, tests, and templates.
- `ports/<title>/`: per-game configuration, native hooks, and original notes.
- `overlays/`: isolated SDK changes for Splashdown and L.A. Rush.
- `android/`: shared SDL Java, launcher, document-tree JNI, shader adapter, and APK packaging.
- `third_party/`: pinned SDL, glslang, SPIRV-Tools/Headers, ImGui, toml++, VMA, Zydis and Zycore sources.
- `tools/`: portable SDK preparation, Linux/Android builds, and source audit.
- [`docs/XBE_PORTING_COOKBOOK.md`](docs/XBE_PORTING_COOKBOOK.md): porting workflow and lessons from these builds.
- [`docs/BUILDING.md`](docs/BUILDING.md): reproducible commands and prerequisites.
- [`docs/RUNNING.md`](docs/RUNNING.md): Linux launch commands and Android installation for every title.
- [`docs/ANDROID.md`](docs/ANDROID.md): SD picker, lifecycle, controls, and device checks.

## Build and run

Install the prerequisites in [the build guide](docs/BUILDING.md), then clone:

```sh
git clone https://github.com/CrownParkComputing/xbox-recomp-suite.git
cd xbox-recomp-suite
```

For example, put your matching extracted L.A. Rush files in `ports/LARush/assets`
with `default.xbe` at that folder's root. Build and launch on a Linux desktop:

```sh
python3 tools/build_linux.py LARush --jobs 4
./ports/LARush/out/build/linux-release/LARush
```

To build Android, generate the guest code first if you skipped the desktop build:

```sh
python3 tools/build_linux.py LARush --generate-only --jobs 4
python3 tools/build_android.py LARush --jobs 4
adb install -r dist/LARush-arm64-debug.apk
adb shell am start -n com.larush.recomp/.LauncherActivity
```

Connect a physical controller and select your ISO or extracted folder on the
device. The launcher remembers that location. L.A. Rush's Android device test is
pending; see the table above for each port's validation limits.

Replace `LARush` with `Splashdown`, `Burnout3`, `Links2004`, `NFSHP2`, `PGR1`, or
`BLACK` to build another title. [The run guide](docs/RUNNING.md) lists executable
names, Android packages, ISO extraction, saves, and troubleshooting.

Dependencies can be compiled without any game data:

```sh
python3 tools/build_android.py --dependencies-only --jobs 4
```

## Provenance and licenses

This combines local work built on [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp)
and [BLACK XBE Recomp](https://github.com/TGP482/BLACK-XBERecomp), with the
source state recorded in [`SOURCE_PROVENANCE.json`](SOURCE_PROVENANCE.json).
Original credits, copyright headers, licenses, and per-project notes are retained.
See [`THIRD_PARTY.md`](THIRD_PARTY.md) for component notices. Bundling these
components does not change their individual licenses.
