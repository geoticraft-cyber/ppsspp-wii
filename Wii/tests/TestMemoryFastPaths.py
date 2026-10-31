#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare mapped/merge helpers with the unchanged core memory handler."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Core/MIPS/MIPSInt.cpp').read_text();a=s.index('void Int_ITypeMem(');b=s.index('void Int_FPULS(',a)
handler=s[a:b]
code=r'''
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include "Wii/MemoryInterpreter.h"
using MIPSOpcode=uint32_t;
static uint32_t regs[32],refPC;
#define PC refPC
#define R(i) regs[i]
#define _RS ((op>>21)&31)
#define _RT ((op>>16)&31)
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
namespace Wii {AddressSpace addressSpace;}
namespace Memory {
static uint32_t Read(uint32_t a,unsigned n) {
 uint32_t v=0;for(unsigned i=0;i<n;++i){auto p=Wii::addressSpace.Translate(a+i);assert(p);v|=uint32_t(*p)<<(i*8);}return v;
}
static void Write(uint32_t v,uint32_t a,unsigned n) {
 for(unsigned i=0;i<n;++i){auto p=Wii::addressSpace.Translate(a+i);assert(p);*p=uint8_t(v>>(i*8));}
}
u8 Read_U8(u32 a){return Read(a,1);}u16 Read_U16(u32 a){return Read(a,2);}u32 Read_U32(u32 a){return Read(a,4);}
void Write_U8(u8 v,u32 a){Write(v,a,1);}void Write_U16(u16 v,u32 a){Write(v,a,2);}void Write_U32(u32 v,u32 a){Write(v,a,4);}
}
''' +handler+r'''
int main() {
 std::vector<uint8_t> ram(0x2000000),vram(0x200000),scratch(0x4000);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();Wii::addressSpace.scratch=scratch.data();Wii::addressSpace.RebuildPages();
 uint32_t seed=0x98765432;
 const auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 const unsigned kinds[]={32,33,34,35,36,37,38,40,41,42,43,46};
 const uint32_t bases[]={0x08001000,0x09ffff00,0x04001000,0x041fff00,0x047fff00,0x10000,0x13f00};
 for(unsigned i=0;i<48000;++i) {
   const unsigned kind=kinds[i%12],rs=1+random()%31,rt=random()%32;
   const bool merge=kind==34||kind==38||kind==42||kind==46;
   const unsigned bytes=(kind==32||kind==36||kind==40)?1:(kind==33||kind==37||kind==41)?2:4;
   uint32_t address=bases[(i/12)%7]+(random()&252);
   address|=merge?random()&3:random()&(4-bytes);
   address|=(random()&3)<<30;
   const int16_t displacement=int16_t(random());
   uint32_t fast[32];for(auto &r:regs)r=random();regs[0]=0;
   regs[rs]=address-uint32_t(int32_t(displacement));
   memcpy(fast,regs,sizeof(fast));
   const uint32_t op=(kind<<26)|(rs<<21)|(rt<<16)|uint16_t(displacement);
   const uint32_t aligned=address&~3u,old=random();Memory::Write_U32(old,aligned);
   refPC=0x08804000;Int_ITypeMem(op);
   const uint32_t expected=Memory::Read_U32(aligned);Memory::Write_U32(old,aligned);
   uint32_t fastPC=0x08804000;assert(Wii::InterpretMemory(op,fast,fastPC));
   assert(!memcmp(fast,regs,sizeof(fast))&&fastPC==refPC&&Memory::Read_U32(aligned)==expected);
 }
 uint32_t fast[32]={},pc=0x08000000;
 for(uint32_t address:{0u,0x0a000000u,0x04800000u,0x14000u,0x08000001u}) {
   fast[1]=address;const auto copy=ram;
   assert(!Wii::InterpretMemory(0xac220000,fast,pc)&&pc==0x08000000&&ram==copy);
 }
 puts("pass: 48000 loads/stores/unaligned merges against original handler, RAM/VRAM mirrors/scratch, aliases, signed offsets, zero register and rejected ranges");
}
'''
with tempfile.TemporaryDirectory(prefix='memory-fast-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
