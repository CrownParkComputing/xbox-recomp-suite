#!/usr/bin/env python3
"""Check BLACK's real mixer routing and surround folding without a device."""
from pathlib import Path
import subprocess

port = Path(__file__).resolve().parents[1]
sdk = port / "sdk"
out = port / "out/audio-smoke"
out.mkdir(parents=True, exist_ok=True)
exe = out / "BlackAudioSmoke"
sources = [sdk / "tests/main.cpp", sdk / "tests/host_memory.cpp",
    port / "tests/test_mixer_routes.cpp", port / "tests/test_audio.cpp",
    port / "src/sound/mixer.cpp", port / "src/audio/fold.cpp"]
subprocess.run(["clang++", "-std=c++20", "-O1", "-pthread", "-ffunction-sections",
    "-fdata-sections", "-Wl,--gc-sections", "-I", str(sdk / "runtime"),
    "-I", str(sdk / "tests"), "-I", str(port / "src"), "-I", str(port / "generated/game"),
    *map(str, sources), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
