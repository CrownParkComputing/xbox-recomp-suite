#!/usr/bin/env python3
"""Run pause, clock, IRQL and limiter regressions without a game or GPU."""
import os
from pathlib import Path
import subprocess
from prepare import ROOT

sdk = ROOT / "sdk"
out = ROOT / ".local/tests"
out.mkdir(parents=True, exist_ok=True)
sources = [sdk / "tests" / name for name in (
    "main.cpp", "host_memory.cpp", "irql.cpp", "test_audio_limiter.cpp", "test_host_pause.cpp")]
exe = out / "host_runtime_tests"
subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++20", "-O2", "-pthread",
    "-I", str(sdk / "runtime"), "-I", str(sdk / "tests"),
    *map(str, sources), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
