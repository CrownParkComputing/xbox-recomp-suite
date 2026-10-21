# Xbox Recomp Suite

Source snapshots for ten original Xbox/Chihiro native ports, their Linux/Vulkan and
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
| PGR1 | SD folder boot; intro plays; Linux multi-car audio routing checked | Full Android race graphics/performance and listening comparison remain open |
| BLACK | SD folder boot; intro and title screen | Full gameplay/performance not validated here |
| L.A. Rush | SD boot, intro/title, Story free roam and fresh-process relaunch verified; desktop Story driving verified | Full races, missions and sustained performance remain unvalidated |
| Virtua Cop 3 (MYSTRO) | ARM64 APK builds; mouse/touch input tests pass | Android device boot/gameplay unverified; Linux gameplay and stage previews verified |
| The House of the Dead III | ARM64 APK builds; mouse/touch input tests pass | Android device boot/gameplay unverified; Linux first-stage mouse gun verified |
| OutRun 2 (Chihiro 2.000) | ARM64 APK builds with touch driving controls | Android device boot/gameplay unverified; Linux first-course driving verified |

The shared Android launcher remembers the selected SD ISO/folder, skips the
picker while it remains accessible, and ends the native game process on
minimize/close. SD boot, remembered selection and fresh-process restart were
device-tested across all seven titles. L.A. Rush's former local
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
device. The three Chihiro ports also provide touch controls. The launcher
remembers that location. See the table above for each
port's validation limits.

Replace `LARush` with `Splashdown`, `Burnout3`, `Links2004`, `NFSHP2`, `PGR1`, or
`BLACK`, `VCop3`, `HOTD3`, or `OutRun2` to build another title.
[The run guide](docs/RUNNING.md) lists executable
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

R3 opens settings and pauses gameplay, game clocks and audio on Linux and Android. R3 closes the panel and resumes; F10 also works on Linux. BLACK keeps its own panel with the same pause behaviour.
