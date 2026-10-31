// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/OpaqueSpan.h"
#include "Wii/tests/OpaqueSpanSpeedReference.h"
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cassert>
namespace Wii {AddressSpace addressSpace;}
extern "C" int __lsan_is_turned_off(){return 1;}
static volatile unsigned checksum;
struct Pixel {
  const uint8_t *source;unsigned mode,calls;
  Pixel(const uint8_t *p,unsigned m):source(p),mode(m),calls(0){}
  uint32_t operator()(unsigned x,unsigned y) {
    ++calls;
    if(mode==0)return ((x*13&255)<<16)|((y*17&255)<<8)|((x^y)&255);
    const unsigned offset=((y*512+(mode==2&&x?x-1:x))*4)&0x1fffff;
    return unsigned(source[offset])<<16|unsigned(source[offset+1])<<8|source[offset+2];
  }
};
using Draw=bool(*)(uint32_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,Pixel);
__attribute__((noinline)) static bool Previous(uint32_t a,unsigned s,unsigned f,unsigned x,unsigned y,unsigned w,unsigned h,Pixel p){return OpaqueSpeedReference::DrawOpaqueSurface(a,s,f,x,y,w,h,p);}
__attribute__((noinline)) static bool Updated(uint32_t a,unsigned s,unsigned f,unsigned x,unsigned y,unsigned w,unsigned h,Pixel p){return Wii::DrawOpaqueSurface(a,s,f,x,y,w,h,p);}
static unsigned polls,limit;
static bool Cancel(){return polls++==limit;}
int main(){
 std::vector<uint8_t>vram(2<<20),ram(2<<20),texture(2<<20);
 Wii::addressSpace.vram=vram.data();Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.RebuildPages();
 unsigned seed=0x738457;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(auto &v:texture)v=random();
 for(unsigned trial=0;trial<800;++trial){
  for(auto &v:vram)v=random();
  const unsigned f=trial%4,mode=trial%3,w=1+random()%480,h=1+random()%8;
  const uint32_t address=trial%7==0?0x047ffff0:trial%11==0?0x04000001:trial%2?0x441ffff0:0x04000000;
  const auto initial=vram;Pixel p{mode==2?vram.data():texture.data(),mode};
  const bool a=Previous(address,512,f,0,0,w,h,p);const auto expected=vram;vram=initial;
  p.source=mode==2?vram.data():texture.data();const bool b=Updated(address,512,f,0,0,w,h,p);
  assert(a==b&&vram==expected);
 }
 for(unsigned f=0;f<4;++f)for(limit=0;limit<=8;++limit){
  std::fill(vram.begin(),vram.end(),0xa7);polls=0;
  assert(OpaqueSpeedReference::DrawOpaqueSurface(0x04000000,512,f,0,0,479,8,Pixel{texture.data(),1},Cancel));
  const auto expected=vram;const unsigned expectedPolls=polls;
  std::fill(vram.begin(),vram.end(),0xa7);polls=0;
  assert(Wii::DrawOpaqueSurface(0x04000000,512,f,0,0,479,8,Pixel{texture.data(),1},Cancel));
  assert(vram==expected&&polls==expectedPolls);
 }
 puts("pass: 800 opaque-span byte comparisons, 36 cancellation comparisons, all formats, partial widths, mirrors, rejection and sequential feedback");
#ifdef BENCHMARK
 const Draw draws[]={Previous,Updated};
 for(unsigned f=0;f<4;++f)for(unsigned mode=0;mode<3;++mode)for(unsigned width:{8u,479u}){
  std::vector<double>samples[2];
  for(unsigned round=0;round<9;++round)for(unsigned turn=0;turn<2;++turn){
   const unsigned which=(turn+round)%2;std::fill(vram.begin(),vram.end(),0xa7);
   Pixel p{mode==2?vram.data():texture.data(),mode};
   assert(draws[which](0x04000000,512,f,0,0,width,8,p));
   const auto start=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<2000;++repeat)assert(draws[which](0x04000000,512,f,0,0,width,8,p));
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   checksum+=vram[0];samples[which].push_back(ms);
   printf("opaque,%u,%u,%u,%u,%s,%.6f\n",f,mode,width,round,which?"updated":"previous",ms);
  }
  for(auto &v:samples)std::sort(v.begin(),v.end());
  printf("HOST opaque fmt %u mode %u width %u: previous %.6f ms updated %.6f ms (median9, 2000 eight-row spans)\n",f,mode,width,samples[0][4],samples[1][4]);
 }
#endif
}
