// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GXDirtyUpload.h"
#include "Wii/tests/GXUploadReference.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main() {
  unsigned seed=91271,cases=0;
  for(unsigned width=4;width<=480;width+=4)
  for(unsigned format=0;format<4;++format)
  for(unsigned native=0;native<2;++native)
  for(unsigned y: {0u,4u,12u}) {
    const unsigned bpp=native?2:4,bytes=width*4*bpp,offset=y*width*bpp;
    std::vector<uint8_t> source(4*(width*4+13)),actual(offset+bytes+64,0x67),expected(actual);
    for(auto &b:source) {seed=seed*1664525+1013904223;b=seed>>24;}
    const uint8_t *rows[4];
    for(unsigned r=0;r<4;++r)rows[r]=source.data()+r*(width*4+13)+1;
    std::vector<unsigned> flushA,flushB;
    const auto flush=[&](std::vector<unsigned>& log,uint8_t* base,uint8_t* p,unsigned n) {
      assert(p==base+32+offset&&n==bytes);
      log.push_back(n);
    };
    unsigned a=Wii::UploadDirtyBandReuse(actual.data()+32,width,y,rows,format,native,true,
      [&](uint8_t* p,unsigned n){flush(flushA,actual.data(),p,n);},
      [](unsigned)->bool{assert(false);return true;});
    unsigned b=Previous::UploadDirtyBandReuse(expected.data()+32,width,y,rows,format,native,true,
      [&](uint8_t* p,unsigned n){flush(flushB,expected.data(),p,n);},
      [](unsigned)->bool{assert(false);return true;});
    assert(a==b&&a==bytes&&actual==expected&&flushA==flushB&&flushA.size()==1);
    ++cases;
  }
  printf("pass: %u forced GX bands vs previous implementation, all widths/formats, padded unaligned rows, offsets, flushes and guards\n",cases);
}
