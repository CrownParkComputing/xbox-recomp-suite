# Writes src/common/europe.patch, which turns the Europe default.xbe into the US one this build is recompiled from.
# python tools/europe_patch.py <Europe default.xbe> <US default.xbe>   (pip install bsdiff4)
import sys
import bsdiff4.core


def varint(n):
    out = bytearray()
    while n >= 0x80:
        out.append(n & 0x7F | 0x80)
        n >>= 7
    out.append(n)
    return out


def main(europe_path, us_path):
    europe = open(europe_path, "rb").read()
    us = open(us_path, "rb").read()
    control, diff, extra = bsdiff4.core.diff(europe, us)
    patch = bytearray()
    at = 0
    extra_at = 0
    for copy, add, seek in control:
        # bytes added to the Europe ones, as (distance from the last, byte) for the non zero ones
        changes = [i for i in range(copy) if diff[at + i]]
        patch += varint(copy) + varint(len(changes))
        last = 0
        for i in changes:
            patch += varint(i - last) + bytes([diff[at + i]])
            last = i + 1
        patch += varint(add) + extra[extra_at:extra_at + add]
        patch += varint(seek * 2 if seek >= 0 else -seek * 2 - 1)
        at += copy
        extra_at += add
    open("src/common/europe.patch", "wb").write(patch)
    print(len(control), "blocks,", len(patch), "bytes")


main(sys.argv[1], sys.argv[2])
