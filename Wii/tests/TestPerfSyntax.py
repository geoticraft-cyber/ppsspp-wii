#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host syntax check of the product profiling path, with SDK declarations only."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='perf-syntax-',dir=Path.cwd()) as t:
 p=Path(t);(p/'ogc').mkdir()
 (p/'gccore.h').write_text('#pragma once\n#include <cstdint>\nuint64_t gettime();uint64_t ticks_to_microsecs(uint64_t);\n')
 (p/'ogc/system.h').write_text('#pragma once\nunsigned SYS_GetArena1Size();unsigned SYS_GetArena2Size();unsigned SYS_GetPhysicalMem1Size();unsigned SYS_GetPhysicalMem2Size();\nvoid *SYS_GetArena1Lo();void *SYS_GetArena1Hi();void *SYS_GetArena2Lo();void *SYS_GetArena2Hi();\n')
 (p/'ogc/lwp_watchdog.h').write_text('#pragma once\n#include <gccore.h>\n')
 subprocess.run(['g++','-std=c++11','-O2','-fsyntax-only','-DWII_CPU_PROFILE','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(p),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(root/'Wii/Perf.cpp')],check=True)
print('pass: actual product profiling syntax, opcode names and vertex/memory counters; host declarations only, no SDK link')
