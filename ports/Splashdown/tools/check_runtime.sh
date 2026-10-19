#!/usr/bin/env bash
# Requires generated/game and the local SDK. Runs without a window or GPU.
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
repo_dir="$PWD"
smoke_dir="$(mktemp -d /tmp/splash-runtime-smoke.XXXXXX)"
trap 'rm -rf -- "$smoke_dir"' EXIT
export JOBS="${JOBS:-6}"
# build.sh tracks headers but not .cpp files included by a test replacement.
# Use a fresh wrapper so each check compiles the current runtime implementation.
threaded_runtime="$smoke_dir/threaded_d3d.cpp"
printf '#undef XBE_RENDER_THREAD\n#define XBE_RENDER_THREAD 1\n#include "%s"\n' \
    "$repo_dir/sdk/runtime/xdk/d3d8.cpp" > "$threaded_runtime"
build=(bash sdk/harness/build.sh -r . -g generated/game -o out/harness
    -x '/gpu/vk_|/gpu/vulkan|/audio/sdl_audio|/core/keyboard_pad')
"${build[@]}" -n splashdown-input-smoke -e tools/input_smoke.cpp
(
    cd "$smoke_dir"
    "$repo_dir/out/harness/splashdown-input-smoke" "$repo_dir/assets"
)
"${build[@]}" -n splashdown-render-states-smoke -e tools/render_states_smoke.cpp
(
    cd "$smoke_dir"
    "$repo_dir/out/harness/splashdown-render-states-smoke" "$repo_dir/assets"
)
"${build[@]}" -n splashdown-gamma-smoke -e tools/gamma_smoke.cpp
(
    cd "$smoke_dir"
    "$repo_dir/out/harness/splashdown-gamma-smoke" "$repo_dir/assets"
)
"${build[@]}" -n splashdown-audio-smoke -e tools/audio_smoke.cpp
(
    cd "$smoke_dir"
    "$repo_dir/out/harness/splashdown-audio-smoke" "$repo_dir/assets"
)
"${build[@]}" -n splashdown-threaded-smoke -x '/xdk/d3d8.cpp' \
    -e tools/indexed_draw_smoke.cpp -e "$threaded_runtime"
(
    cd "$smoke_dir"
    REF=1 "$repo_dir/out/harness/splashdown-threaded-smoke" "$repo_dir/assets"
)
"${build[@]}" -n splashdown-shader-reload-smoke -x '/xdk/d3d8.cpp' \
    -e tools/shader_reload_smoke.cpp -e "$threaded_runtime"
(
    cd "$smoke_dir"
    REF=1 "$repo_dir/out/harness/splashdown-shader-reload-smoke" "$repo_dir/assets"
)
