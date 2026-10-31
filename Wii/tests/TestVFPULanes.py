#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual vector helper bodies with the preceding build; host timing only."""
from pathlib import Path
import subprocess,tempfile,sys,os
root=Path(__file__).resolve().parents[2]
def extract(s,name):
 a=s.index('void '+name+'(');b=s.index('{',a);depth=1;e=b+1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
reference=root/'Wii/tests/VFPULaneReference.h'
current=(root/'Core/MIPS/MIPSVFPUUtils.cpp').read_text()
prefix=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include "Wii/VFPUVectorLanes.h"
#define PPSSPP_PLATFORM(x) 1
#define _assert_msg_(tag,condition,...) assert(condition)
using u32=uint32_t;
enum VectorSize {V_Single=1,V_Pair,V_Triple,V_Quad};
struct State {float v[128];unsigned mask;unsigned VfpuWriteMask()const{return mask;}bool VfpuWriteMask(int i)const{return (mask>>i)&1;}} state;
State *currentMIPS=&state;uint8_t voffset[128];
#define V(i) (currentMIPS->v[voffset[i]])
'''
code=prefix+reference.read_text()+'\n'+extract(current,'ReadVector')+'\n'+extract(current,'WriteVector')+r'''
extern "C" int __lsan_is_turned_off(){return 1;}
static volatile uint32_t checksum;
int main(){
 for(unsigned r=0;r<128;++r)voffset[r]=((r&31)<<2)|(r>>5);
 uint32_t seed=0x912873;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned cases=0;
 for(unsigned trial=0;trial<8;++trial)for(unsigned n=1;n<=4;++n)for(unsigned reg=0;reg<128;++reg)for(unsigned mask=0;mask<16;++mask){
  uint32_t raw[128];for(auto &x:raw)x=random();
  // Include signed zero, infinities, NaNs and subnormals as raw payloads.
  const uint32_t special[]={0,0x80000000,0x7f800000,0xff800000,0x7fc12345,0x7f812345,1,0x807fffff};raw[reg]=special[trial];
  memcpy(state.v,raw,sizeof(raw));state.mask=mask;
  float a[6],b[6];memset(a,0x67,sizeof(a));memcpy(b,a,sizeof(a));
  PreviousReadVector(a+1,VectorSize(n),reg);ReadVector(b+1,VectorSize(n),reg);
  assert(!memcmp(a,b,sizeof(a))&&!memcmp(raw,state.v,sizeof(raw)));
  uint32_t input[4];for(auto &x:input)x=random();input[trial%4]=special[trial];float values[4];memcpy(values,input,sizeof(values));
  PreviousWriteVector(values,VectorSize(n),reg);uint32_t expected[128];memcpy(expected,state.v,sizeof(expected));
  memcpy(state.v,raw,sizeof(raw));WriteVector(values,VectorSize(n),reg);
  assert(!memcmp(expected,state.v,sizeof(expected)));++cases;
 }
 printf("pass: %u actual old/new vector reads/writes, all sizes/registers/masks, raw IEEE payloads and untouched lanes\n",cases);
#ifdef BENCHMARK
 using Read=void(*)(float*,VectorSize,int);using Write=void(*)(const float*,VectorSize,int);
 Read reads[]={PreviousReadVector,ReadVector};Write writes[]={PreviousWriteVector,WriteVector};
 for(unsigned workload=0;workload<3;++workload)for(unsigned round=0;round<9;++round)for(unsigned turn=0;turn<2;++turn){
  unsigned which=(round+turn)%2;float v[4]={1,2,3,4};state.mask=workload==2?5:0;
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<8000000;++i){unsigned n=workload?1+i%4:4,reg=(i*13)&127;reads[which](v,VectorSize(n),reg);writes[which](v,VectorSize(n),(reg+7)&127);}
  uint32_t bits;memcpy(&bits,v,4);checksum^=bits;
  printf("lane,%u,%u,%s,%.6f\n",workload,round,which?"updated":"previous",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='vfpu-lanes-',dir=Path.cwd()) as d:
 cpp=Path(d)/'test.cpp';cpp.write_text(code);exe=Path(d)/'test'
 flags=['-O3','-DBENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 subprocess.run(['g++','-std=c++11',*flags,'-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"),check=True)
