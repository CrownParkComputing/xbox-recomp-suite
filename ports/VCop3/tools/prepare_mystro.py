#!/usr/bin/env python3
"""Extract the supported MYSTRO Xbox ISO into the isolated variant's assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ISO_SHA256 = "9c92cc24d36b5c9cb4463b76c06be5df173ecae7b28680da34a5e0cfa3faf204"
XBE_SHA256 = "832b6b786ae44acca64ad10aa82621ce86a8e209866259e78821e3d3c396e83c"


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--suite", type=Path, help="suite with a built iso_extract tool")
    parser.add_argument("--extractor", type=Path, help="explicit iso_extract binary")
    parser.add_argument("--check", action="store_true", help="verify the ISO without extracting")
    args = parser.parse_args()
    try:
        source_hash = digest(args.iso)
        if source_hash != ISO_SHA256:
            raise ValueError("ISO differs from the image used to establish this build variant")
        if args.check:
            print("PASS: supported MYSTRO ISO SHA-256 matches")
            return
        extractor = args.extractor
        if not extractor and args.suite:
            extractor = args.suite / "ports/Splashdown/out/build/linux-release/iso_extract"
        if not extractor or not extractor.is_file():
            raise ValueError("Pass --extractor or --suite with an existing iso_extract binary")
        destination = ROOT / "assets"
        if destination.exists():
            raise ValueError("MYSTRO assets already exist; refusing to overwrite")
        destination.parent.mkdir(parents=True, exist_ok=True)
        (ROOT / ".local").mkdir(exist_ok=True)
        subprocess.run([str(extractor.resolve()), "--all", str(args.iso.resolve()), str(destination)], check=True)
        if digest(destination / "default.xbe") != XBE_SHA256:
            raise ValueError("Extracted default.xbe differs from the supported executable")
        files = [{"path": p.relative_to(destination).as_posix(), "size": p.stat().st_size,
                  "sha256": digest(p)} for p in sorted(destination.rglob("*")) if p.is_file()]
        report = {"iso": str(args.iso.resolve()), "iso_sha256": source_hash, "files": files}
        (ROOT / ".local/extracted.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"Verified {len(files)} files ({sum(f['size'] for f in files):,} bytes); default.xbe matches")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ISO preparation stopped: {error}\n")


if __name__ == "__main__":
    main()
