#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/out/build/linux-release"
# Normal movie playback, with runtime details retained in logs/splashdown.log.
exec env -u SPLASH_SKIP_MOVIES -u SPLASH_GUARD_NULL_186F16 -u SPLASH_GUARD_NULL_1804E8 \
    XBE_LOG_VERBOSE=1 ./Splashdown "$@"
