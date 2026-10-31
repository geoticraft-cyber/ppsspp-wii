#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential checks and optional host size/timing comparison of actual handlers.
Does not validate Broadway paired-single instructions; use a real Wii for that.
"""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'Core/MIPS/MIPSIntVFPU.cpp').read_text()
a=s.index('void Int_VecDo3(');b=s.index('\n\tvoid Int_CrossQuat',a)
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "Wii/VectorArithmetic.h"
struct MIPSOpcode {uint32_t encoding;operator uint32_t()const{return encoding;}};
using VectorSize=unsigned;
static float regs[128];static unsigned pc,sp,tp,dp;
#define _VD (op.encoding&127)
#define _VS ((op.encoding>>8)&127)
#define _VT ((op.encoding>>16)&127)
#define PC pc
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
unsigned GetVecSize(MIPSOpcode op){return 1+((op.encoding>>7)&1)+2*((op.encoding>>15)&1);}
unsigned GetNumVectorElements(unsigned n){return n;}
void ReadVector(float*d,unsigned n,int r){for(unsigned i=0;i<n;++i)d[i]=regs[(r+i)&127];}
void WriteVector(float*d,unsigned n,int r){for(unsigned i=0;i<n;++i)if(!(dp&(1u<<(8+i))))regs[(r+i)&127]=d[i];}
void Swizzle(float*d,unsigned n,unsigned p){float v[4];memcpy(v,d,n*4);for(unsigned i=0;i<n;++i){unsigned j=(i+(p&3))%n;d[i]=v[j];if(p&4)d[i]=-d[i];}}
void ApplySwizzleS(float*d,unsigned n){Swizzle(d,n,sp);}
void ApplySwizzleT(float*d,unsigned n){Swizzle(d,n,tp);}
void ApplyPrefixD(float*d,unsigned n){if(dp&1)for(unsigned i=0;i<n;++i) {if(d[i]<0)d[i]=0;if(d[i]>1)d[i]=1;}}
void EatPrefixes(){sp=tp=dp=0;}
'''+(root/'Wii/tests/VectorArithmeticReference.h').read_text()+s[a:b]+r'''
int main(){
 uint32_t seed=12345;auto rnd=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 const uint32_t specials[]={0,0x80000000,1,0x807fffff,0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x3f800000,0xbf800000};
 const unsigned operations[]={192,193,200,199};unsigned cases=0;
 for(unsigned trial=0;trial<30000;++trial)for(unsigned n=1;n<=4;++n)for(unsigned operation:operations){
  float initial[128],expected[128];for(unsigned i=0;i<128;++i){uint32_t bits=trial<100?specials[(i+trial)%10]:rnd();memcpy(initial+i,&bits,4);}
  // Includes exact and partial overlap of guest sources/destination.
  unsigned vd=rnd()%128,vs=trial%3?rnd()%128:vd,vt=trial%5?rnd()%128:vd;
  MIPSOpcode op{operation<<23|vd|vs<<8|vt<<16|((n-1)&1)<<7|((n-1)>>1)<<15};
  unsigned ps=rnd()%8,pt=rnd()%8,pd=(rnd()%16)<<8|(rnd()&1);
  memcpy(regs,initial,sizeof(regs));pc=0x8804000;sp=ps;tp=pt;dp=pd;
  PreviousVecDo3(op);memcpy(expected,regs,sizeof(regs));unsigned endPC=pc;
  memcpy(regs,initial,sizeof(regs));pc=0x8804000;sp=ps;tp=pt;dp=pd;
  Int_VecDo3(op);for(unsigned lane=0;lane<128;++lane) {
    if(memcmp(regs+lane,expected+lane,4) && !(std::isnan(regs[lane])&&std::isnan(expected[lane]))) {
      uint32_t x,y;memcpy(&x,regs+lane,4);memcpy(&y,expected+lane,4);
      printf("mismatch trial=%u n=%u op=%u lane=%u actual=%08x expected=%08x\n",trial,n,operation,lane,x,y);return 1;
    }
  }assert(pc==endPC&&!sp&&!tp&&!dp);++cases;
 }
 printf("pass: %u old/new handler cases; vector sizes, overlap, prefix call order, masks, IEEE edge cases (NaNs by classification)\n",cases);
#ifdef BENCHMARK
 using Handler=void(*)(MIPSOpcode);Handler handlers[]={PreviousVecDo3,Int_VecDo3};
 for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn){unsigned which=(round+turn)%2;
  for(unsigned i=0;i<128;++i)regs[i]=float(i%7)*0.125f;
  auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<2000000;++i)handlers[which](MIPSOpcode{operations[i%4]<<23|64|16<<8|32<<16|1<<7|1<<15});
  printf("host,%u,%s,%.3f ms,%g\n",round,which?"updated":"original",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),regs[64]);
 }
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='vector-arithmetic-',dir=Path.cwd()) as d:
 p=Path(d);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 flags=['-O3','-DBENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
 subprocess.run(['g++','-std=c++11',*flags,'-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
 if '--benchmark' in sys.argv:
  for opt in ['-Os','-O3']:
   obj=p/'size.o';subprocess.run(['g++','-std=c++11',opt,'-I'+str(root),'-c',str(cpp),'-o',str(obj)],check=True)
   result=subprocess.run(['nm','-S','--size-sort',str(obj)],text=True,capture_output=True,check=True)
   print(opt+' handler sizes (hex bytes; helpers separate, not total Wii DOL size):')
   print('\n'.join(x for x in result.stdout.splitlines() if 'VecDo3' in x or 'VectorArithmetic' in x))
