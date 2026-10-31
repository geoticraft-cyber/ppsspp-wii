#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Optional paired host benchmarks. Supply the uploaded source zip for atlas timing.
Run correctness first: TestCompactAtlas.py and TestDrawVertexMemo.py.
Results measure host helpers, not Wii frame rates. Avoid concurrent benchmarks.
"""
from pathlib import Path
import argparse, subprocess, tempfile, zipfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--baseline-zip',type=Path);args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='compact-perf-') as temp:
    temp=Path(temp)
    common=['g++','-std=c++11','-O3','-fno-strict-aliasing','-I'+str(root),'-I'+str(root/'ext/native')]
    if args.baseline_zip:
        with zipfile.ZipFile(args.baseline_zip) as archive:
            for suffix,name in [('Core/Util/ppge_atlas.cpp','atlas.cpp'),('ext/native/gfx/texture_atlas.cpp','lookup.cpp')]:
                matches=[n for n in archive.namelist() if n.endswith('/'+suffix)]
                assert len(matches)==1
                (temp/name).write_bytes(archive.read(matches[0]))
        (temp/'wrapper.cpp').write_text('#include "Core/Util/ppge_atlas.h"\nextern "C" const AtlasChar *UploadedGetChar(int cp){return ppge_atlas.fonts[0]->getChar(cp);}\n')
        rename=['-DAtlasFont=UploadedAtlasFont','-DAtlas=UploadedAtlas','-Dppge_atlas=uploaded_atlas','-Dppge_images=uploaded_images','-Dppge_fonts=uploaded_fonts']
        objects=[]
        for name in ['atlas','lookup','wrapper']:
            obj=temp/(name+'.o');objects.append(str(obj))
            subprocess.run(common+['-I'+str(root/'Core/Util')]+rename+['-c',str(temp/(name+'.cpp')),'-o',str(obj)],check=True)
        exe=temp/'atlas'
        subprocess.run(common+[str(root/'Wii/tests/CompactAtlasBenchmark.cpp'),str(root/'Core/Util/ppge_atlas.cpp'),str(root/'ext/native/gfx/texture_atlas.cpp')]+objects+['-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    exe=temp/'memo'
    subprocess.run(common+['-I'+str(root/'Common'),'-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),str(root/'Wii/tests/CompactMemoBenchmark.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
