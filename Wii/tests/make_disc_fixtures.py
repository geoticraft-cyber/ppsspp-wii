#!/usr/bin/env python3
"""Build original PSP smoke ISO/CSO fixtures; requires pycdlib, no game data."""
import argparse
import io
from pathlib import Path
import struct
import zlib
import pycdlib
from make_smoke_elf import make_elf

def sfo():
    values = {"BOOTABLE": 1, "CATEGORY": "UG", "DISC_ID": "HBWI00001",
              "PSP_SYSTEM_VER": "1.00", "TITLE": "Wii PSP framebuffer smoke"}
    keys, data, entries = bytearray(), bytearray(), []
    for name, value in sorted(values.items()):
        raw = struct.pack("<I", value) if isinstance(value, int) else value.encode() + b"\0"
        while len(data) % 4: data.append(0)
        entries.append(struct.pack("<HHIII", len(keys), 0x404 if isinstance(value, int) else 0x204,
                                   len(raw), len(raw), len(data)))
        keys += name.encode() + b"\0"; data += raw
    key_start = 20 + len(entries) * 16
    data_start = (key_start + len(keys) + 3) & ~3
    return struct.pack("<4s4I", b"\0PSF", 0x101, key_start, data_start, len(entries)) + b"".join(entries) + keys + bytes(data_start-key_start-len(keys)) + data

def pack_cso(raw):
    assert len(raw) % 2048 == 0
    count = len(raw) // 2048
    offset = 24 + (count + 1) * 4
    indices, blocks = [], []
    for start in range(0, len(raw), 2048):
        block = raw[start:start+2048]
        compressor = zlib.compressobj(6, zlib.DEFLATED, -15)
        packed = compressor.compress(block) + compressor.flush()
        plain = len(packed) >= len(block)
        chunk = block if plain else packed
        indices.append(offset | (0x80000000 if plain else 0))
        blocks.append(chunk); offset += len(chunk)
    indices.append(offset)
    return struct.pack("<4sIQIBB2s", b"CISO", 24, len(raw), 2048, 1, 0, b"\0\0") + struct.pack("<%dI" % len(indices), *indices) + b"".join(blocks)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--ge", action="store_true")
    parser.add_argument("--sprite", action="store_true")
    parser.add_argument("--gx-sprite", action="store_true")
    parser.add_argument("--wrap", action="store_true")
    parser.add_argument("--zero-prim", action="store_true")
    args = parser.parse_args(); args.directory.mkdir(parents=True, exist_ok=True)
    if args.ge:
        from make_ge_smoke_elf import make_elf as ge_program
        program=lambda: ge_program(args.sprite or args.gx_sprite,args.gx_sprite,args.wrap,args.zero_prim)
    else: program=make_elf
    image = pycdlib.PyCdlib(); image.new(interchange_level=3, vol_ident="PPSSPP_WII_TEST")
    image.add_directory("/PSP_GAME"); image.add_directory("/PSP_GAME/SYSDIR")
    sources = []
    for path, data in [("/PSP_GAME/SYSDIR/EBOOT.BIN", program()),
                       ("/PSP_GAME/SYSDIR/BOOT.BIN", program()),
                       ("/PSP_GAME/PARAM.SFO", sfo()),
                       ("/UMD_DATA.BIN", b"HBWI-00001|0000000000000000|0001|G")]:
        stream = io.BytesIO(data); sources.append(stream)
        image.add_fp(stream, len(data), iso_path=path)
    iso_path = args.directory / ("PSP-ge-smoke.iso" if args.ge else "PSP-smoke.iso")
    image.write(str(iso_path)); image.close()
    raw = iso_path.read_bytes(); cso_path = args.directory / ("PSP-ge-smoke.cso" if args.ge else "PSP-smoke.cso")
    cso_path.write_bytes(pack_cso(raw))
    print("Created original test discs:", iso_path, cso_path)

if __name__ == "__main__": main()
