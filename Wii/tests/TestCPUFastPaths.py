#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the new leaf operations with handlers extracted from this core.

Uses the original handlers verbatim, not duplicated reference formulas.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'Core/MIPS/MIPSInt.cpp').read_text()
def handler(name):
    start = source.index('void ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "Wii/ScalarInterpreter.h"
using u32=uint32_t;
using MIPSOpcode=uint32_t;
static uint32_t regs[32], refPC;
#define PC refPC
#define R(i) regs[i]
#define _RS ((op>>21)&31)
#define _RT ((op>>16)&31)
#define _RD ((op>>11)&31)
#define _POS ((op>>6)&31)
#define _SIZE ((op>>11)&31)
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
static uint32_t swap32(uint32_t x) {return __builtin_bswap32(x);}
'''
tail = r'''
int main() {
  uint32_t seed=0xe12129;
  auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned i=0;i<120000;++i) {
    uint32_t fast[32];for(auto &r:regs)r=random();regs[0]=0;
    memcpy(fast,regs,sizeof(fast));unsigned rs=random()%32,rt=random()%32,rd=random()%32,sa=random()%32;
    uint32_t op=31u<<26;
    if(i%4==0) {rd=random()%(32-sa);op|=rs<<21|rt<<16|rd<<11|sa<<6;}
    else if(i%4==1) {rd=sa+random()%(32-sa);op|=rs<<21|rt<<16|rd<<11|sa<<6|4;}
    else {sa=(i%4==2)?2:3;op|=rt<<16|rd<<11|sa<<6|32;}
    refPC=0x08804000;uint32_t fastPC=refPC;
    if(i%4<2)Int_Special3(op);else Int_Allegrex2(op);
    assert(Wii::InterpretScalar(op,fast,fastPC));
    assert(!memcmp(fast,regs,sizeof(fast))&&fastPC==refPC);
  }
  uint32_t fast[32]{},pc=0x08804000;
  // Invalid bitfield ranges must not enter C++ undefined shifts.
  assert(!Wii::InterpretScalar(31u<<26|31u<<11|31u<<6,fast,pc));
  assert(!Wii::InterpretScalar(31u<<26|1u<<11|31u<<6|4,fast,pc));
  assert(!Wii::InterpretScalar(31u<<26|1u<<21|2u<<6|32,fast,pc));
  assert(pc==0x08804000);
  puts("PASS: 120000 EXT/INS/WSBH/WSBW cases against original core handlers, zero register, aliasing, width-32 and invalid encodings");
}
'''
with tempfile.TemporaryDirectory(prefix='cpu-regression-', dir=Path.cwd()) as directory:
    cpp=Path(directory)/'reference.cpp'
    cpp.write_text(prefix+handler('Int_Special3')+'\n'+handler('Int_Allegrex2')+tail)
    exe=Path(directory)/'reference'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
