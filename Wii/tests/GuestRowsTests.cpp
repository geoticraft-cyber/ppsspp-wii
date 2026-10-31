// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GuestCopy.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <chrono>
namespace Wii {AddressSpace addressSpace;}
int main() {
  std::vector<uint8_t> ram(0x200000),vram(0x200000);
  Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();
  Wii::addressSpace.vram=vram.data();Wii::addressSpace.RebuildPages();
  uint32_t seed=12345;const auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned trial=0;trial<1200;++trial) {
    for(unsigned i=0;i<ram.size();++i)ram[i]=uint8_t(i*13+trial);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*7+trial);
    const auto originalRAM=ram,originalVRAM=vram;
    const unsigned width=1+random()%2048,height=random()%80;
    const unsigned sp=width+(trial%3?0:random()%64),dp=width+(trial%4?0:random()%64);
    uint32_t src=0x08010000+(random()%128),dst=0x08100000+(random()%128);
    switch(trial%7) {
      case 0:dst=src+width;break;case 1:dst=src-width;break;
      case 2:src=0x041fff80;dst=0x08030000;break;
      case 3:src=0x04010000;dst=0x44210000+width;break;
      case 4:src=0x081fff00;break;case 5:dst=0x14000;break;
    }
    bool expected=true;
    for(unsigned row=0;row<height;++row)if(!Wii::CopyGuest(dst+row*dp,src+row*sp,width))expected=false;
    const auto expectedRAM=ram,expectedVRAM=vram;
    ram=originalRAM;vram=originalVRAM;
    assert(Wii::CopyGuestRows(dst,src,dp,sp,width,height)==expected);
    assert(ram==expectedRAM&&vram==expectedVRAM);
  }
  puts("PASS: 1200 GE row-copy comparisons: packed, padded, overlapping, aliases, mirrors, invalid rows and zero height");
  const auto run=[&](bool bulk) {
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<500;++i) {
      if(bulk)assert(Wii::CopyGuestRows(0x08100000,0x08000000,1920,1920,1920,272));
      else for(unsigned row=0;row<272;++row)assert(Wii::CopyGuest(0x08100000+row*1920,0x08000000+row*1920,1920));
    }
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  };
  printf("HOST packed GE copy: rows %.3f ms, bulk %.3f ms (not Wii FPS)\n",run(false),run(true));
}
