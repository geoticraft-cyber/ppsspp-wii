#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare uploaded/current CPU loops and current without constant-pair fusion.
First run run_io_hle_checks.py. Requires GNU nm, objcopy, size and host C++ tools.
No game data is used. Results describe the host, not Wii throughput.
"""
from pathlib import Path
import argparse, csv, os, re, statistics, subprocess, tempfile, zipfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline_zip',type=Path)
p.add_argument('--rounds',type=int,default=7)
p.add_argument('--instructions',type=int,default=4000000)
a=p.parse_args()
assert a.rounds>0 and 0<a.instructions<100000000
root=Path(__file__).resolve().parents[2]
runner=root/'Wii/tests/run_io_hle_checks.py'
s=runner.read_text();ns={'__file__':str(runner)}
exec(s[:s.index('with concurrent.futures.ThreadPoolExecutor')],ns)
out=ns['out'];flags=ns['flags']+['-O3','-std=c++11','-fpermissive','-include','Wii/tests/HostConfig.h']
def run(cmd,**kwargs):return subprocess.run(cmd,cwd=root,check=True,**kwargs)
cpp=r'''
#include "Wii/tests/CPUExecutionChecks.h"
extern int BaselineLoop(u64) asm("baseline__Z22MIPSInterpret_RunUntily");
extern int UnfusedLoop(u64) asm("unfused__Z22MIPSInterpret_RunUntily");
extern MIPSInfo BaselineInfo(MIPSOpcode) asm("baseline__Z11MIPSGetInfoN6Memory6OpcodeE");
extern const char *BaselineName(MIPSOpcode) asm("baseline__Z11MIPSGetNameN6Memory6OpcodeE");
extern MIPSInterpretFunc BaselineHandler(MIPSOpcode) asm("baseline__Z20MIPSGetInterpretFuncN6Memory6OpcodeE");
static void InterpreterLoopBenchmark() {
  g_Config.bEnableLogging=false;Wii::SetDiagnosticLevel(0);
  static_assert(MIPSInfo().value==0,"default flags must be constant-initializable");
  static_assert((MIPSInfo(IN_VS|OUT_VD)&OUT_VD)==OUT_VD,"retain high metadata bits");
  u32 seed=0x817348;
  for(unsigned i=0;i<1200000;++i) {
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    const MIPSOpcode op(seed);
    assert(BaselineInfo(op).value==MIPSGetInfo(op).value);
    assert(!strcmp(BaselineName(op),MIPSGetName(op)));
    assert(BaselineHandler(op)==MIPSGetInterpretFunc(op));
  }
  puts("pass: 1200000 uploaded/current opcode flag, name and handler comparisons");
  // Dedicated benchmark process: discard boot events so no guest services or
  // rendering enter the timing interval. The regular tests retain all events.
  CoreTiming::ClearPendingEvents();
  const u32 base=0x08820000;
  const char *names[]={"baseline","updated","unfused"};
  int (*loops[])(u64)={BaselineLoop,MIPSInterpret_RunUntil,UnfusedLoop};
  const char *workloads[]={"scalar","pairs_dense","pairs_sparse","lui_misses","vfpu"};
  struct Result {
    CPUObservedState cpu;u32 vectors[128],ctrl[16];u64 ticks;
    void Read(){cpu.Read();memcpy(vectors,currentMIPS->v,sizeof(vectors));memcpy(ctrl,currentMIPS->vfpuCtrl,sizeof(ctrl));ticks=CoreTiming::GetTicks();}
    bool Same(const Result &b)const{return cpu.Same(b.cpu)&&!memcmp(vectors,b.vectors,sizeof(vectors))&&!memcmp(ctrl,b.ctrl,sizeof(ctrl))&&ticks==b.ticks;}
  };
  const auto prepare=[&]() {
    for(unsigned r=0;r<32;++r){currentMIPS->r[r]=r?173*r:0;currentMIPS->fi[r]=0x3f800000;}
    for(auto &v:currentMIPS->v)v=1.0f;
    memset(currentMIPS->vfpuCtrl,0,sizeof(currentMIPS->vfpuCtrl));
    currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX]=currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX]=0xe4;
    currentMIPS->pc=base;currentMIPS->nextPC=0;currentMIPS->inDelaySlot=false;
    currentMIPS->lo=currentMIPS->hi=currentMIPS->fpcond=currentMIPS->fcr31=0;
    currentMIPS->downcount=CoreTiming::slicelength=100000000;
    CoreTiming::globalTimer=0;coreState=CORE_RUNNING;
  };
  for(unsigned mode=0;mode<5;++mode) {
    for(unsigned i=0;i<64;++i) {
      u32 word=0x25290001; // addiu t1,t1,1
      if(mode==0){const u32 ops[]={0x25290001,0x312affff,0x01495021,0};word=ops[i&3];}
      if(mode==1||(mode==2&&i%16<2))word=i&1?0x3508abcd:0x3c081234;
      if(mode==3)word=i&1?0x3509abcd:0x3c081234;
      if(mode==4){const u32 ops[]={0xd0000080,0xd0010080,0xd0020080,0xd0060080,0xf3800080,0xf3830080};word=ops[i%6];}
      Memory::Write_U32(word,base+4*i);
    }
    Memory::Write_U32(2u<<26|((base>>2)&0x03ffffff),base+256);
    Memory::Write_U32(0,base+260);
    // Every cutoff includes tick and branch-delay handling. Compare full CPU
    // state before timing, including a cutoff halfway through a constant pair.
    for(unsigned budget:{1u,2u,63u,64u,65u,66u,67u,1001u}) {
      Result expected;
      for(unsigned which=0;which<3;++which) {
        prepare();loops[which](budget-1);Result result;result.Read();
        if(which)assert(expected.Same(result));else expected=result;
      }
    }
    for(unsigned round=0;round<ROUNDS;++round)for(unsigned turn=0;turn<3;++turn) {
      const unsigned which=(turn+round)%3;prepare();
      const auto start=std::chrono::steady_clock::now();loops[which](ITERATIONS-1);
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      printf("loop,%u,%s,%s,%.6f,%llu\n",round,names[which],workloads[mode],ms,(unsigned long long)CoreTiming::GetTicks());
    }
  }
  puts("pass: uploaded/updated/unfused complete CPU/vector/tick states match at all tested loop cutoffs");
}
'''.replace('ROUNDS',str(a.rounds)).replace('ITERATIONS',str(a.instructions))
with tempfile.TemporaryDirectory(prefix='loop-probe-',dir=out) as folder:
    temp=Path(folder)
    with zipfile.ZipFile(a.baseline_zip.resolve()) as z:
        baseline=temp/'baseline.cpp';baseline.write_bytes(z.read('ppsspp-wii-interpreter/Core/MIPS/MIPSTables.cpp'))
        overlay=temp/'original';(overlay/'Core/MIPS').mkdir(parents=True)
        (overlay/'Core/MIPS/MIPSTables.h').write_bytes(z.read('ppsspp-wii-interpreter/Core/MIPS/MIPSTables.h'))
    objects=[];sizes=[]
    for name,source,extra in [('baseline',baseline,['-I'+str(overlay)]),('updated',root/'Core/MIPS/MIPSTables.cpp',[]),('unfused',root/'Core/MIPS/MIPSTables.cpp',['-DWII_DISABLE_CONSTANT_PAIRS'])]:
        raw=temp/(name+'-raw.o');run(['g++',*extra,*flags,'-c',str(source),'-o',str(raw)])
        sizes.append(name+'\n'+run(['size',str(raw)],text=True,stdout=subprocess.PIPE).stdout)
        if name=='updated':objects.append(raw);continue
        symbols=run(['nm','--defined-only','--extern-only','--format=posix',str(raw)],text=True,stdout=subprocess.PIPE).stdout
        mapping=temp/(name+'-symbols');mapping.write_text(''.join(line.split()[0]+' '+name+'_'+line.split()[0]+'\n'for line in symbols.splitlines()if line.strip()))
        obj=temp/(name+'.o');run(['objcopy','--redefine-syms='+str(mapping),str(raw),str(obj)]);objects.append(obj)
    header=temp/'loop.h';header.write_text(cpp)
    source=(root/'Wii/tests/CoreIntegration.cpp').read_text()
    source=source.replace('#include "Wii/tests/CPUExecutionChecks.h"','#include "'+str(header)+'"')
    source=source.replace('if(getenv("WII_TEST_DISPATCH")) {','if(getenv("WII_TEST_DISPATCH")) { InterpreterLoopBenchmark(); PSP_Shutdown();LogManager::Shutdown();return 0;')
    harness=temp/'harness.cpp';harness.write_text(source);obj=temp/'harness.o'
    run(['g++',*flags,'-c',str(harness),'-o',str(obj)])
    shared=[out/(src.replace('/','_')+'.o')for src in ns['sources']if src not in ('Core/MIPS/MIPSTables.cpp','Wii/tests/CoreIntegration.cpp')]
    assert all(o.is_file() and o.stat().st_size for o in shared),'build portable core first'
    binary=temp/'benchmark';run(['g++','-Wl,--gc-sections',*[str(o)for o in shared],str(obj),*[str(o)for o in objects],'-lz','-lpthread','-o',str(binary)])
    with (out/'interpreter-loop.log').open('w') as log:
        subprocess.run([str(binary),'smoke.elf',str(root/'assets')],cwd=out/'test-run',env=dict(os.environ,WII_TEST_DISPATCH='1'),stdout=log,stderr=subprocess.STDOUT,check=True)
    (out/'interpreter-object-sizes.txt').write_text('\n'.join(sizes))
rows=list(csv.reader(line for line in (out/'interpreter-loop.log').read_text().splitlines()if line.startswith('loop,')))
assert len(rows)==a.rounds*15
with (out/'interpreter-loop.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['type','round','variant','workload','milliseconds','guest_ticks']);w.writerows(rows)
for mode in ('scalar','pairs_dense','pairs_sparse','lui_misses','vfpu'):
    med={name:statistics.median(float(r[4])for r in rows if r[2]==name and r[3]==mode)for name in ('baseline','updated','unfused')}
    print(mode,', '.join(f'{n} {v:.3f} ms'for n,v in med.items()))
print('host only; raw data and object sizes are in build-io-host/interpreter-*')
