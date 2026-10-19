#!/usr/bin/env python3
"""Select the exact SDK variant used by each source port."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PORTS = {"Splashdown": ("Splashdown", "splashdown", "Splashdown"),
         "Burnout3": ("Burnout 3", "burnout3", "Burnout3"),
         "Links2004": ("Links 2004", "links2004", "Links2004"),
         "NFSHP2": ("NFS Hot Pursuit 2", "nfshp2", "NFSHP2"),
         "BLACK": ("BLACK", "black", "Black"),
         "PGR1": ("Project Gotham Racing", "pgr1", "PGR1")}


def sdk_for(title):
    sdk = ROOT / "sdk"
    if title != "Splashdown":
        return sdk
    overlay = ROOT / "overlays/splashdown-sdk.patch"
    digest = hashlib.sha256(overlay.read_bytes())
    for path in sorted(sdk.rglob("*")):
        if path.is_file():
            digest.update(str(path.relative_to(sdk)).encode())
            digest.update(path.read_bytes())
    destination = ROOT / ".local/sdks" / ("splashdown-" + digest.hexdigest()[:16])
    if not (destination / ".prepared").exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + ".tmp")
        if temporary.exists():
            shutil.rmtree(temporary)
        shutil.copytree(sdk, temporary)
        subprocess.run(["git", "apply", "--check", str(overlay)], cwd=temporary, check=True)
        subprocess.run(["git", "apply", str(overlay)], cwd=temporary, check=True)
        (temporary / ".prepared").write_text(digest.hexdigest() + "\n")
        temporary.rename(destination)
    return destination


def prepare(title):
    sdk = sdk_for(title)
    link = ROOT / "ports" / title / "sdk"
    if link.is_symlink():
        if link.resolve() != sdk.resolve():
            link.unlink()
    elif link.exists():
        raise RuntimeError(f"{link} is not a managed link; move it before preparing this port")
    if not link.exists():
        import os
        link.symlink_to(os.path.relpath(sdk, link.parent), target_is_directory=True)
    return sdk


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("title", choices=["all"] + list(PORTS), default="all", nargs="?")
    args = parser.parse_args()
    for title in PORTS if args.title == "all" else [args.title]:
        print(f"{title}: {prepare(title).relative_to(ROOT)}")
