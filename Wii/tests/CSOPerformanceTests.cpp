// SPDX-License-Identifier: GPL-2.0-or-later
#if defined(__SANITIZE_ADDRESS__)
// LeakSanitizer cannot enumerate threads in the restricted host environment.
extern "C" int __lsan_is_turned_off() {return 1;}
#endif
#include "Core/FileSystems/BlockDevices.h"
#include "Core/Loaders.h"
#include "Common/Log.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <cstdarg>
#include "Wii/CpuProfile.h"
static unsigned inits=0,ends=0,resets=0;
static bool failInit=false,failReset=false;
extern "C" int __real_inflateInit2_(z_streamp,int,const char *,int);
extern "C" int __real_inflateEnd(z_streamp);
extern "C" int __real_inflateReset(z_streamp);
extern "C" int __wrap_inflateInit2_(z_streamp z,int bits,const char *version,int bytes) {++inits;if(failInit){failInit=false;return Z_MEM_ERROR;}return __real_inflateInit2_(z,bits,version,bytes);}
extern "C" int __wrap_inflateEnd(z_streamp z) {++ends;return __real_inflateEnd(z);}
extern "C" int __wrap_inflateReset(z_streamp z) {++resets;if(failReset){failReset=false;return Z_STREAM_ERROR;}return __real_inflateReset(z);}
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char *fmt,...) {if(!getenv("WII_CSO_PROFILE"))return;va_list a;va_start(a,fmt);vprintf(fmt,a);puts("");va_end(a);}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return getenv("WII_CSO_PROFILE")!=nullptr;}
class Image : public FileLoader {
public:
 std::vector<u8> data,raw;std::vector<size_t> offsets;size_t payload=0,truncateAt=0;
 void put(size_t at,u32 n) {for(unsigned i=0;i<4;++i)data[at+i]=n>>(8*i);}
 Image(unsigned frameBytes,unsigned frames) : data(24+(frames+1)*4),raw(frameBytes*frames) {
  memcpy(data.data(),"CISO",4);put(4,24);put(8,raw.size());put(12,0);put(16,frameBytes);data[20]=1;
  payload=data.size();
  for(unsigned f=0;f<frames;++f) {
   u8 *source=raw.data()+f*frameBytes;
   for(unsigned i=0;i<frameBytes;++i)source[i]=u8(f*37+(i/31));
   const bool plain=f%3==1;
   offsets.push_back(data.size());put(24+f*4,u32(data.size())|(plain?0x80000000u:0));
   if(plain)data.insert(data.end(),source,source+frameBytes);
   else {
    std::vector<u8> compressed(compressBound(frameBytes));z_stream z{};
    assert(deflateInit2(&z,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY)==Z_OK);
    z.next_in=source;z.avail_in=frameBytes;z.next_out=compressed.data();z.avail_out=compressed.size();
    assert(deflate(&z,Z_FINISH)==Z_STREAM_END);compressed.resize(z.total_out);deflateEnd(&z);
    data.insert(data.end(),compressed.begin(),compressed.end());
   }
  }
  offsets.push_back(data.size());put(24+frames*4,data.size());
 }
 bool Exists() override{return true;}bool IsDirectory() override{return false;}
 s64 FileSize() override{return data.size();}std::string Path() const override{return "fixture.cso";}
 size_t ReadAt(s64 at,size_t bytes,size_t count,void *out,Flags=Flags::NONE) override {
  if(at<0||!bytes||size_t(at)>=data.size()||count>size_t(-1)/bytes)return 0;
  size_t end=truncateAt&&size_t(at)>=payload?std::min(truncateAt,data.size()):data.size();
  if(size_t(at)>=end)return 0;
  const size_t length=std::min(bytes*count,end-size_t(at));memcpy(out,data.data()+at,length);return length/bytes;
 }
};
int main() {
 if(getenv("WII_CSO_BENCH")) {
  Image image(4096,12);CISOFileBlockDevice device(&image);u8 out[2048];
  assert(device.ReadBlocks(0,1,out));
  Wii::CpuProfileConfigure(getenv("WII_CSO_PROFILE")!=nullptr,1);
  const auto start=std::clock();
  for(unsigned i=0;i<2000000;++i)assert(device.ReadBlocks(0,1,out));
  assert(!memcmp(out,image.raw.data(),2048));
  const double us=double(std::clock()-start)*1000000/CLOCKS_PER_SEC;
  printf("cached-single-sector calls=2000000 cpu-us=%.0f\n",us);
  Wii::CpuProfileDump("cached-single-sector");return 0;
 }

 for(unsigned frameBytes:{2048u,4096u,65536u}) {
  Image image(frameBytes,12);const unsigned beforeInit=inits,beforeEnd=ends;
  {
   CISOFileBlockDevice device(&image);assert(device.GetNumBlocks()==image.raw.size()/2048);
   const unsigned blocks=device.GetNumBlocks();std::vector<u8> out(image.raw.size()+32,0xa5);
   for(unsigned pass=0;pass<3;++pass)for(unsigned i=0;i<blocks;++i) {
    assert(device.ReadBlock(i,out.data()));assert(!memcmp(out.data(),image.raw.data()+i*2048,2048));
   }
   for(unsigned start=0;start<blocks;start+=3) {
    const unsigned count=std::min(7u,blocks-start);
    assert(device.ReadBlocks(start,count,out.data()));assert(!memcmp(out.data(),image.raw.data()+start*2048,count*2048));
   }
   assert(device.ReadBlocks(blocks,0,nullptr));assert(!device.ReadBlocks(blocks+1,0,nullptr));
   assert(!device.ReadBlock(0,nullptr)&&!device.ReadBlocks(0,1,nullptr));
   assert(!device.ReadBlocks(blocks,1,out.data())&&!device.ReadBlocks(0,-1,out.data()));
   auto check=[&](){std::vector<u8> block(2048);for(unsigned i=0;i<blocks;++i){assert(device.ReadBlock(i,block.data()));assert(!memcmp(block.data(),image.raw.data()+i*2048,2048));}};
   std::thread a(check),b(check);a.join();b.join();
   assert(inits==beforeInit+1&&ends==beforeEnd);
  }
  assert(ends==beforeEnd+1);
 }
 // Corrupt compressed data and short raw/compressed frames must fail. Repair
 // the source and retry on the same retained decoder to verify reset recovery.
 for(unsigned frameBytes:{2048u,4096u}) {
  Image image(frameBytes,6);CISOFileBlockDevice device(&image);std::vector<u8> out(image.raw.size());
  const auto clean=image.data;
  image.data[image.offsets[2]]=0x07; // invalid DEFLATE block type
  assert(!device.ReadBlocks(0,frameBytes/2048*3,out.data()));
  image.data=clean;
  assert(device.ReadBlocks(0,device.GetNumBlocks(),out.data()));assert(!memcmp(out.data(),image.raw.data(),image.raw.size()));
  image.data[image.offsets[3]]=0x07;
  assert(!device.ReadBlock(3*frameBytes/2048,out.data()));
  image.data=clean;assert(device.ReadBlock(3*frameBytes/2048,out.data()));
  for(unsigned frame:{1u,3u}) {
   image.truncateAt=image.offsets[frame]+1;
   assert(!device.ReadBlocks(frame*frameBytes/2048,frameBytes/2048+1,out.data()));
   image.truncateAt=0;assert(device.ReadBlocks(frame*frameBytes/2048,frameBytes/2048+1,out.data()));
  }
 }
 // A raw frame's declared extent must cover the bytes being copied.
 {Image image(4096,6);image.put(24+2*4,image.offsets[1]+1);
  CISOFileBlockDevice device(&image);u8 out[4096];assert(!device.ReadBlocks(2,2,out));}
 // Initialization failure and reset failure cannot poison the device.
 {Image image(2048,6);CISOFileBlockDevice device(&image);u8 out[2048];
  failInit=true;assert(!device.ReadBlock(0,out));assert(device.ReadBlock(0,out));
  failReset=true;assert(device.ReadBlock(2,out));
  assert(!memcmp(out,image.raw.data()+4096,2048));}
 // Force a failed partial-frame decode after an earlier memo in the same batch.
 {Image image(4096,6);CISOFileBlockDevice device(&image);std::vector<u8> out(image.raw.size());
  const auto clean=image.data;image.data[image.offsets[2]]=0x07;
  assert(!device.ReadBlocks(1,4,out.data()));image.data=clean;
  assert(device.ReadBlock(1,out.data()));assert(!memcmp(out.data(),image.raw.data()+2048,2048));}
 printf("pass: mixed raw/compressed CSO frames (2/4/64 KiB), exact bytes, serialized readers, one inflater init per device, cleanup, corrupt/short-read failure and same-device recovery; inits=%u resets=%u ends=%u\n",inits,resets,ends);
}
