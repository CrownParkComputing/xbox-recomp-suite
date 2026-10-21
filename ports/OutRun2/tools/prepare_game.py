#!/usr/bin/env python3
"""Extract the matching local OutRun 2 Chihiro ZIP without overwriting files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

XBE_SHA256 = "d98a7efc0ee4ba48b6e938743fa309a9fc774c05713ef735e67d1b17b9bbbd1b"


def extract(archive, output):
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; refusing to overwrite")
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        names = set()
        for entry in entries:
            path = PurePosixPath(entry.filename)
            if (not path.parts or path.is_absolute() or ".." in path.parts or
                    "\\" in entry.filename or ":" in entry.filename or
                    (entry.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError("Unsafe archive entry")
            normalized = str(path).casefold()
            if normalized in names:
                raise ValueError("Duplicate archive entry")
            names.add(normalized)
        xbe = source.getinfo("outrun2.xbe")
        if xbe.file_size != 2306048 or hashlib.sha256(source.read(xbe)).hexdigest() != XBE_SHA256:
            raise ValueError("Executable does not match this OutRun 2 port")
        output.mkdir(parents=True)
        files = []
        for entry in entries:
            target = output.joinpath(*PurePosixPath(entry.filename).parts)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            with source.open(entry) as reader, target.open("xb") as writer:
                while block := reader.read(1024 * 1024):
                    writer.write(block)
                    digest.update(block)
            files.append({"path": entry.filename, "size": entry.file_size,
                          "sha256": digest.hexdigest()})
        return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, default=Path("assets"))
    parser.add_argument("--report", type=Path, default=Path(".local/extracted.json"))
    args = parser.parse_args()
    try:
        files = extract(args.archive, args.output)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"files": files}, indent=2) + "\n")
        print(f"Extracted {len(files)} files; all ZIP CRCs checked")
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Preparation stopped: {error}\n")


if __name__ == "__main__":
    main()
