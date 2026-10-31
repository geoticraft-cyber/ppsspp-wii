#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare original and current dispatch in one portable-core process.
Run run_io_hle_checks.py first; pass the original uploaded source zip.
Requires the host build dependencies plus GNU nm/objcopy. Host timing only.
"""
from pathlib import Path
import argparse, os, re, statistics, subprocess, tempfile, zipfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline_zip',type=Path)
parser.add_argument('--rounds',type=int,default=11)
parser.add_argument('--instructions',type=int,default=20000000)
args=parser.parse_args()
assert args.rounds>0 and args.instructions>0
root=Path(__file__).resolve().parents[2];out=root/'build-io-host'
assert (out/'WiiCoreIntegration').is_file(), 'first run run_io_hle_checks.py'
# Reuse the exact portable harness flags/source list without running its build.
runner=(root/'Wii/tests/run_io_hle_checks.py').read_text()
ns={'__file__':str(root/'Wii/tests/run_io_hle_checks.py')}
exec(runner[:runner.index('with concurrent.futures.ThreadPoolExecutor')],ns)
flags=['-std=c++11','-O3','-fno-strict-aliasing','-DHW_RVL',
       '-DPPSSPP_WII_PORTABLE_TEST','-DNO_VULKAN','-DMOBILE_DEVICE',
       '-DMAX_LOGLEVEL=5','-include','Wii/tests/HostConfig.h',
       '-I.','-ICommon','-Iext/native','-IWii/tests/compat']
def run(cmd,**kwargs):
    return subprocess.run(cmd,cwd=root,check=True,**kwargs)
with tempfile.TemporaryDirectory(prefix='cpu-probe-',dir=out) as folder:
    temp=Path(folder);overlay=temp/'original';(overlay/'Wii').mkdir(parents=True)
    with zipfile.ZipFile(args.baseline_zip.resolve()) as archive:
        prefix='ppsspp-wii-interpreter/'
        for rel in ('Core/MIPS/MIPSTables.cpp','Wii/MemoryInterpreter.h'):
            dest=overlay/rel;dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(archive.read(prefix+rel))
    baseline=temp/'baseline.o';candidate=temp/'candidate.o'
    run(['g++','-I'+str(overlay)]+flags+['-c',str(overlay/'Core/MIPS/MIPSTables.cpp'),'-o',str(baseline)])
    run(['g++']+flags+['-c','Core/MIPS/MIPSTables.cpp','-o',str(candidate)])
    symbols=run(['nm','-g','--defined-only',str(baseline)],stdout=subprocess.PIPE,text=True).stdout
    mapping=temp/'symbols';mapping.write_text(''.join(f'{line.split()[-1]} probe_baseline_{line.split()[-1]}\n' for line in symbols.splitlines()))
    renamed=temp/'renamed.o'
    run(['objcopy','--redefine-syms='+str(mapping),str(baseline),str(renamed)])
    header=(root/'Wii/tests/InterpreterDispatchChecks.h').read_text()
    header=header.replace('static void InterpreterDispatchChecks()', 'extern void BaselineInterpret(MIPSOpcode) asm("probe_baseline__Z13MIPSInterpretN6Memory6OpcodeE");\nstatic void InterpreterDispatchChecks()')
    probe=r'''
  using Dispatch=void(*)(MIPSOpcode);
  const Dispatch funcs[]={BaselineInterpret,MIPSInterpret};
  const char* names[]={"baseline","updated"};
  const uint32_t memOps[]={0x8c880000,0xac880004,0x90890001,0xa4890006,0x848a0002,0x00005812,0x01200011};
  for(unsigned trial=0;trial<ROUNDS;++trial)for(unsigned slot=0;slot<2;++slot) {
    unsigned which=(slot+trial)%2;
    for(unsigned mode=0;mode<2;++mode) {
      for(unsigned r=1;r<32;++r)currentMIPS->r[r]=r*173;
      auto start=std::chrono::steady_clock::now();
      for(unsigned i=0;i<ITERATIONS;++i) {
        currentMIPS->pc=0x08804000;currentMIPS->inDelaySlot=false;currentMIPS->downcount=100;
        currentMIPS->r[4]=0x08805000;
        funcs[which](MIPSOpcode(mode?memOps[i%7]:ops[i%10]));
      }
      printf("probe %u %s %s %.3f\n",trial,names[which],mode?"memory":"branch",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
  }
'''.replace('ROUNDS',str(args.rounds)).replace('ITERATIONS',str(args.instructions))
    position=header.rfind('  currentMIPS->inDelaySlot=false;')
    header=header[:position]+probe+header[position:]
    h=temp/'probe.h';h.write_text(header)
    source=(root/'Wii/tests/CoreIntegration.cpp').read_text().replace('"Wii/tests/InterpreterDispatchChecks.h"','"'+str(h)+'"')
    cpp=temp/'probe.cpp';cpp.write_text(source);harness=temp/'harness.o'
    run(['g++']+ns['flags']+['-std=c++11','-fpermissive','-include','Wii/tests/HostConfig.h','-c',str(cpp),'-o',str(harness)])
    objects=[out/(src.replace('/','_')+'.o') for src in ns['sources'] if src not in ('Core/MIPS/MIPSTables.cpp','Wii/tests/CoreIntegration.cpp')]
    binary=temp/'probe'
    run(['g++','-Wl,--gc-sections']+[str(p) for p in objects]+[str(harness),str(candidate),str(renamed),'-lz','-lpthread','-o',str(binary)])
    with (out/'cpu-dispatch-benchmark.log').open('w') as log:
        subprocess.run([str(binary),'smoke.elf',str(root/'assets')],cwd=out/'test-run',env=dict(os.environ,WII_TEST_DISPATCH='1'),stdout=log,stderr=subprocess.STDOUT,check=True)
rows=re.findall(r'probe (\d+) (\w+) (\w+) ([\d.]+)',(out/'cpu-dispatch-benchmark.log').read_text())
assert len(rows)==args.rounds*4
(out/'cpu-dispatch-benchmark.csv').write_text('round,version,workload,milliseconds\n'+''.join(','.join(row)+'\n' for row in rows))
for kind in ('branch','memory'):
    medians={name:statistics.median(float(v) for _,n,k,v in rows if n==name and k==kind) for name in ('baseline','updated')}
    percent=(1-medians['updated']/medians['baseline'])*100
    print(f"{kind}: baseline {medians['baseline']:.3f} ms, updated {medians['updated']:.3f} ms; {percent:.2f}% less time (host only)")
print('raw results:',out/'cpu-dispatch-benchmark.csv')
