#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact BMP glyph fingerprint from the uploaded dense atlas, plus legacy lookups."""
from pathlib import Path
import hashlib, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='compact-atlas-') as temp:
    exe=Path(temp)/'check'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-I'+str(root),'-I'+str(root/'ext/native'),
        str(root/'Wii/tests/CompactAtlasTests.cpp'),str(root/'Core/Util/ppge_atlas.cpp'),
        str(root/'ext/native/gfx/texture_atlas.cpp'),'-o',str(exe)],check=True)
    data=subprocess.check_output([str(exe)])
    assert len(data)==65536*32
    assert hashlib.sha256(data).hexdigest()=="17f906c3a02dd32b67d4d24940f1aa9c6e6ef4dea2d4fddf2b1a91084f1f725b"
print('pass: exact uploaded glyph fingerprint, unicode boundaries and legacy range lookup')
