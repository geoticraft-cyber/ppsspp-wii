#!/usr/bin/env python3
"""Create an original, import-free PSP framebuffer smoke ELF using only Python.

Expected output is a solid red framebuffer. This is a CPU/load/display check,
not a GE, audio, or compatibility test. No PSP firmware or game data is used.
"""
import argparse
from pathlib import Path
import struct


def make_elf():
    base = 0x08804000
    # lui t0,0x0400; lui t1,0x0408; ori t1,t1,0x8000
    # lui t2,0xff00; ori t2,t2,0x00ff
    # fill: sw t2,0(t0); addiu t0,t0,4; bne t0,t1,fill; nop
    # halt: j halt; nop
    words = [0x3C080400, 0x3C090408, 0x35298000, 0x3C0AFF00, 0x354A00FF,
             0xAD0A0000, 0x25080004, 0x1509FFFD, 0,
             0x08000000 | ((base + 9 * 4) >> 2), 0]
    code = struct.pack("<11I", *words)
    module = struct.pack("<HH28s5I", 0, 0x0100, b"WiiFramebufferSmoke", 0, 0, 0, 0, 0)
    names = b"\0.text\0.rodata.sceModuleInfo\0.shstrtab\0"
    data = bytearray(0x240 + 4 * 40)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    data[:52] = struct.pack("<16sHH5I6H", ident, 2, 8, 1, base, 52, 0x240,
                            0x10001001, 52, 32, 1, 40, 4, 3)
    data[52:84] = struct.pack("<8I", 1, 0x100, base, 0x180, 0x80 + len(module),
                              0x80 + len(module), 5, 0x100)
    data[0x100:0x100 + len(code)] = code
    data[0x180:0x180 + len(module)] = module
    data[0x200:0x200 + len(names)] = names
    sections = [
        (0,) * 10,
        (names.index(b".text"), 1, 6, base, 0x100, len(code), 0, 0, 4, 0),
        (names.index(b".rodata"), 1, 2, base + 0x80, 0x180, len(module), 0, 0, 4, 0),
        (names.index(b".shstrtab"), 3, 0, 0, 0x200, len(names), 0, 0, 1, 0),
    ]
    for index, section in enumerate(sections):
        start = 0x240 + index * 40
        data[start:start + 40] = struct.pack("<10I", *section)
    return bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(make_elf())
    print(f"Wrote {args.output} ({args.output.stat().st_size} bytes); expected framebuffer: red")
