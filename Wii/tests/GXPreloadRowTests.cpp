// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GXUpload.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
extern "C" int __lsan_is_turned_off(){return 1;}
int main(){
 unsigned cases=0,seed=0x594128;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(unsigned format=0;format<4;++format)for(unsigned count=1;count<=512;++count){
  const unsigned width=(count+3)&~3u,bpp=format==3?4:2;
  std::vector<uint8_t>src(count*bpp+1),actual(width*12*4+64),expected;
  for(auto &v:src)v=random();for(auto &v:actual)v=random();expected=actual;
  // Deliberately unaligned, exact-length input rows; untouched bytes and
  // sentinel bands must survive each individual row conversion.
  for(unsigned y=0;y<12;++y){
   for(unsigned x=0;x<width;++x)
    Wii::PackGXPixel(expected.data()+32,width,x,y,Wii::DecodePixel(src.data()+1+std::min(x,count-1)*bpp,format));
   Wii::UploadPreloadRow(actual.data()+32,width,y,src.data()+1,count,format);
   assert(actual==expected);++cases;
  }
 }
 printf("pass: %u exact GX preload rows, all formats, widths 1..512, partial tiles, unaligned input and guards\n",cases);
}
