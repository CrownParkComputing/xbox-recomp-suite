#!/usr/bin/env python3
"""Verify ARM64 application payloads without packaged game assets."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import struct
import zipfile

GAME_EXTENSIONS = {".xbe", ".iso", ".xiso", ".bin", ".chd", ".cue", ".cvm", ".pic", ".data", ".fatx", ".sfd", ".xpr", ".tgt", ".key"}
LIBRARIES = {"libmain.so", "libSDL3.so", "libc++_shared.so"}


def audit(path):
    with zipfile.ZipFile(path) as apk:
        if apk.testzip() is not None:
            raise ValueError("ZIP CRC failure")
        names = apk.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate APK entries")
        required = {"AndroidManifest.xml", "classes.dex", "resources.arsc"}
        if not required.issubset(names):
            raise ValueError("Missing application components")
        found = set()
        for name in names:
            entry = PurePosixPath(name)
            if entry.is_absolute() or ".." in entry.parts or entry.suffix.lower() in GAME_EXTENSIONS:
                raise ValueError("Unexpected game or unsafe entry: " + name)
            if name.endswith("/"):
                continue
            if entry.parts[0] == "lib":
                if len(entry.parts) != 3 or entry.parts[1] != "arm64-v8a" or entry.name not in LIBRARIES:
                    raise ValueError("Unexpected native library: " + name)
                header = apk.read(name)[:64]
                if len(header) < 64 or header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != 183:
                    raise ValueError("Native library is not ARM64 ELF: " + name)
                found.add(entry.name)
            elif name not in required and entry.parts[0] not in {"res", "META-INF"}:
                raise ValueError("Unexpected APK payload: " + name)
        if found != LIBRARIES:
            raise ValueError("Missing native libraries")
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    print(f"PASS: {path.name}: ARM64 application payload, no game files; SHA-256 {digest}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path, nargs="+")
    args = parser.parse_args()
    for path in args.apk:
        try:
            audit(path)
        except (OSError, ValueError, zipfile.BadZipFile) as error:
            parser.exit(1, f"APK audit failed for {path}: {error}\n")


if __name__ == "__main__":
    main()
