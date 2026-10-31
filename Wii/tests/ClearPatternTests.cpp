// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/BufferFill.h"
#include <vector>
#include <cassert>
#include <cstdio>
namespace Wii {AddressSpace addressSpace;}
static uint32_t seed=0x526174;
static uint32_t Random(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main() {
  std::vector<uint8_t> vram(0x200000+64),ram(4096+64),scratch(16384+64);
  Wii::addressSpace.vram=vram.data()+32;Wii::addressSpace.ram=ram.data()+32;
  Wii::addressSpace.ramSize=4096;Wii::addressSpace.scratch=scratch.data()+32;
  unsigned cases=0;
  for(unsigned trial=0;trial<2400;++trial) {
    unsigned bytes=trial&1?2:4;
    uint8_t *base;
    switch((trial/2)%6) {
      case 0:base=Wii::addressSpace.vram+0x1ffff0;break;
      case 1:base=Wii::addressSpace.vram;break;
      case 2:base=Wii::addressSpace.ram+4096-32;break;
      case 3:base=Wii::addressSpace.scratch+16384-32;break;
      case 4:base=nullptr;break;
      default:base=Wii::addressSpace.ram+1;break;
    }
    for(auto *buffer:{&vram,&ram,&scratch}) {
      for(unsigned i=0;i<512;++i)(*buffer)[i]=uint8_t(Random());
      for(unsigned i=buffer->size()-512;i<buffer->size();++i)(*buffer)[i]=uint8_t(Random());
    }
    auto savedV=vram,savedR=ram,savedS=scratch;
    const int x=int(Random()%36)-4,y=int(Random()%4)-1,stride=trial%13?64:-1;
    const unsigned count=Random()%72;
    uint32_t value=Random(),mask=Random();
    switch(trial%8) {
      case 0:mask=0;break;case 1:mask=0xff000000;break;
      case 2:mask=0xffff;break;case 3:mask=0xf000;break;
      case 4:mask=~0u;break;case 5:mask=0x8000;break;
      case 6:mask=0;value=0x77777777;break;
    }
    mask&=bytes==2?0xffffu:~0u;value&=bytes==2?0xffffu:~0u;
    bool uniform=!mask&&(value&255)==((value>>8)&255)&&(bytes==2||(value&65535)==(value>>16));
    if(uniform)Wii::FillBuffer(base,x,y,stride,bytes,count,uint8_t(value));
    else for(unsigned col=0;col<count;++col) {
      auto *p=Wii::BufferAccess(base,x+col,y,stride,bytes);
      if(!p)continue;
      uint32_t old=uint32_t(p[0])|uint32_t(p[1])<<8;
      if(bytes==4)old|=uint32_t(p[2])<<16|uint32_t(p[3])<<24;
      uint32_t out=(old&mask)|(value&~mask);
      for(unsigned b=0;b<bytes;++b)p[b]=uint8_t(out>>(b*8));
    }
    const auto expectedV=vram,expectedR=ram,expectedS=scratch;
    // Keep backing addresses stable while restoring their bytes.
    std::copy(savedV.begin(),savedV.end(),vram.begin());
    std::copy(savedR.begin(),savedR.end(),ram.begin());
    std::copy(savedS.begin(),savedS.end(),scratch.begin());
    Wii::FillBufferPattern(base,x,y,stride,bytes,count,value,mask);
    assert(vram==expectedV&&ram==expectedR&&scratch==expectedS);++cases;
  }
  printf("pass: %u pattern clears against per-pixel reference, masks, negative starts, backing bounds, VRAM wrap, alignment and guards\n",cases);
}
