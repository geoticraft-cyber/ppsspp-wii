#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build changed portable-core objects, compare full VFPU dispatch and run core fixtures.
First build the portable core with run_io_hle_checks.py. Requires GNU ld wrapping.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];runner=root/'Wii/tests/run_io_hle_checks.py'
s=runner.read_text();n={'__file__':str(runner)};exec(s[:s.index('with concurrent.futures.ThreadPoolExecutor')],n)
out=n['out'];flags=n['flags']+['-O3','-std=c++11','-fpermissive','-include','Wii/tests/HostConfig.h']
for src in ['Core/MIPS/MIPSVFPUUtils.cpp','Core/MIPS/MIPSIntVFPU.cpp','GPU/Software/Rasterizer.cpp','GPU/Software/Lighting.cpp','GPU/Software/TransformUnit.cpp','GPU/Common/VertexDecoderCommon.cpp']:
 subprocess.run(['g++',*flags,'-c',src,'-o',str(out/(src.replace('/','_')+'.o'))],cwd=root,check=True)
with tempfile.TemporaryDirectory(prefix='vfpu-checks-',dir=out) as d:
 d=Path(d);source=(root/'Wii/tests/CoreIntegration.cpp').read_text()
 source=source.replace('#include "Wii/tests/CPUExecutionChecks.h"','#include "Wii/tests/VFPUExecutionNextChecks.h"')
 source=source.replace('InterpreterDispatchChecks();','VFPUExecutionNextChecks();InterpreterDispatchChecks();')
 cpp=d/'harness.cpp';cpp.write_text(source);obj=d/'harness.o'
 subprocess.run(['g++',*flags,'-c',str(cpp),'-o',str(obj)],cwd=root,check=True)
 objects=[out/(src.replace('/','_')+'.o') for src in n['sources'] if src!='Wii/tests/CoreIntegration.cpp']
 assert all(p.is_file() and p.stat().st_size for p in objects),'build complete portable core first'
 subprocess.run(['g++','-Wl,--gc-sections','-Wl,--wrap=_Z10ReadVectorPf10VectorSizei','-Wl,--wrap=_Z11WriteVectorPKf10VectorSizei',*[str(p)for p in objects],str(obj),'-lz','-lpthread','-o',str(out/'WiiCoreIntegration')],cwd=root,check=True)
 (out/'WiiCoreIntegration').chmod(0o755)
 tail=s[s.index('import os\nrun='):]
 tail=tail.replace("  subprocess.run([str(out/","  os.chmod(out/'WiiCoreIntegration',0o755)\n  subprocess.run([str(out/")
 exec(tail,n)
