#!/usr/bin/env python3
"""Compile and execute a synthetic XBE to test segment PUSH/POP stack widths.
Usage: python3 tools/recomp/test_segment_stack.py /path/to/xbe_recomp
Requires clang++; no game data is used.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[2]
recompiler = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='segment-stack-') as temp:
    root = pathlib.Path(temp)
    image = bytearray(0x1100)
    image[:4] = b'XBEH'
    fields = {0x104: 0x10000, 0x108: 0x1000, 0x10c: 0x2000,
              0x118: 0x10300, 0x11c: 1, 0x120: 0x10200,
              0x128: 0x11000 ^ 0xa8fc57ab, 0x158: 0x10380 ^ 0x5b6d40b6,
              0x200: 4, 0x204: 0x11000, 0x208: 0x100, 0x20c: 0x1000,
              0x210: 0x100, 0x214: 0x10280}
    for offset, value in fields.items():
        struct.pack_into('<I', image, offset, value)
    image[0x280:0x286] = b'.text\0'
    # Save EBX/ESI/EDI. Push DS/ES/FS/GS and record ESP in EAX, pop them.
    # Repeat DS/ES with operand-size overrides, record ESP in EDX, restore.
    code = bytes.fromhex('53 56 57 1e 06 0fa0 0fa8 8bc4 0fa9 0fa1 07 1f '
                         '66 1e 66 06 8bd4 66 07 66 1f 5f 5e 5b c3')
    image[0x1000:0x1000+len(code)] = code
    (root/'fixture.xbe').write_bytes(image)
    (root/'test.toml').write_text('[xbe]\npartitions=1\ncode_sections=[".text"]\n')
    subprocess.run([str(recompiler), str(root/'test.toml'), str(root/'fixture.xbe'),
                    str(root/'generated')], check=True)
    (root/'check.cpp').write_text('''
#include "cpu/x86_ops.h"
#include <cassert>
#include <vector>
X86_EXTERN_FUNC(sub_00011000);
int main() {
    std::vector<uint8_t> mem(0x20000);
    X86Context ctx{};
    ctx.esp=0x18000; ctx.ebx=0x11223344; ctx.esi=0x55667788; ctx.edi=0x99aabbcc;
    sub_00011000(ctx, mem.data());
    assert(ctx.eax==0x18000-28); // 3 GPR slots + 4 default-width selector slots
    assert(ctx.edx==0x18000-16); // 3 GPR slots + 2 operand-size-overridden slots
    assert(ctx.esp==0x18004);
    assert(ctx.ebx==0x11223344 && ctx.esi==0x55667788 && ctx.edi==0x99aabbcc);
}
''')
    subprocess.run(['clang++', '-std=c++20', '-I', str(sdk/'runtime'),
                    str(root/'check.cpp'), str(root/'generated/x86_recomp.0.cpp'),
                    '-o', str(root/'check')], check=True)
    subprocess.run([str(root/'check')], check=True)
print('segment stack regression passed')
