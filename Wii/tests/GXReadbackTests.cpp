// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GXReadback.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
// LeakSanitizer cannot enumerate threads in the restricted host runner.
extern "C" int __lsan_is_turned_off() {return 1;}
int main() {
  unsigned cases=0;
  for(unsigned count:{1u,3u,4u,5u,63u,256u,479u,512u}) {
    const unsigned width=(count+3)&~3u;
    std::vector<uint8_t> tiled(width*8*4);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<width;++x)
      Wii::PackGXPixel(tiled.data(),width,x,y,((x*97+y*71)*0x319b1)^0xabcdef);
    for(unsigned format=0;format<4;++format)for(unsigned y=0;y<8;++y)
      for(uint32_t mask:{0u,0xffffffu,0x00ff00u,0x7823c9u,0xff000000u}) {
        const unsigned bpp=format==3?4:2;
        std::vector<uint8_t> actual(count*bpp+64),expected;
        for(unsigned i=0;i<actual.size();++i)actual[i]=uint8_t(i*17+y*31);
        expected=actual;
        // Reference is the previous product loop, independent of the grouped walk.
        for(unsigned x=0;x<count;++x) {
          auto *dst=expected.data()+32+x*bpp;
          uint32_t rgb=Wii::UnpackGXPixel(tiled.data(),width,x,y);
          const uint32_t rgbMask=((mask&255)<<16)|(mask&0xff00)|((mask>>16)&255);
          if(mask)rgb=(Wii::DecodePixel(dst,format)&rgbMask)|(rgb&~rgbMask);
          if(format==3){dst[0]=rgb>>16;dst[1]=rgb>>8;dst[2]=rgb;}
          else {
            const unsigned old=unsigned(dst[0])|unsigned(dst[1])<<8;
            const unsigned stencil=format==1?0x8000:format==2?0xf000:0;
            const unsigned pixel=Wii::RGBToPSPPixel(rgb,0,format)|(old&stencil);
            dst[0]=pixel;dst[1]=pixel>>8;
          }
        }
        Wii::CopyGXColorRow(actual.data()+32,tiled.data(),width,y,count,format,mask);
        assert(actual==expected);++cases;
      }
  }
  printf("pass: %u grouped readback/reference comparisons, all formats, partial tiles, color masks and stencil/guard preservation\n",cases);
}
