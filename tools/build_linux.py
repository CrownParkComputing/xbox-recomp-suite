#!/usr/bin/env python3
"""Configure and build a source port, or regenerate its private guest C++."""
import argparse
import os
from pathlib import Path
import subprocess
from prepare import ROOT, PORTS, prepare, image_for, verify_image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("title", choices=PORTS)
parser.add_argument("--generate-only", action="store_true")
parser.add_argument("--tools-only", action="store_true")
parser.add_argument("--jobs", default="3")
parser.add_argument("--debug", action="store_true")
args = parser.parse_args()
prepare(args.title)
source = ROOT / "ports" / args.title
preset = "linux-debug" if args.debug else "linux-release"
subprocess.run(["cmake", "--preset", preset], cwd=source, check=True)
if args.tools_only:
    targets = ["xbe_recomp", "xbe_symbols", "iso_extract"]
else:
    if not (source / "assets" / image_for(args.title)).exists():
        parser.error("Put your own extracted game in ports/" + args.title + "/assets first (or use --tools-only)")
    verify_image(args.title)
    target = PORTS[args.title][2]
    targets = ["recompile_" + target] if args.generate_only else [target]
subprocess.run(["cmake", "--build", "--preset", preset, "--target"] + targets + ["-j", args.jobs], cwd=source, check=True)
