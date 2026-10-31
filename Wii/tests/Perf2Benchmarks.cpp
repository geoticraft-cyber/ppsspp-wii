#include "Wii/GXDirtyUpload.h"
#include "Wii/ScalarInterpreter.h"
#include "Wii/CachedInterpreter.h"
#include "Wii/tests/GXUploadReference.h"
#include <chrono>
#include <vector>
#include <cstdio>
#include <cassert>
#include <algorithm>
static volatile unsigned checksum;
struct Flush {void operator()(uint8_t *,unsigned n)const {checksum+=n;}};
__attribute__((noinline)) static void Scalar(uint32_t op,uint32_t *r,uint32_t &pc) {assert(Wii::InterpretScalar(op,r,pc));}
__attribute__((noinline)) static bool Pair(uint32_t a,uint32_t b,uint32_t *r,uint32_t &pc) {return Wii::FoldConstantPair(a,b,r,pc);}
int main(){
 std::vector<uint8_t> src(480*272*2),out(480*272*4),old(out.size());
 for(unsigned i=0;i<src.size();++i)src[i]=i*31+(i>>8);
 for(unsigned format=0;format<3;++format) {
  std::vector<double> a,b;
  for(unsigned pass=0;pass<6;++pass)for(unsigned which=0;which<2;++which){
   const bool newer=(which+pass)%2;
   auto start=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<200;++repeat)for(unsigned y=0;y<272;y+=4){
    const uint8_t *rows[4];for(unsigned r=0;r<4;++r)rows[r]=src.data()+(y+r)*480*2;
    if(newer)Wii::UploadDirtyBand(out.data(),480,y,rows,format,false,true,Flush());
    else Previous::UploadDirtyBand(old.data(),480,y,rows,format,false,true,Flush());
   }
   (newer?b:a).push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  assert(out==old);std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
  printf("gx format %u old %.3f new %.3f ms (200 full 480x272 uploads, median6)\n",format,(a[2]+a[3])/2,(b[2]+b[3])/2);
 }
 for(unsigned which=0;which<2;++which){
  uint32_t r[32]={},pc=0;auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<3000000;++i) {
   uint32_t first=0x3c080000|(i&65535),second=(i&1?0x35080000:0x25080000)|((i*13)&65535);
   if(which)assert(Pair(first,second,r,pc));else{Scalar(first,r,pc);Scalar(second,r,pc);}checksum+=r[8];
  }
  printf("cpu constant pairs %s %.3f ms checksum %u\n",which?"fused":"scalar",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),checksum);
 }
}
