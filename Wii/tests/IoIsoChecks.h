// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/FileSystems/ISOFileSystem.h"
#include <algorithm>

class FaultDisc final : public BlockDevice {
public:
  int failBlock=-1;
  u32 GetNumBlocks() override {return 0x20000;}
  bool ReadBlock(int block,u8 *out,bool=false) override {
    if(block==failBlock||block<0||u32(block)>=GetNumBlocks())return false;
    if(block==16) {
      memset(out,0,2048);memcpy(out+1,"CD001",5);
      // ISO 9660 PVD: path table length 2051, L table at sector 0x10020.
      const u8 length[]={3,8,0,0}, location[]={0x20,0,1,0};
      memcpy(out+132,length,4);memcpy(out+140,location,4);
    } else for(unsigned i=0;i<2048;++i)out[i]=u8((u32(block)+i)%251);
    return true;
  }
};
static void IoIsoChecks() {
  SequentialHandleAllocator allocator;
  auto *disc=new FaultDisc();
  ISOFileSystem fs(&allocator,disc);
  u32 file=fs.OpenFile("/sce_lbn0x20_size0x3000",FILEACCESS_READ,"disc0:");assert(file);
  u8 out[8192];memset(out,0xcd,sizeof(out));int usec=0;
  assert(fs.SeekFile(file,1,FILEMOVE_BEGIN)==1);
  disc->failBlock=33;
  assert((s32)fs.ReadFile(file,out,5000,usec)==(s32)0x80010005U);
  assert(fs.SeekFile(file,0,FILEMOVE_CURRENT)==1); // no false progress
  disc->failBlock=-1;
  assert(fs.ReadFile(file,out,5000,usec)==5000);
  for(unsigned i=0;i<5000;++i)assert(out[i]==u8((32+(i+1)/2048+(i+1)%2048)%251));
  assert(out[5000]==0xcd);
  fs.SeekFile(file,0x2ffc,FILEMOVE_BEGIN);
  assert(fs.ReadFile(file,out,2048,usec)==4);
  assert(fs.ReadFile(file,out,8,usec)==0);
  assert(fs.ReadFile(file,nullptr,0,usec)==0);
  const u32 addr=0x089e0000;
  assert(fs.Ioctl(file,0x01020002,0,0,addr,4096,usec)==0);
  for(unsigned i=0;i<2051;++i)assert(Memory::Read_U8(addr+i)==u8((0x10020+i/2048+i%2048)%251));
  assert(fs.Ioctl(file,0x01020002,0,0,0x09fffffc,4096,usec)==SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT);
  disc->failBlock=0x10021;
  assert((s32)fs.Ioctl(file,0x01020002,0,0,addr,4096,usec)==(s32)0x80010005U);
  disc->failBlock=16;
  assert((s32)fs.Ioctl(file,0x01020001,0,0,addr,4096,usec)==(s32)0x80010005U);
  disc->failBlock=-1;
  u32 raw=fs.OpenFile("",FILEACCESS_READ,"umd0:");assert(raw);
  fs.SeekFile(raw,3,FILEMOVE_BEGIN);disc->failBlock=3;
  assert((s32)fs.ReadFile(raw,out,1,usec)==(s32)0x80010005U);
  assert(fs.SeekFile(raw,0,FILEMOVE_CURRENT)==3);
  disc->failBlock=-1;
  fs.SeekFile(raw,disc->GetNumBlocks()-1,FILEMOVE_BEGIN);
  assert(fs.ReadFile(raw,out,2,usec)==1);
  assert(fs.ReadFile(raw,out,1,usec)==0);
  fs.CloseFile(file);fs.CloseFile(raw);
  puts("PASS: injected disc failures, unchanged failed seek position, unaligned byte reads, short EOF, raw sector counts, full 32-bit path table LBA and multi-sector tail");
}
