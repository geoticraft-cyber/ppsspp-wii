#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run real hash construction with all builtin eligible sizes and verify matches."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'Core/MIPS/MIPSAnalyst.cpp').read_text()
a=s.index('\tvoid HashFunctions(');fn=s[a:s.index('\n\tvoid PrecompileFunction(',a)]
table=s[s.index('static const HardHashTableEntry hardcodedHashes'):s.index('namespace MIPSAnalyst')]
sizes=sorted({int(n) for n in re.findall(r'\{\s*0x[0-9a-fA-F]+(?:ULL)?,\s*(\d+)',table)})
assert 40 in sizes and 512 in sizes
code=r'''
#include "Common/Swap.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <mutex>
using MIPSOpcode=u32;using MIPSInfo=u32;
const u32 IN_IMM16=1,IN_IMM26=2;
#define MIPS_IS_EMUHACK(x) false
static unsigned reads=0,hashes=0;static std::vector<u32> memory;
static u32 MIPSGetInfo(u32 op){return op>>26==3?IN_IMM26:op>>26==9?IN_IMM16:0;}
namespace Memory {bool IsValidRange(u32 a,u32 n){return (uint64_t)a+n<=memory.size()*4;}u32 ReadUnchecked_Instruction(u32 a,bool){++reads;return memory[a/4];}}
struct AnalyzedFunction{u32 start,end;u64 hash;bool hasHash;};
static std::vector<AnalyzedFunction> functions;static std::recursive_mutex functions_lock;
static u64 CityHash64(const char *p,size_t n){++hashes;u64 h=123;for(size_t i=0;i<n;++i)h=h*131+u8(p[i]);return h;}
'''+fn+'''
int main(){
 std::vector<u32> eligible={'''+','.join(map(str,sizes))+r'''};
 for(unsigned i=0;i<1024;++i){u32 n=(i%256+1)*4,begin=memory.size()*4;
  for(unsigned k=0;k<n/4;++k)memory.push_back(k%2?0x24210001+k:0x00431821);
  functions.push_back({begin,begin+n-4,0,false});
 }
 HashFunctions();auto full=functions;const unsigned beforeReads=reads,beforeHashes=hashes;
 reads=hashes=0;HashFunctions(&eligible);unsigned retained=0;
 for(unsigned i=0;i<functions.size();++i){auto &f=functions[i];bool match=std::binary_search(eligible.begin(),eligible.end(),f.end-f.start+4);
  assert(f.hasHash==match);if(match){++retained;assert(f.hash==full[i].hash);}else assert(f.hash==0);
 }
 assert(hashes==retained&&reads<beforeReads);
 printf("pass: actual function-hash filter: instructions read %u -> %u, hashes %u -> %u; all eligible hashes identical; builtin 40/512-byte GoW candidates retained; transient size-vector payload %u bytes\n",beforeReads,reads,beforeHashes,hashes,unsigned(eligible.size()*sizeof(u32)));
 HashFunctions();for(unsigned i=0;i<functions.size();++i)assert(functions[i].hasHash&&functions[i].hash==full[i].hash);
 puts("pass: explicit unfiltered hashing restores all signatures for user hash-map generation/registration");
}
'''
with tempfile.TemporaryDirectory(prefix='hash-filter-') as t:
 t=Path(t);(t/'test.cpp').write_text('#include "Wii/CpuProfile.h"\n'+code);exe=t/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-fno-strict-aliasing','-I'+str(root),str(t/'test.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
