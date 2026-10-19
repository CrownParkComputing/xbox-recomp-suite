#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
title="${1:-Splashdown}"
case "$title" in
    Splashdown) slug=splashdown;;
    Burnout3) slug=burnout3;;
    Links2004) slug=links2004;;
    NFSHP2) slug=nfshp2;;
    BLACK) slug=black;;
    PGR1) slug=pgr1;;
    *) echo "Unknown title: $title" >&2; exit 2;;
esac
adb_command=(adb)
[[ -z "${SERIAL:-}" ]] || adb_command+=(-s "$SERIAL")
"${adb_command[@]}" install -r "dist/$title-arm64-debug.apk"
"${adb_command[@]}" shell am start -n "com.$slug.recomp/.LauncherActivity"
