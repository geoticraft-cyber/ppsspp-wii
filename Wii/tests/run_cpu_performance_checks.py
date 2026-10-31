#!/usr/bin/env python3
"""Run the portable CPU regressions/benchmarks at the product's -O3 setting.
Build the complete portable core first with run_io_hle_checks.py when needed.
No PSP game data or Wii SDK required. Timings are host dispatch microbenchmarks.
"""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[2]
runner=root/'Wii/tests/run_io_hle_checks.py'
s=runner.read_text()
namespace={'__file__':str(runner),'__name__':'cpu_host_build'}
exec(s[:s.index('with concurrent.futures.ThreadPoolExecutor')],namespace)
out=namespace['out'];sources=namespace['sources'];objs=[out/(src.replace('/','_')+'.o')for src in sources]
if not all(p.exists() and p.stat().st_size>0 for p in objs):
 subprocess.run([sys.executable,str(runner)],cwd=root,check=True)
namespace['flags']+=['-O3','-DWII_CPU_COMPILED_REFERENCE']
reference=out/'cpu-reference-include/Wii';reference.mkdir(parents=True,exist_ok=True)
(reference/'MemoryInterpreter.h').write_bytes((root/'Wii/tests/CPUReferenceMemory.h').read_bytes())
raw=out/'cpu-reference-raw.o';baseline=out/'cpu-reference.o'
subprocess.run(['g++','-I'+str(reference.parent),*namespace['flags'],'-std=c++11','-fpermissive','-include',str(root/'Wii/tests/HostConfig.h'),'-c',str(root/'Wii/tests/CPUReferenceTables.cpp'),'-o',str(raw)],cwd=root,check=True)
# Isolate all old definitions, including COMDAT data, while retaining shared
# undefined references to original instruction handlers and live guest state.
old='_Z13MIPSInterpretN6Memory6OpcodeE';new='_Z20PreviousCPUInterpretN6Memory6OpcodeE'
symbols=subprocess.run(['nm','--defined-only','--extern-only','--format=posix',str(raw)],text=True,stdout=subprocess.PIPE,check=True).stdout
names=[line.split()[0]for line in symbols.splitlines()if line.strip()]
assert old in names
mapping=out/'cpu-reference-symbols.txt'
mapping.write_text(''.join(name+' '+(new if name==old else 'CPUReference_'+name)+'\n'for name in names))
subprocess.run(['objcopy','--redefine-syms='+str(mapping),str(raw),str(baseline)],check=True)
for src in ['Core/MIPS/MIPSTables.cpp','Wii/tests/CoreIntegration.cpp']:
 result=namespace['build'](src)
 if not result or not result.stat().st_size:raise SystemExit('CPU build failed: '+src)
subprocess.run(['g++','-Wl,--gc-sections']+[str(p)for p in objs]+[str(baseline),'-lz','-lpthread','-o',str(out/'WiiCoreIntegration')],cwd=root,check=True)
# Same IO, interpreter boundary/dispatch, service and guest-wait fixtures.
exec(s[s.index("run=out/'test-run'"):],namespace)
print('all CPU portable core checks passed; timings are in build-io-host/dispatch-test.log',flush=True)
