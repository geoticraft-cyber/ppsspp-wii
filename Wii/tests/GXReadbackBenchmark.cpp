// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GXReadback.h"
#include "Wii/tests/GXReadbackReference.h"
#include <vector>
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstdio>
__attribute__((noinline)) static void Previous(uint8_t *out,const uint8_t *in,unsigned width,unsigned y,unsigned n,unsigned fmt,uint32_t mask) {PreviousReadback::CopyGXColorRow(out,in,width,y,n,fmt,mask);}
__attribute__((noinline)) static void Updated(uint8_t *out,const uint8_t *in,unsigned width,unsigned y,unsigned n,unsigned fmt,uint32_t mask) {Wii::CopyGXColorRow(out,in,width,y,n,fmt,mask);}
static volatile unsigned checksum;
int main() {
 std::vector<uint8_t> in(512*8*4),out(512*4),expected;
 uint32_t seed=0x592451;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(auto &v:in)v=random();for(auto &v:out)v=random();
 for(unsigned trial=0;trial<12000;++trial) {
  const unsigned fmt=trial%4,n=1+random()%512,y=random()%8;
  uint32_t mask=random();if(trial%7==0)mask=0;if(trial%7==1)mask=~0u;
  for(auto &v:out)v=random();expected=out;
  Previous(expected.data(),in.data(),512,y,n,fmt,mask);Updated(out.data(),in.data(),512,y,n,fmt,mask);
  assert(out==expected);
 }
 puts("pass: 12000 readback comparisons against previous implementation, random masks, partial groups, alpha/stencil and guards");
#ifdef BENCHMARK
 using Copy=void(*)(uint8_t*,const uint8_t*,unsigned,unsigned,unsigned,unsigned,uint32_t);
 const Copy funcs[]={Previous,Updated};
 for(unsigned fmt=0;fmt<4;++fmt)for(uint32_t mask:{0u,0x7823c9u,0xffffffu}) {
  std::vector<double> times[2];
  for(unsigned trial=0;trial<7;++trial)for(unsigned slot=0;slot<2;++slot) {
   unsigned which=(slot+trial)%2;
   auto start=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<30000;++repeat)funcs[which](out.data(),in.data(),512,repeat&7,479,fmt,mask);
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   times[which].push_back(ms);checksum+=out[1];
   printf("sample,%u,%06x,%u,%s,%.3f\n",fmt,mask,trial,which?"updated":"previous",ms);
  }
  for(auto &v:times)std::sort(v.begin(),v.end());
  printf("HOST readback fmt %u mask %06x: previous %.3f ms updated %.3f ms (median7, 30000 rows; not GX timing)\n",fmt,mask,times[0][3],times[1][3]);
 }
#endif
}
