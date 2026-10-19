#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
exec python3 tools/build_android.py Splashdown "$@"
