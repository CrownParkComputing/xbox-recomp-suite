#!/usr/bin/env python3
"""Inspect a zipped Chihiro BIN/CUE disc and optionally extract its ISO root.

This only removes raw-sector framing. Encrypted game containers stay encrypted;
it does not produce an XBE or bypass the need for the matching PIC data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile


RAW_SECTOR = 2352
DATA_OFFSET = 16
DATA_SECTOR = 2048


def exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Truncated disc data")
    return data


def bcd(value):
    if value & 15 > 9 or value >> 4 > 9:
        raise ValueError("Invalid raw-sector MSF address")
    return (value >> 4) * 10 + (value & 15)


def both32(data, offset):
    little = int.from_bytes(data[offset:offset + 4], "little")
    big = int.from_bytes(data[offset + 4:offset + 8], "big")
    if little != big:
        raise ValueError("ISO9660 byte-order copies disagree")
    return little


def records(data):
    position = 0
    while position < len(data):
        length = data[position]
        if length == 0:
            position = (position // DATA_SECTOR + 1) * DATA_SECTOR
            continue
        if length < 34 or position + length > len(data):
            raise ValueError("Invalid ISO9660 directory record")
        record = data[position:position + length]
        position += length
        name_length = record[32]
        if 33 + name_length > len(record):
            raise ValueError("Truncated ISO9660 filename")
        raw_name = record[33:33 + name_length]
        if raw_name in (b"\0", b"\1"):
            continue
        name = raw_name.decode("ascii").split(";", 1)[0]
        if not name or name in (".", "..") or any(c in name for c in "/\\:"):
            raise ValueError("Unsafe ISO9660 filename")
        if record[25] & 0x80:
            raise ValueError("Multi-extent ISO9660 files are unsupported")
        yield {"name": name, "lba": both32(record, 2),
               "size": both32(record, 10), "directory": bool(record[25] & 2)}


def payload(stream, entry, start_lba, track_size):
    sector = entry["lba"] - start_lba
    count = (entry["size"] + DATA_SECTOR - 1) // DATA_SECTOR
    if sector < 0 or (sector + count) * RAW_SECTOR > track_size:
        raise ValueError("ISO9660 extent lies outside the data track")
    stream.seek(sector * RAW_SECTOR)
    remaining = entry["size"]
    while count:
        batch = min(count, 1024)
        raw = exact(stream, batch * RAW_SECTOR)
        data = b"".join(raw[i + DATA_OFFSET:i + DATA_OFFSET + DATA_SECTOR]
                        for i in range(0, len(raw), RAW_SECTOR))
        yield data[:remaining]
        remaining -= min(len(data), remaining)
        count -= batch


def inspect(archive, extract=None):
    with zipfile.ZipFile(archive) as disc:
        cues = [entry for entry in disc.infolist() if entry.filename.lower().endswith(".cue")]
        if len(cues) != 1 or cues[0].file_size > 65536:
            raise ValueError("Expected one small CUE sheet")
        cue = disc.read(cues[0]).decode("utf-8-sig")
        match = re.search(r'FILE\s+"([^"\r\n]+)"\s+BINARY\s+TRACK\s+03\s+MODE1/2352',
                          cue, re.IGNORECASE)
        if not match:
            raise ValueError("Expected track 03 in MODE1/2352 format")
        cue_dir = Path(cues[0].filename).parent
        track_name = (cue_dir / match[1]).as_posix()
        track = disc.getinfo(track_name)
        if track.file_size % RAW_SECTOR:
            raise ValueError("Data track is not a whole number of raw sectors")
        with disc.open(track) as stream:
            initial = exact(stream, RAW_SECTOR)
            if initial[:12] != b"\0" + b"\xff" * 10 + b"\0" or initial[15] != 1:
                raise ValueError("Expected raw MODE1 sector framing")
            start_lba = ((bcd(initial[12]) * 60 + bcd(initial[13])) * 75
                         + bcd(initial[14]) - 150)
            boot = initial[DATA_OFFSET:DATA_OFFSET + DATA_SECTOR]
            stream.seek(16 * RAW_SECTOR + DATA_OFFSET)
            pvd = exact(stream, DATA_SECTOR)
            if pvd[:7] != b"\x01CD001\x01":
                raise ValueError("Expected ISO9660 primary volume descriptor")
            root_record = pvd[156:190]
            root = {"lba": both32(root_record, 2), "size": both32(root_record, 10)}
            if root["size"] > 16 * 1024 * 1024:
                raise ValueError("Unexpectedly large root directory")
            entries = list(records(b"".join(payload(stream, root, start_lba, track.file_size))))
            if len({e["name"].casefold() for e in entries}) != len(entries):
                raise ValueError("Duplicate root filenames")
            result = {
                "archive": str(archive.resolve()),
                "cue": cues[0].filename,
                "data_track": track_name,
                "data_track_crc32": f"{track.CRC:08x}",
                "data_track_start_lba": start_lba,
                "volume": pvd[40:72].decode("ascii").strip(),
                "product": boot[64:74].decode("ascii").strip(),
                "version": boot[74:80].decode("ascii").strip(),
                "release_date": boot[80:88].decode("ascii").strip(),
                "title": boot[128:256].decode("ascii").strip(),
                "entries": entries,
                "executable_extracted": False,
            }
            if extract:
                if any(e["directory"] for e in entries):
                    raise ValueError("Extraction supports flat GD-ROM roots only")
                if extract.exists():
                    raise ValueError(f"Output already exists; refusing to overwrite: {extract}")
                extract.mkdir(parents=True)
                for entry in sorted(entries, key=lambda e: e["lba"]):
                    digest = hashlib.sha256()
                    output = extract / entry["name"]
                    with output.open("xb") as target:
                        for block in payload(stream, entry, start_lba, track.file_size):
                            target.write(block)
                            digest.update(block)
                    entry["sha256"] = digest.hexdigest()
                    print(f"Extracted {entry['name']}: {entry['size']:,} bytes", file=sys.stderr)
                # Consume the remaining raw sectors to validate the ZIP track CRC.
                while stream.read(4 * 1024 * 1024):
                    pass
                result["track_zip_crc_verified"] = True
            return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--extract", type=Path, help="new local directory for encrypted containers")
    parser.add_argument("--report", type=Path, help="write a private JSON inspection report")
    args = parser.parse_args()
    try:
        report = json.dumps(inspect(args.archive, args.extract), indent=2) + "\n"
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(report)
        print(report, end="")
    except (OSError, ValueError, KeyError, UnicodeError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Disc preparation stopped: {error}\n")


if __name__ == "__main__":
    main()
