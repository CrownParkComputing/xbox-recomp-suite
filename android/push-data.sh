#!/usr/bin/env bash
# Install this debug APK first. Copy only this game's data into its private files.
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"
adb_cmd=("${ADB:-adb}")
[[ -z "${SERIAL:-}" ]] || adb_cmd+=(-s "$SERIAL")
package=com.splashdown.recomp
[[ -f ../assets/default.xbe ]] || { echo "Missing assets/default.xbe" >&2; exit 1; }
"${adb_cmd[@]}" shell am force-stop "$package"
"${adb_cmd[@]}" shell run-as "$package" mkdir -p files
# Stream the archive rather than keeping a second multi-gigabyte copy on the device.
tar -C .. -cf - assets | "${adb_cmd[@]}" shell -T run-as "$package" tar xf - -C files
"${adb_cmd[@]}" shell run-as "$package" du -sh files/assets
