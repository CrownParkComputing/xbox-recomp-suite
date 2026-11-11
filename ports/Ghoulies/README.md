# Grabbed by the Ghoulies — native Xbox recompilation

Experimental Linux/Vulkan port of Grabbed by the Ghoulies (Xbox title ID `4D530053`,
XDK 5659).

No retail executable, disc image, extracted game content, generated guest C++,
saves, APKs, or signing keys belong in the source repository. Supply your own
matching game. See [PORT_STATUS.md](PORT_STATUS.md) for actual validation and
remaining problems.

The Linux intro renders animated smoke, coloured book covers and opening pages.
Gameplay, menus, saves, audio and Android remain unvalidated.

## Build from Xbox Recomp Suite

Place extracted game files under `ports/Ghoulies/assets`, with `default.xbe` and
its folders beside it, then run from the suite root:

```sh
python3 tools/prepare.py Ghoulies
python3 tools/build_linux.py Ghoulies --jobs 8
./ports/Ghoulies/out/build/linux-release/Ghoulies
```

Ghoulies uses XDK 5659, which has no SDK signature set: its `config/game.toml` is
filled from XbSymbolDatabase plus hand-proven addresses, and `sdk/xdk/5659/layout.toml`
carries the device layout proven from this image. The disc's second executable,
`cdx.xbe` (the CDX bonus-video player, XSNDTRK/XMV), is not recompiled.
