#!/usr/bin/env python3
"""Prepared pixel addresses must exactly match bounded/mirrored reference access."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include "Wii/PreparedBuffer.h"
#include <vector>
#include <cassert>
#include <cstdio>
namespace Wii {AddressSpace addressSpace;}
int main(){
 std::vector<uint8_t> ram(1<<20),vram(1<<21),scratch(16384);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();
 Wii::addressSpace.vram=vram.data();Wii::addressSpace.scratch=scratch.data();
 uint32_t seed=0xab75213;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned checks=0;
 for(unsigned trial=0;trial<50000;++trial){
   uint8_t *p=nullptr;
   switch(trial%6){case 0:p=ram.data()+random()%ram.size();break;case 1:p=vram.data()+random()%vram.size();break;
     case 2:p=scratch.data()+random()%scratch.size();break;case 3:p=vram.data()+0x1ffffc;break;
     case 4:p=ram.data()+ram.size()-4;break;}
   const unsigned bytes=trial&1?2:4;
   const int pitch=trial%7?random()%1025:int(random()%100000)-20;
   Wii::PreparedBuffer prepared;prepared.Prepare(p,pitch,bytes);
   for(unsigned i=0;i<40;++i){
     const int x=i<20?int(random()%1025):int(random()%100000)-10;
     const int y=i<20?int(random()%1025):int(random()%100000)-10;
     assert(prepared.At(x,y)==Wii::BufferAccess(p,x,y,pitch,bytes));++checks;
   }
 }
 printf("pass: %u prepared/reference addresses, RAM bounds, VRAM mirrors, scratch, null/misaligned, wide coordinates and pitches\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='prepared-buffer-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
