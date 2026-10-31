#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the actual sprite dispatch branch against core types with host headers."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'GPU/Software/Rasterizer.cpp').read_text()
source=source.replace('#if PPSSPP_PLATFORM(WII) && !defined(PPSSPP_WII_PORTABLE_TEST)','#if PPSSPP_PLATFORM(WII)')
with tempfile.TemporaryDirectory(prefix='sprite-syntax-',dir=Path.cwd()) as folder:
    cpp=Path(folder)/'raster.cpp';cpp.write_text(source)
    subprocess.run(['g++','-std=c++11','-O2','-fsyntax-only','-fpermissive',
        '-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-DNO_VULKAN','-DMOBILE_DEVICE',
        '-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),
        '-I'+str(root/'ext/native'),str(cpp)],check=True)
print('pass: product sprite dispatch branch checked with actual core types; host compiler only')
