#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"
adb_cmd=("${ADB:-adb}")
[[ -z "${SERIAL:-}" ]] || adb_cmd+=(-s "$SERIAL")
apk=build/Splashdown-arm64-debug.apk
[[ -f "$apk" ]] || { echo "Run android/build.sh first" >&2; exit 1; }
"${adb_cmd[@]}" install -r "$apk"
"${adb_cmd[@]}" shell am start -n com.splashdown.recomp/.LauncherActivity
"${adb_cmd[@]}" logcat -v time Splashdown:V SDL:V AndroidRuntime:E libc:F '*:S'
