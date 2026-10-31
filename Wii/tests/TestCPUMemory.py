#!/usr/bin/env python3
"""Compare live CPU memory operations with preceding/original handlers."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'Core/MIPS/MIPSInt.cpp').read_text();a=s.index('void Int_FPULS(');b=s.index('void Int_mxc1(',a);fp=s[a:b]
old=(root/'Wii/tests/CPUReferenceMemory.h').read_text().replace('#pragma once','').replace('namespace Wii {','namespace PreviousCPU {using Wii::addressSpace;',1)
code=r'''
#if defined(__SANITIZE_ADDRESS__)
extern "C" int __lsan_is_turned_off(){return 1;}
#endif
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include <chrono>
#include "Wii/MemoryInterpreter.h"
using MIPSOpcode=uint32_t;
namespace Wii {AddressSpace addressSpace;}
static uint32_t regs[32],floats[32],refPC;
#define PC refPC
#define R(i) regs[i]
#define FI(i) floats[i]
#define _FT ((op>>16)&31)
#define _RS ((op>>21)&31)
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
namespace Memory {
u32 Read_U32(u32 a){auto *p=Wii::addressSpace.Contiguous(a,4);assert(p);return *reinterpret_cast<u32_le*>(p);}
void Write_U32(u32 value,u32 a){auto *p=Wii::addressSpace.Contiguous(a,4);assert(p);*reinterpret_cast<u32_le*>(p)=value;}
}
'''+old+fp+r'''
__attribute__((noinline)) static bool OldMemory(uint32_t op,uint32_t *r,uint32_t &pc){return PreviousCPU::InterpretMemory(op,r,pc);}
__attribute__((noinline)) static bool NewMemory(uint32_t op,uint32_t *r,uint32_t &pc){return Wii::InterpretMemory(op,r,pc);}
int main(){
 std::vector<uint8_t> ram(0x2000000),vram(0x200000),scratch(16384),nullPage(65536);
 auto &map=Wii::addressSpace;map.ram=ram.data();map.ramSize=ram.size();map.vram=vram.data();map.scratch=scratch.data();map.nullPage=nullPage.data();
 uint32_t seed=0x298371;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned fpChecks=0,addressChecks=0;
 for(unsigned mode=0;mode<4;++mode){
  map.ramSize=mode==2?0x100040:ram.size();map.RebuildPages();if(mode==0)map.pagesReady=false;
  const u32 bases[]={0x08000000,mode==2?0x08100000u:0x09ffffc0u,0x04000000,0x041fffc0,0x047fffc0,0x10000,0x13fc0,0};
  for(unsigned i=0;i<100000;++i){
   const unsigned rs=random()%32,ft=random()%32;const int16_t displacement=random();
   const u32 address=bases[i%8]+(random()&60)|((random()&3)<<30);
   for(auto &r:regs)r=random();regs[0]=0;for(auto &f:floats)f=random();
   // The helper consumes the supplied register file; register zero is fixed by
   // the caller. Use rs != 0 when constructing an arbitrary target address.
   const unsigned baseReg=rs?rs:1;regs[baseReg]=address-u32(int32_t(displacement));
   uint32_t fastR[32],fastF[32];memcpy(fastR,regs,sizeof(regs));memcpy(fastF,floats,sizeof(floats));
   const u32 op=(i&1?57u:49u)<<26|baseReg<<21|ft<<16|uint16_t(displacement);
   auto *p=map.Contiguous(address,4);assert(p);*reinterpret_cast<u32_le*>(p)=random();const u32 initial=*reinterpret_cast<u32_le*>(p);
   const u32 initialPC=i%19?random()&~3u:0xfffffffcu;refPC=initialPC;Int_FPULS(op);const u32 expectedMemory=*reinterpret_cast<u32_le*>(p);
   *reinterpret_cast<u32_le*>(p)=initial;u32 pc=initialPC;
   assert(Wii::InterpretFPMemory(op,fastR,fastF,pc));
   assert(pc==refPC&&!memcmp(fastR,regs,sizeof(regs))&&!memcmp(fastF,floats,sizeof(floats))&&u32(*reinterpret_cast<u32_le*>(p))==expectedMemory);++fpChecks;
  }
  for(unsigned i=0;i<200000;++i){
   const unsigned bytes=1u<<(i%3);const u32 address=(random()&~(bytes-1));
   assert(Wii::InterpreterMemoryPointer(address,bytes)==map.Contiguous(address,bytes));++addressChecks;
  }
 }
 map.nullPage=nullptr;map.ramSize=ram.size();map.RebuildPages();
 for(u32 address:{0u,0x14000u,0x0a000000u,0x04800000u,0x08000001u})for(unsigned kind:{49u,57u}){
   uint32_t r[32]{},f[32]{};r[1]=address;f[0]=0x7fc12345;u32 pc=123;
   assert(!Wii::InterpretFPMemory(kind<<26|1u<<21,r,f,pc)&&pc==123&&f[0]==0x7fc12345);
 }
 // Re-fetching and reloading must observe current bytes, not cached values.
 uint32_t r[32]{},f[32]{};r[1]=0x08000000;u32 pc=0;
 for(u32 bits:{0x7fc12345u,0xff800000u,0x80000000u,0x00000001u,0x12345678u}){
   Memory::Write_U32(bits,r[1]);assert(Wii::InterpretFPMemory(49u<<26|1u<<21,r,f,pc));assert(f[0]==bits);
 }
 printf("pass: %u raw COP1 loads/stores and %u aligned pointer comparisons; aliases, mirrors, partial RAM, null/scratch, page fallback, f0, NaN bits and live edits\n",fpChecks,addressChecks);
#ifdef CPU_MEMORY_BENCHMARK
 uint32_t opcodes[8]={0x8c220000,0xac220004,0x80220008,0x90220009,0x8422000a,0x9422000c,0xa022000e,0xa4220010};
 uint32_t registers[32]{};registers[1]=0x08004000;u32 counter=0;
 for(unsigned round=0;round<7;++round)for(unsigned turn=0;turn<2;++turn){
   const bool updated=(turn^(round&1))!=0;
   auto fn=updated?NewMemory:OldMemory;const auto start=std::chrono::steady_clock::now();
   for(unsigned i=0;i<4000000;++i){registers[1]=0x08004000+((i&255)<<2);assert(fn(opcodes[i&7],registers,counter));}
   printf("host mapped memory round %u %s %.3f ms\n",round,updated?"new":"old",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='cpu-memory-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 flags=['-O3','-DCPU_MEMORY_BENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all']
 subprocess.run(['g++','-std=c++11',*flags,'-fno-strict-aliasing','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
