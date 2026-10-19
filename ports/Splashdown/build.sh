#!/usr/bin/env bash
# A game's Linux build (build.bat on Windows): set GAME_TITLE, GAME_XBE and GAME_EXE below.
# What it does: the toolchain checked, the game found in assets (its files, or
# a disc image extracted there), the config's XDK symbols written when it has none, then the game recompiled, built
# and its tests run.
#   ./build.sh          release build
#   ./build.sh debug    with debug info
set -euo pipefail
GAME_TITLE="Splashdown - XBE Recomp"
GAME_XBE=default.xbe
GAME_EXE=Splashdown
cd "$(dirname "$(realpath "$0")")"
PRESET=linux-release
[ "${1:-}" = debug ] && PRESET=linux-debug
OUT=out/build/$PRESET

echo
echo "  $GAME_TITLE"
echo "  =================="
echo

fail() {
    echo
    echo "  BUILD FAILED. $*"
    echo
    exit 1
}

# ---------------------------------------------------------------- toolchain
echo "[1/4] Toolchain"
missing=()
for tool in clang++ cmake ninja git ld.lld pkg-config; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if command -v pkg-config >/dev/null; then
    pkg-config --exists sdl3 || missing+=("SDL3")
    pkg-config --exists vulkan || missing+=("Vulkan loader")
fi
if ((${#missing[@]})); then
    echo "      missing: ${missing[*]}"
    echo "      Arch:          sudo pacman -S --needed clang lld cmake ninja git pkgconf sdl3 vulkan-headers vulkan-icd-loader"
    echo "      Debian/Ubuntu: sudo apt install clang lld cmake ninja-build git pkg-config libsdl3-dev libvulkan-dev"
    echo "      Fedora:        sudo dnf install clang lld cmake ninja-build git pkgconf SDL3-devel vulkan-headers vulkan-loader-devel"
    echo "      plus your GPU's Vulkan driver (Mesa's vulkan-radeon / vulkan-intel, or NVIDIA's)"
    fail "Install the missing tools, then run this again."
fi
echo "      $(clang++ --version | head -1)"

# ---------------------------------------------------------------- the game
echo "[2/4] Game files"
mkdir -p assets
if [ ! -f "assets/$GAME_XBE" ]; then
    iso=$(find assets -maxdepth 1 -iname '*.iso' -type f | sort | head -1)
    [ -n "$iso" ] || {
        echo "      Put the game in the assets folder: its disc image (.iso), or $GAME_XBE with its folders beside it."
        fail "No game in $(realpath assets)."
    }
    # the recompiler needs the xbe as a file, and the game then runs from the files beside it: the whole disc out
    size=$(du -h "$iso" | cut -f1)
    echo "      extracting $(basename "$iso") ($size) into assets; the image may be deleted afterwards"
    cmake --preset "$PRESET" >/dev/null || fail "CMake could not configure."
    cmake --build --preset "$PRESET" --target iso_extract >/dev/null || fail "iso_extract did not build."
    "$OUT/iso_extract" --all "$iso" assets || fail "$(basename "$iso") could not be extracted."
    [ -f "assets/$GAME_XBE" ] || fail "$(basename "$iso") has no $GAME_XBE at its root."
fi
echo "      assets/$GAME_XBE"

# ---------------------------------------------------------------- symbols
echo "[3/4] XDK symbols"
cmake --preset "$PRESET" >/dev/null || fail "CMake could not configure."
if grep -q '^\[symbols\]' config/game.toml 2>/dev/null; then
    echo "      config/game.toml already written"
else
    cmake --build --preset "$PRESET" --target xbe_symbols >/dev/null || fail "xbe_symbols did not build."
    [ -f "$OUT/sdk_dir.txt" ] || fail "$OUT/sdk_dir.txt was not written, update the SDK."
    SDK=$(head -1 "$OUT/sdk_dir.txt")
    "$OUT/xbe_symbols" "assets/$GAME_XBE" "$SDK/xdk" --out config/game.toml || fail "xbe_symbols failed."
    cmake --preset "$PRESET" >/dev/null
fi
# argument registers xbe_symbols could not order are left as "?": the recompiler needs them filled in by hand
if grep -n '^[A-Za-z_0-9]* *= *\[.*"?"' config/game.toml; then
    echo "      These [abi] entries in config/game.toml need their argument locations in API order: each comment says"
    echo "      which registers the function reads and how many stack bytes it pops (4 per \"stack\" argument)."
    fail "Fill in the [abi] entries above, then run this again."
fi

# ---------------------------------------------------------------- game
echo "[4/4] Game code"
echo "      the first build recompiles $GAME_XBE: 30 to 90 minutes on weaker systems ..."
cmake --build --preset "$PRESET" || fail "Read the last message above."
ctest --preset linux-release --output-on-failure >/dev/null || echo "      warning: the tests failed, run: ctest --preset linux-release"

echo
echo "  $GAME_TITLE built."
echo "  $OUT/$GAME_EXE"
echo
if [ -t 0 ]; then
    read -r -p "  Play now? [y/N] " answer
    [[ "$answer" =~ ^[Yy] ]] && (cd "$OUT" && exec "./$GAME_EXE")
fi
exit 0
