#!/usr/bin/env python3
"""Compatibility entry point; the portable suite build lives under tools/."""
from pathlib import Path
import runpy
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/build_android.py"), run_name="__main__")
