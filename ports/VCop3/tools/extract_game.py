#!/usr/bin/env python3
"""Decrypt an owned VC3 GD-ROM container and extract its FATX filesystem.

Format references: mamedev/mame src/mame/sega/naomigd.cpp and
JayFoxRox/Chihiro-Tools. Requires PyCryptodome. PIC keys are never printed.
"""
import argparse
import array
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys
import zipfile


PIC_NAME = "317-0354-com.data"
PIC_SHA1 = "9f0f4bf6b15f3b6eeea81eaa27b3d25bd94110da"


def security_data(path):
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as source:
            entry = source.getinfo(PIC_NAME)
            if entry.file_size != 80:
                raise ValueError("Expected an 80-byte security response dump")
            data = source.read(entry)
    else:
        data = path.read_bytes()
    if len(data) != 80 or hashlib.sha1(data).hexdigest() != PIC_SHA1:
        raise ValueError("Security response dump does not match Virtua Cop 3")
    return data


def text(data):
    value = data.split(b"\0", 1)[0].decode("ascii")
    if not value or value in (".", "..") or any(c in value for c in "/\\:"):
        raise ValueError("Unsafe or empty filename")
    return value


def swap_blocks(data):
    blocks = array.array("Q")
    if blocks.itemsize != 8:
        raise ValueError("This host has no 64-bit array element")
    blocks.frombytes(data)
    blocks.byteswap()
    return blocks.tobytes()


def decrypt(source, destination, pic):
    from Crypto.Cipher import DES

    key = (pic[0x31:0x38] + pic[0x29:0x2a])[::-1]
    cipher = DES.new(key, DES.MODE_ECB)
    if source.stat().st_size % 8:
        raise ValueError("Encrypted container is not DES block aligned")
    encrypted_hash = hashlib.sha256()
    decrypted_hash = hashlib.sha256()
    destination.parent.mkdir(parents=True, exist_ok=True)
    first = True
    with source.open("rb") as reader, destination.open("xb") as writer:
        while block := reader.read(4 * 1024 * 1024):
            plain = swap_blocks(cipher.decrypt(swap_blocks(block)))
            if first and plain[:4] != b"FATX":
                raise ValueError("Decryption did not produce a FATX superblock")
            first = False
            encrypted_hash.update(block)
            decrypted_hash.update(plain)
            writer.write(plain)
    if first:
        raise ValueError("Empty game container")
    return encrypted_hash.hexdigest(), decrypted_hash.hexdigest()


class Fatx:
    def __init__(self, data, partition_size):
        self.data = data
        if data[:4] != b"FATX":
            raise ValueError("Missing FATX superblock")
        _, sectors, self.root = struct.unpack_from("<III", data, 4)
        if sectors == 0 or sectors & (sectors - 1) or sectors > 128:
            raise ValueError("Invalid FATX cluster size")
        self.cluster_size = sectors * 512
        self.cluster_count = partition_size // self.cluster_size + 1
        self.entry_size = 2 if self.cluster_count < 0xfff0 else 4
        self.end = 0xfff8 if self.entry_size == 2 else 0xfffffff8
        fat_size = self.cluster_count * self.entry_size
        self.data_offset = 4096 + ((fat_size + 4095) & ~4095)
        self.directories = set()
        if self.data_offset >= len(data):
            raise ValueError("FATX data area lies outside the container")

    def chain(self, cluster):
        seen = set()
        while cluster < self.end:
            if cluster < 1 or cluster >= self.cluster_count or cluster in seen:
                raise ValueError("Invalid or cyclic FATX cluster chain")
            seen.add(cluster)
            start = self.data_offset + (cluster - 1) * self.cluster_size
            if start + self.cluster_size > len(self.data):
                raise ValueError("FATX cluster lies outside the decrypted container")
            yield self.data[start:start + self.cluster_size]
            offset = 4096 + cluster * self.entry_size
            cluster = int.from_bytes(self.data[offset:offset + self.entry_size], "little")

    def extract(self, destination):
        files = []

        def directory(cluster, path, depth=0):
            if cluster in self.directories or depth > 64:
                raise ValueError("Cyclic or excessively nested FATX directory")
            self.directories.add(cluster)
            path.mkdir()
            names = set()
            for block in self.chain(cluster):
                for offset in range(0, len(block), 64):
                    entry = block[offset:offset + 64]
                    size, attributes = entry[0], entry[1]
                    if size in (0, 255):
                        return
                    if size == 0xe5:
                        continue
                    if size > 42:
                        raise ValueError("Invalid FATX filename length")
                    name = text(entry[2:2 + size])
                    if name.casefold() in names:
                        raise ValueError("Duplicate FATX filename")
                    names.add(name.casefold())
                    if attributes & 8:
                        continue
                    first, length = struct.unpack_from("<II", entry, 44)
                    target = path / name
                    if attributes & 16:
                        directory(first, target, depth + 1)
                        continue
                    digest = hashlib.sha256()
                    remaining = length
                    with target.open("xb") as output:
                        if remaining:
                            for contents in self.chain(first):
                                chunk = contents[:remaining]
                                output.write(chunk)
                                digest.update(chunk)
                                remaining -= len(chunk)
                                if not remaining:
                                    break
                    if remaining:
                        raise ValueError("FATX file chain ended before its declared length")
                    files.append({"path": target.relative_to(destination).as_posix(),
                                  "size": length, "sha256": digest.hexdigest()})

        directory(self.root, destination)
        return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pic", type=Path, required=True)
    parser.add_argument("--gdrom", type=Path, default=Path(".local/gdrom"))
    parser.add_argument("--image", type=Path, default=Path(".local/VC3.fatx"))
    parser.add_argument("--output", type=Path, default=Path("assets"))
    parser.add_argument("--dimm-size", type=int, default=512000000,
                        help="FATX partition size; default is the 512 MB media variant")
    parser.add_argument("--report", type=Path, default=Path(".local/extracted.json"))
    args = parser.parse_args()
    try:
        if args.image.exists() or args.output.exists():
            raise ValueError("Decrypted image or output already exists; refusing to overwrite")
        pic = security_data(args.pic)
        loader_name = text(pic[33:40] + pic[25:32])
        loader = (args.gdrom / loader_name).read_bytes()
        if len(loader) != 256:
            raise ValueError("Expected a 256-byte Chihiro load descriptor")
        container_name = text(loader[0xc0:0xe0])
        print(f"Decrypting {container_name} using verified VC3 security data...", flush=True)
        encrypted_hash, decrypted_hash = decrypt(args.gdrom / container_name, args.image, pic)
        with args.image.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
            fs = Fatx(data, args.dimm_size)
            files = fs.extract(args.output)
            report = {"pic_sha1": PIC_SHA1, "loader": loader_name, "container": container_name,
                      "encrypted_sha256": encrypted_hash, "decrypted_sha256": decrypted_hash,
                      "partition_size": args.dimm_size, "cluster_size": fs.cluster_size,
                      "data_offset": fs.data_offset, "files": files}
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Extracted {len(files)} files ({sum(f['size'] for f in files):,} bytes) to {args.output}")
        for entry in files:
            if entry["path"].lower().endswith(".xbe"):
                print(f"XBE: {entry['path']} ({entry['size']:,} bytes)")
    except (OSError, ValueError, KeyError, ImportError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Game extraction stopped: {error}\n")


if __name__ == "__main__":
    main()
