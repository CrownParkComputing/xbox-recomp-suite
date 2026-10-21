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
         "PGR1": ("Project Gotham Racing", "pgr1", "PGR1"),
         "LARush": ("L.A. Rush", "larush", "LARush"),
         "VCop3": ("Virtua Cop 3", "vcop3", "VCop3"),
         "HOTD3": ("The House of the Dead III", "hotd3", "HOTD3"),
         "OutRun2": ("OutRun 2", "outrun2", "OutRun2")}

CHIHIRO = {
    "VCop3": {"xbe": "default.xbe", "sha256": "832b6b786ae44acca64ad10aa82621ce86a8e209866259e78821e3d3c396e83c",
              "input": "VCOP3_SDL_INPUT", "controls": "gun-vcop3", "folder_only": False},
    "HOTD3": {"xbe": "hod3xb.xbe", "sha256": "89a0e7778002205c114a91bc1797291344b2afeea8b5c7d5085cce7e47dc71de",
              "input": "HOTD3_SDL_INPUT", "controls": "gun-hotd3", "folder_only": True},
    "OutRun2": {"xbe": "outrun2.xbe", "sha256": "d98a7efc0ee4ba48b6e938743fa309a9fc774c05713ef735e67d1b17b9bbbd1b",
               "input": "OUTRUN2_SDL_INPUT", "controls": "driving", "folder_only": True},
}


def image_for(title):
    return CHIHIRO.get(title, {}).get("xbe", "default.xbe")


def verify_image(title):
    image = ROOT / "ports" / title / "assets" / image_for(title)
    if title in CHIHIRO:
        with image.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != CHIHIRO[title]["sha256"]:
            raise ValueError(f"{title}: {image.name} does not match this port's executable")
    return image


def sdk_for(title):
    sdk = ROOT / "sdk"
    if title not in {"Splashdown", "LARush"} and title not in CHIHIRO:
        return sdk
    slug = title.lower()
    if title in CHIHIRO:
        slug = "chihiro"
        overlays = [ROOT / "overlays/chihiro" / name for name in (
            "chihiro-memory.patch", "flush-guest-push.patch", "buffer-envelope.patch",
            "indexed-up.patch", "controller-capabilities.patch", "target-alpha.patch")]
    else:
        overlays = [ROOT / "overlays" / (slug + "-sdk.patch")]
    # Keep existing retail-port cache identities when adding a new family.
    digest = hashlib.sha256(overlays[0].read_bytes()) if len(overlays) == 1 else hashlib.sha256()
    if len(overlays) > 1:
        for overlay in overlays:
            digest.update(overlay.name.encode())
            digest.update(overlay.read_bytes())
    for path in sorted(sdk.rglob("*")):
        if path.is_file():
            digest.update(str(path.relative_to(sdk)).encode())
            digest.update(path.read_bytes())
    destination = ROOT / ".local/sdks" / (slug + "-" + digest.hexdigest()[:16])
    if not (destination / ".prepared").exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + ".tmp")
        if temporary.exists():
            shutil.rmtree(temporary)
        shutil.copytree(sdk, temporary)
        for overlay in overlays:
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
