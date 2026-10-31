// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/OpaqueSpan.h"
#include <vector>
#include <cassert>
#include <cstdio>
namespace Wii {AddressSpace addressSpace;}
extern "C" int __lsan_is_turned_off() {return 1;}
int main() {
  std::vector<uint8_t> vram(2*1024*1024),ram(32*1024*1024);
  Wii::addressSpace.vram=vram.data();Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();
  auto pixel=[](unsigned x,unsigned y) {return ((x*13&255)<<16)|((y*17&255)<<8)|((x^y)&255);};
  for(unsigned format=0;format<4;++format) {
    const unsigned bpp=format==3?4:2;
    std::fill(vram.begin(),vram.end(),0xa7);
    assert(Wii::DrawOpaqueSurface(0x441ffff0,512,format,0,0,480,8,pixel));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<480;++x) {
      const unsigned offset=(0x1ffff0+(y*512+x)*bpp)&0x1fffff;
      uint32_t actual=0;for(unsigned i=0;i<bpp;++i)actual|=uint32_t(vram[offset+i])<<(i*8);
      const uint32_t mask=format==3?0xff000000:format==1?0x8000:format==2?0xf000:0;
      const uint32_t old=format==3?0xa7a7a7a7:0xa7a7;
      const uint32_t expected=Wii::RGBToPSPPixel(pixel(x,y),0,format)|(old&mask);
      assert(actual==expected);
    }
  }
  auto before=vram;assert(!Wii::DrawOpaqueSurface(0x047ffff0,512,3,0,0,480,8,pixel));assert(vram==before);
  assert(!Wii::DrawOpaqueSurface(0x04000001,512,3,0,0,480,8,pixel));assert(vram==before);
  puts("pass: opaque span reference colors, all frame formats, preserved stencil, VRAM mirrors and rejected transfer preflight");
}
