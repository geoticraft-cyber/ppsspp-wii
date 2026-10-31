#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run actual HashFunctions with native and simulated big-endian word storage.
The simulated case uses the real u32_le swap type and models the host byte store;
it is not a PowerPC executable test.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Core/MIPS/MIPSAnalyst.cpp').read_text();fn=s[s.index('\tvoid HashFunctions('):s.index('\n\tvoid PrecompileFunction(',s.index('\tvoid HashFunctions('))]
with tempfile.TemporaryDirectory(prefix='hash-endian-') as t:
 t=Path(t)
 for be in (0,1):
  cpp=t/'check.cpp';exe=t/'check'
  cpp.write_text('''
#include "Wii/CpuProfile.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>
#include <algorithm>
#include "Common/Swap.h"
using MIPSOpcode=u32;using MIPSInfo=u32;
const u32 IN_IMM16=1,IN_IMM26=2;
#define MIPS_IS_EMUHACK(x) false
static u32 words[]={0x3c021234,0x24425678,0x0c123456,0x00401821,0x03e00008};
static u32 masked[]={0x3c020000,0x24420000,0x0c000000,0x00401821,0x03e00008};
u32 MIPSGetInfo(u32 op){return op>>26==3?IN_IMM26:op>>26==15||op>>26==9?IN_IMM16:0;}
namespace Memory {bool IsValidRange(u32,u32 n){return n==20;}u32 ReadUnchecked_Instruction(u32 a,bool){return words[a/4];}}
struct AnalyzedFunction {u32 start=0,end=16;u64 hash=0;bool hasHash=false;};
static std::vector<AnalyzedFunction> functions(1);static std::recursive_mutex functions_lock;
u64 CityHash64(const char *bytes,size_t n) {
 assert(n==20);
 for(unsigned i=0;i<5;++i){u32 stored;memcpy(&stored,bytes+i*4,4);
#if SIMULATE_BE
  assert(stored==bswap32(masked[i]));
  for(unsigned j=0;j<4;++j)assert(u8(stored>>(24-8*j))==u8(masked[i]>>(8*j)));
#else
  for(unsigned j=0;j<4;++j)assert(u8(bytes[i*4+j])==u8(masked[i]>>(8*j)));
#endif
 }
 return 0x0123456789abcdefULL;
}
'''+fn+'''
int main(){HashFunctions();assert(functions[0].hasHash&&functions[0].hash==0x0123456789abcdefULL);
 std::vector<u32> skip{40,512};HashFunctions(&skip);assert(!functions[0].hasHash&&functions[0].hash==0);
 std::vector<u32> match{20};HashFunctions(&match);assert(functions[0].hasHash&&functions[0].hash==0x0123456789abcdefULL);
}
''')
  cmd=['g++','-std=c++11','-O2','-fno-strict-aliasing','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),'-DSIMULATE_BE='+str(be)]
  if be:cmd+=['-DCOMMON_LITTLE_ENDIAN=0','-DCOMMON_BIG_ENDIAN=1']
  subprocess.run(cmd+[str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('pass: actual function-hash masking emits canonical little-endian bytes on native and modeled big-endian storage')
