// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMapHelpers.h"
#include "Core/CoreTiming.h"
#include <vector>
#include <cassert>
#include <cstdio>

// Runs actual guest syscall instructions and the real emulated scheduler.
static void IoChecks() {
  const u32 code=0x089c0000, name=0x089d0000, buf=0x089d1000;
  const u32 result=0x089d2000, records=0x089d3000, marker=0x089d4000;
  const char path[]="ms0:/io-regression.bin";
  Memory::Memcpy(name,path,sizeof(path));
  Memory::Memset(buf,0xcd,4096); Memory::Memset(result,0x5a,128);
  Memory::Memcpy(buf+32,"wxyz",4);
  Memory::Write_U64(1,result+80);Memory::Write_U32(0,result+88);Memory::Write_U32(0,result+92);
  Memory::Write_U32(4,result+64);
  const char invalidPath[]="ms0:/pgd-invalid.bin";
  Memory::Memcpy(name+64,invalidPath,sizeof(invalidPath));
  {u8 header[0x90]={0,0x50,0x47,0x44};FILE *f=fopen("test-memstick/pgd-invalid.bin","wb");assert(f);assert(fwrite(header,1,sizeof(header),f)==sizeof(header));fclose(f);}
  std::vector<u32> words;
  auto load=[&](int reg,u32 value) {
    words.push_back(0x3c000000|(reg<<16)|(value>>16));
    words.push_back(0x34000000|(reg<<21)|(reg<<16)|(value&0xffff));
  };
  unsigned slot=0;
  auto record=[&]() {load(8,records+4*slot++); words.push_back(0xad020000);};
  auto call=[&](u32 nid) {words.push_back(GetSyscallOp("IoFileMgrForUser",nid));record();};
  auto fd=[&]() {words.push_back(0x02002021);}; // addu a0,s0,zero
  load(4,0xffffffff);load(5,7);call(0xb293727f); // illegal priority
  load(4,0xffffffff);load(5,0x78);call(0xb293727f);
  load(4,0xffffffff);load(5,0x38);call(0xb293727f); // default for new fds
  load(4,63);load(5,0x38);call(0xb293727f); // unused fd
  load(4,name);load(5,1);load(6,0);call(0x89aa9906); // async open
  words.push_back(0x00408021); // save fd in s0
  fd();load(5,result);call(0xe23eec33); // wait open
  fd();load(5,0x77);call(0xb293727f); // lower priority than caller
  fd();load(5,buf);load(6,8);call(0xa0b5a7c2); // async read
  load(8,buf);words.push_back(0x8d020000);record(); // still untouched
  fd();call(0xff5940b6); // close must not overwrite pending read
  fd();load(5,0x09fffffc);call(0x3251ea56); // invalid result span
  fd();load(5,result);call(0x35dbd746); // actual callback-enabled wait
  fd();load(5,0);load(6,0);call(0x1b385d8f); // seek32 async
  fd();load(5,result+8);call(0xe23eec33);
  fd();load(5,buf+16);load(6,8);call(0xa0b5a7c2); // reuse same worker
  fd();load(5,0);call(0xe23eec33); // null result allowed
  fd();load(5,0x09fffffc);load(6,8);call(0xa0b5a7c2); // rejected in worker
  fd();load(5,result+8);call(0xe23eec33);
  fd();call(0xff5940b6); // async close
  fd();load(5,0);call(0xe23eec33);
  fd();load(5,0x38);call(0xb293727f); // closed fd
  load(4,1);load(5,buf);load(6,0xffffffff);call(0x42ec03ac); // negative stdout
  load(4,name);load(5,3);load(6,0);call(0x89aa9906);
  words.push_back(0x00408021);
  fd();load(5,0);call(0xe23eec33);
  fd();load(5,0x1a);call(0xb293727f);
  fd();load(5,buf+32);load(6,4);call(0x0facab19);
  fd();load(5,result+32);call(0xe23eec33);
  fd();load(5,0x01010005);load(6,result+80);load(7,4);load(8,0);load(9,0);call(0xe95a012b);
  fd();load(5,result+40);call(0xe23eec33);
  fd();load(5,0x01030008);load(6,result+64);load(7,4);load(8,buf+64);load(9,4);call(0xe95a012b);
  fd();load(5,result+48);call(0xe23eec33);
  fd();call(0xff5940b6);
  fd();load(5,0);call(0xe23eec33);
  const unsigned legacySlot=slot;
  load(4,name);load(5,1);load(6,0);call(0x109f50bc);words.push_back(0x00408021);
  fd();load(5,0x04100001);load(6,0);load(7,0);load(8,0);load(9,0);call(0x63632449);
  fd();load(5,buf+128);load(6,4);call(0x6a638d83);fd();call(0x810c4bc3);
  load(4,name+64);load(5,1);load(6,0);call(0x109f50bc);words.push_back(0x00408021);
  fd();load(5,0x04100001);load(6,0);load(7,0);load(8,0);load(9,0);call(0x63632449);fd();call(0x810c4bc3);
  load(8,marker);load(9,0xfeed10aa);words.push_back(0xad090000);
  const u32 stop=code+4*words.size();words.push_back(0x08000000|(stop>>2));words.push_back(0);
  for(size_t i=0;i<words.size();++i)Memory::Write_U32(words[i],code+4*i);
  currentMIPS->InvalidateICache(code,words.size()*4);
  currentMIPS->pc=code;currentMIPS->inDelaySlot=false;
  for(int n=0;n<10000&&Memory::Read_U32(marker)!=0xfeed10aa;++n) {
    coreState=CORE_RUNNING;PSP_BeginHostFrame();PSP_RunLoopFor(10000);PSP_EndHostFrame();
  }
  assert(Memory::Read_U32(marker)==0xfeed10aa);
  auto got=[&](unsigned i){return Memory::Read_U32(records+4*i);};
  for(unsigned i=0;i<slot;++i)fprintf(stderr,"io-record %u = %08x\n",i,got(i));
  assert(got(0)==SCE_KERNEL_ERROR_ILLEGAL_PRIORITY&&got(1)==SCE_KERNEL_ERROR_ILLEGAL_PRIORITY);
  assert(got(2)==0&&got(3)==SCE_KERNEL_ERROR_BADF);
  assert(got(4)>=4&&got(4)<64&&got(5)==0&&got(6)==0&&got(7)==0);
  assert(got(8)==0xcdcdcdcd&&got(9)==SCE_KERNEL_ERROR_ASYNC_BUSY);
  assert(got(10)==SCE_KERNEL_ERROR_ILLEGAL_ADDR);
  for(unsigned i=11;i<=19;++i)assert(got(i)==0);
  assert(got(20)==SCE_KERNEL_ERROR_BADF&&got(21)==SCE_KERNEL_ERROR_ILLEGAL_ADDR);
  assert(got(22)>=4&&got(22)<64);
  for(unsigned i=23;i<legacySlot;++i)assert(got(i)==0);
  assert(got(legacySlot)>=4&&got(legacySlot)<64&&got(legacySlot+1)==0);
  assert(got(legacySlot+2)==4&&got(legacySlot+3)==0);
  assert(got(legacySlot+4)>=4&&got(legacySlot+4)<64&&got(legacySlot+5)==0x80510204&&got(legacySlot+6)==0);
  assert(!memcmp(Memory::GetPointer(buf+128),"wxyz",4));
  puts("PASS: real PGD ioctl preserves ordinary short-file success, reset position, and genuine malformed-PGD failure");
  assert(Memory::Read_U64(result+32)==4&&Memory::Read_U64(result+40)==0&&Memory::Read_U64(result+48)==4);
  assert(!memcmp(Memory::GetPointer(buf+64),"xyze",4));
  assert(Memory::Read_U64(result)==8);
  // failed read returns a sign-extended 64-bit PSP error, in little endian bytes.
  const u8 *raw=Memory::GetPointer(result+8);
  assert(raw[0]==(SCE_KERNEL_ERROR_ILLEGAL_ADDR&255)&&raw[4]==255&&raw[7]==255);
  assert(!memcmp(Memory::GetPointer(buf),"abcdefgh",8));
  assert(!memcmp(Memory::GetPointer(buf+16),"abcdefgh",8));
  puts("PASS: actual guest async open/read/write/seek/ioctl/close, worker reuse, priorities, busy state, CB waits, null/boundary results, signed little endian errors");
}
