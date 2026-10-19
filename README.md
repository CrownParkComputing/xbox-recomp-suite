# Xbox Recomp Suite

Source snapshots for six original Xbox native ports, their Linux/Vulkan and
Android tooling, the shared Xbox XBE Recomp runtime, and the porting cookbook.
SDL and the other source dependencies are vendored with their upstream licenses
and exact revisions in [`DEPENDENCIES.lock.json`](DEPENDENCIES.lock.json).

**No retail game files, extracted content, ISOs/XBEs, generated game C++, saves,
APKs, or signing keys are included.** Supply your own matching game files to
build a port. This repository records experimental conversions; booting an intro
is not evidence of complete gameplay support.

| Port | Android evidence at this snapshot | Main limitation |
| --- | --- | --- |
| Splashdown | SD ISO boot and native race; menu and spray fixes installed | Latest spray fix verified in a native race capture; handheld confirmation pending; DSP/reverb incomplete; missing Career opponents reported after skipping qualifying |
| Burnout 3 | SD folder boot; loading completes and EA intro plays | Full gameplay/performance not revalidated here |
| Links 2004 | SD folder boot; intro plays | Full gameplay/performance not revalidated here |
| NFS Hot Pursuit 2 | SD folder boot; intro plays | Pause/standings text alignment remains open |
| PGR1 | SD folder boot; intro plays | Full race graphics/performance not validated here |
| BLACK | SD folder boot; intro and title screen | Full gameplay/performance not validated here |

All six Android builds remember the selected SD ISO/folder, skip the picker while
it remains accessible, and end the native game process on minimize/close.
SFRush is intentionally excluded.

## Layout

- `sdk/`: the shared runtime, recompiler, XDK layouts, tests, and templates.
- `ports/<title>/`: per-game configuration, native hooks, and original notes.
- `overlays/splashdown-sdk.patch`: Splashdown's isolated tested SDK changes.
- `android/`: shared SDL Java, launcher, document-tree JNI, shader adapter, and APK packaging.
- `third_party/`: pinned SDL, glslang, SPIRV-Tools/Headers, ImGui, toml++, VMA, Zydis and Zycore sources.
- `tools/`: portable SDK preparation, Linux/Android builds, and source audit.
- [`docs/XBE_PORTING_COOKBOOK.md`](docs/XBE_PORTING_COOKBOOK.md): porting workflow and lessons from these builds.
- [`docs/BUILDING.md`](docs/BUILDING.md): reproducible commands and prerequisites.
- [`docs/ANDROID.md`](docs/ANDROID.md): SD picker, lifecycle, controls, and device checks.

## Build

On Linux, provide an extracted game under `ports/<title>/assets`, including its
matching `default.xbe`, then run:

```sh
python3 tools/prepare.py all
python3 tools/build_linux.py Splashdown --generate-only
python3 tools/build_android.py Splashdown --jobs 4
```

The APK appears in `dist/Splashdown-arm64-debug.apk`. To build another title,
replace `Splashdown` with `Burnout3`, `Links2004`, `NFSHP2`, `PGR1`, or `BLACK`.
For a native desktop executable, omit `--generate-only` from the Linux command.
Install the prerequisites in [the build guide](docs/BUILDING.md) first.

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
