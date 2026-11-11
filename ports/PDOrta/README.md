# Panzer Dragoon Orta — native Xbox recompilation

Experimental Linux/Vulkan port of Panzer Dragoon Orta (Xbox title ID `53450007`,
XDK 4928).

No retail executable, disc image, extracted game content, generated guest C++,
saves, APKs, or signing keys belong in the source repository. Supply your own
matching game. See [PORT_STATUS.md](PORT_STATUS.md) for actual validation and
remaining problems.

Movies, menus and active Episode 1 movement/shooting are verified on Linux,
including pause/resume. Later episodes, saves, audio fidelity and Android remain
unvalidated.

## Build from Xbox Recomp Suite

Place extracted game files under `ports/PDOrta/assets`, with `default.xbe` and
its folders together, then run from the suite root:

```sh
python3 tools/prepare.py PDOrta
python3 tools/build_linux.py PDOrta --jobs 8
./ports/PDOrta/out/build/linux-release/PDOrta
```

Orta's default.xbe uses XDK 4928, which has no SDK signature set: its
`config/game.toml` is filled from XbSymbolDatabase plus hand-proven addresses,
and `sdk/xdk/4928/layout.toml` carries the device layout proven from the image.
The bonus `PD1.xbe` (Panzer Dragoon 1) is not recompiled yet.
