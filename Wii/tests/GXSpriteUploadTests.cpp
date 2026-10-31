// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/GXSpriteUpload.h"
#include "Wii/tests/GXSpriteUploadReference.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <chrono>
struct Read {
  unsigned mode=0,width=0,height=0;unsigned *calls=nullptr;
  uint32_t operator()(unsigned x,unsigned y) const {
    if(calls)++*calls;
    uint32_t rgb=((x*71+y*97)*0x13ab75)^0xe357ab91;
    if(mode==1)rgb^=0x0f5382d1;
    if(mode==2&&x==3&&y==2)rgb^=0x0f5382d1;
    if(mode==3&&x<4&&(y==3||y==4))rgb^=0x0f5382d1;
    return rgb;
  }
};
static unsigned Invoke(bool previous,uint8_t *out,unsigned w,unsigned h,unsigned format,bool compare,Read read,std::vector<unsigned>&tiles,unsigned &flushCalls) {
 const unsigned tileBytes=format==3?64:32;
 auto flush=[&](uint8_t *p,unsigned n){
  assert(p>=out&&p+n<=out+w*h*(format==3?4:2));
  assert((p-out)%tileBytes==0&&n&&n%tileBytes==0);++flushCalls;
  for(unsigned i=0;i<n/tileBytes;++i)++tiles[(p-out)/tileBytes+i];
 };
 if(format==3)return previous?PreviousGX::UploadSpritePatch(out,w,h,read,compare,flush):Wii::UploadSpritePatch(out,w,h,read,compare,flush);
 return previous?PreviousGX::UploadNative16Patch(out,w,h,read,format,compare,flush):Wii::UploadNative16Patch(out,w,h,read,format,compare,flush);
}
static volatile unsigned checksum;
int main() {
 unsigned cases=0;
 for(unsigned w:{4u,12u,128u,480u,512u})for(unsigned h:{4u,8u,64u})
 for(unsigned format=0;format<4;++format)for(unsigned mode=0;mode<5;++mode) {
  const unsigned bytes=w*h*(format==3?4:2),tiles=w*h/16;
  std::vector<uint8_t> a(bytes+64,0x67),b(a);
  std::vector<unsigned> init(tiles),flushA(tiles),flushB(tiles);unsigned fa=0,fb=0,callsA=0,callsB=0;
  Read read;Invoke(true,a.data()+32,w,h,format,false,read,init,fa);std::copy(a.begin(),a.end(),b.begin());
  read.mode=mode?mode-1:0;read.calls=&callsA;fa=0;
  unsigned n=Invoke(true,a.data()+32,w,h,format,mode!=0,read,flushA,fa);
  read.calls=&callsB;
  unsigned m=Invoke(false,b.data()+32,w,h,format,mode!=0,read,flushB,fb);
  assert(a==b&&n==m&&flushA==flushB&&fb<=fa&&callsA==w*h&&callsA==callsB);
  if(mode==0||mode==2)assert(fb==1);
  ++cases;
 }
 printf("pass: %u sprite uploads vs previous implementation, RGBA/native16, all change modes, exact changed-tile flush coverage, callbacks and guards\n",cases);
#ifdef BENCHMARK
 const unsigned w=256,h=256;
 for(unsigned format:{3u,0u,1u,2u})for(unsigned mode=0;mode<4;++mode) {
  std::vector<uint8_t> a(w*h*4),b(a);
  const char *names[]={"full","unchanged","animated","sparse"};
  std::vector<double> times[2];
  for(unsigned trial=0;trial<7;++trial)for(unsigned slot=0;slot<2;++slot) {
   unsigned which=(slot+trial)%2;
   auto *out=which?b.data():a.data();Read read;
   auto quiet=[](uint8_t*,unsigned){};
   if(format==3)PreviousGX::UploadSpritePatch(out,w,h,read,false,quiet);
   else PreviousGX::UploadNative16Patch(out,w,h,read,format,false,quiet);
   auto flush=[](uint8_t*,unsigned n){checksum+=n;};
   auto start=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<400;++repeat) {
    read.mode=mode==2?(repeat&1):mode==3?(repeat&1?2:0):0;
    if(format==3)checksum+=which?Wii::UploadSpritePatch(out,w,h,read,mode!=0,flush):PreviousGX::UploadSpritePatch(out,w,h,read,mode!=0,flush);
    else checksum+=which?Wii::UploadNative16Patch(out,w,h,read,format,mode!=0,flush):PreviousGX::UploadNative16Patch(out,w,h,read,format,mode!=0,flush);
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();times[which].push_back(ms);
   printf("sample,%u,%s,%u,%s,%.3f\n",format,names[mode],trial,which?"updated":"previous",ms);
  }
  for(auto &v:times)std::sort(v.begin(),v.end());
  printf("HOST GX format %u %s: previous %.3f ms updated %.3f ms (median7, 400 uploads; mock flush, not GX timing)\n",format,names[mode],times[0][3],times[1][3]);
 }
#endif
}
