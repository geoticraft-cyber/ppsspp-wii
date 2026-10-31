extern "C" int __lsan_is_turned_off() {return 1;}
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/FileSystems/BlockDevices.h"
#include "Core/Loaders.h"
#include "Common/Log.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <algorithm>
extern "C" {
#include "ext/libkirk/amctrl.h"
int kirk_init() {return 0;}
int sceDrmBBMacInit(MAC_KEY *,int) {return 0;}
int sceDrmBBMacUpdate(MAC_KEY *,u8 *,int) {return 0;}
int bbmac_getkey(MAC_KEY *,u8 *,u8 *key) {memset(key,0,16);return 0;}
int sceDrmBBCipherInit(CIPHER_KEY *,int,int,u8 *,u8 *,u32) {return 0;}
int sceDrmBBCipherUpdate(CIPHER_KEY *,u8 *,int) {return 0;}
int sceDrmBBCipherFinal(CIPHER_KEY *) {return 0;}
}
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char*,...) {}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return false;}
class Image : public FileLoader {
public:
  std::vector<u8> data;bool shortData=false,shortTable=false;unsigned payloadReads=0;
  void put(size_t at,u32 n) {for(unsigned i=0;i<4;++i)data[at+i]=n>>(8*i);}
  Image():data(0x2300) {
    put(0x24,0x100);memcpy(data.data()+0x100,"NPUMDIMG",8);
    put(0x10c,2);put(0x154,0);put(0x164,3);put(0x16c,0x100);
    for(unsigned i=0;i<2;++i) {put(0x210+i*32,0x200+i*4096);put(0x214+i*32,4096);put(0x218+i*32,4);}
    for(unsigned i=0;i<4;++i)memset(data.data()+0x300+i*2048,0x31+i,2048);
  }
  bool Exists() override {return true;}bool IsDirectory() override {return false;}
  s64 FileSize() override {return data.size();}std::string Path() const override {return "fixture.pbp";}
  size_t ReadAt(s64 at,size_t bytes,size_t count,void *out,Flags=Flags::NONE) override {
    if(at<0 || size_t(at)>=data.size() || !bytes || count>size_t(-1)/bytes)return 0;
    size_t length=std::min(bytes*count,data.size()-size_t(at));
    if(at>=0x300) {++payloadReads;assert(length<=4096);if(shortData)length=std::min(length,size_t(2048));}
    if(at==0x200 && shortTable && length)--length;
    memcpy(out,data.data()+at,length);return length/bytes;
  }
};
int main() {
  u8 output[2048];
  {Image image;NPDRMDemoBlockDevice device(&image);assert(device.GetNumBlocks()==4);
    for(int i=0;i<4;++i) {memset(output,0,sizeof(output));assert(device.ReadBlock(i,output));for(auto n:output)assert(n==0x31+i);}
    assert(image.payloadReads==2);assert(!device.ReadBlock(-1,output) && !device.ReadBlock(4,output) && !device.ReadBlock(0,nullptr));}
  for(unsigned kind=0;kind<5;++kind) {Image image;
    if(kind==0)image.put(0x10c,0);if(kind==1)image.put(0x10c,513);if(kind==2)image.put(0x164,UINT32_MAX);
    if(kind==3)image.data.resize(0x180);if(kind==4)image.shortTable=true;
    NPDRMDemoBlockDevice device(&image);assert(device.GetNumBlocks()==0 && !device.ReadBlock(0,output));}
  for(unsigned kind=0;kind<4;++kind) {Image image;
    if(kind==0)image.put(0x214,UINT32_MAX);if(kind==1)image.put(0x214,4097);
    if(kind==2)image.put(0x210,UINT32_MAX);if(kind==3) {image.put(0x214,17);image.put(0x218,0);}
    NPDRMDemoBlockDevice device(&image);assert(!device.ReadBlock(0,output) && image.payloadReads==0);}
  {Image image;NPDRMDemoBlockDevice device(&image);image.shortData=true;
    assert(!device.ReadBlock(0,output) && !device.ReadBlock(1,output));assert(image.payloadReads==2);
    image.shortData=false;assert(device.ReadBlock(0,output) && output[0]==0x31);}
  {Image image;image.put(0x23c,1);NPDRMDemoBlockDevice device(&image);memset(output,0xa5,sizeof(output));
    assert(device.ReadBlock(3,output));for(auto n:output)assert(n==0);}
  puts("pass: actual NPDRM reader first-read/cache state, invalid headers, signed/oversized entries, offsets, short reads and sparse tails; crypto is mocked");
}
