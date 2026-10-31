#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check actual core and fast HI/LO paths against modular arithmetic."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Core/MIPS/MIPSInt.cpp').read_text()
a=s.index('void Int_MulDivType(');b=s.index('\n\tvoid ',a+5)
code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "Wii/ScalarInterpreter.h"
using u32=uint32_t;using s32=int32_t;using u64=uint64_t;using s64=int64_t;using MIPSOpcode=uint32_t;
static u32 regs[32],stateLo,stateHi,pc;
#define R(i) regs[i]
#define PC pc
#define LO stateLo
#define HI stateHi
#define _RS ((op>>21)&31)
#define _RT ((op>>16)&31)
#define _RD ((op>>11)&31)
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
'''+s[a:b]+r'''
int main() {
 u32 seed=0xabcdef;
 auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 const unsigned forms[]={16,18,17,19,24,25,28,29,46,47};
 const u32 edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
 for(unsigned i=0;i<100000;++i) {
  for(auto &r:regs)r=random();regs[0]=0;
  unsigned rs=random()%32,rt=random()%32,rd=0,fn=forms[i%10];
  if(fn==16||fn==18){rs=rt=0;rd=random()%32;}
  if(fn==17||fn==19)rt=0;
  if(i<25000) {if(rs)regs[rs]=edges[(i/10)%5];if(rt)regs[rt]=edges[(i/50)%5];}
  u32 fast[32],expected[32];memcpy(fast,regs,sizeof(regs));memcpy(expected,regs,sizeof(regs));
  stateLo=random();stateHi=random();
  if(i<25000) {stateLo=edges[(i/250)%5];stateHi=edges[(i/1250)%5];}
  u32 fastLo=stateLo,fastHi=stateHi,fastPC=0x8804000;pc=fastPC;
  const u32 op=(rs<<21)|(rt<<16)|(rd<<11)|fn;
  u64 value=u64(stateLo)|(u64(stateHi)<<32);
  if(fn==16||fn==18){if(rd)expected[rd]=fn==16?stateHi:stateLo;}
  else if(fn==17)value=(value&0xffffffffu)|(u64(regs[rs])<<32);
  else if(fn==19)value=(value&0xffffffff00000000ull)|regs[rs];
  else {
   const u64 product=(fn&1)?u64(regs[rs])*regs[rt]:u64(s64(s32(regs[rs]))*s64(s32(regs[rt])));
   value=fn>=46?value-product:fn>=28?value+product:product;
  }
  Int_MulDivType(op);assert(Wii::InterpretHiLo(op,fast,fastLo,fastHi,fastPC));
  assert(stateLo==u32(value)&&stateHi==u32(value>>32)&&fastLo==stateLo&&fastHi==stateHi&&fastPC==pc);
  assert(!memcmp(regs,expected,sizeof(regs))&&!memcmp(fast,expected,sizeof(regs)));
 }
 puts("pass: 100000 actual core/fast HI/LO cases, modular oracle, signed accumulator overflow edges");
}
'''
with tempfile.TemporaryDirectory(prefix='hilo-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
