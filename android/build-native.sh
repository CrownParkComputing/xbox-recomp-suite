#!/usr/bin/env bash
# The suite builds native code and packages the result together.
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
exec python3 tools/build_android.py Splashdown "$@"
