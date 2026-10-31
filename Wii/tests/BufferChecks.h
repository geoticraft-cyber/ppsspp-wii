#pragma once
#include <cassert>
#include <cstring>
#include "GPU/Software/SoftGpu.h"
#include "Wii/BufferFill.h"
#include "Wii/GuestCopy.h"
static void BufferChecks() {
  auto v=Wii::addressSpace.vram;uint8_t first[128],last[128],ram[128],out[128];
  memcpy(first,v,128);memcpy(last,v+0x200000-128,128);
  auto r=Memory::GetPointerUnchecked(0x089e8000);memcpy(ram,r,128);
  FormatBuffer buffer;buffer.data=v+0x1ffffc;
  buffer.Set32(1,0,512,0x12345678);assert(Memory::ReadUnchecked_U32(0x04000000)==0x12345678);
  assert(buffer.Get32(1,0,512)==0x12345678);
  buffer.data=v+0x1ffffe;buffer.Set16(1,0,512,0xabcd);
  assert(buffer.Get16(1,0,512)==0xabcd&&v[0]==0xcd&&v[1]==0xab);
  Wii::FillBuffer(v+0x1ffffc,0,0,512,4,16,0x5a);
  for(unsigned i=0;i<60;++i)assert(v[i]==0x5a);
  for(unsigned i=0;i<4;++i)assert(v[0x1ffffc+i]==0x5a);
  for(unsigned i=0;i<128;++i)r[i]=uint8_t(i*7);
  assert(Wii::CopyGuest(0x041ffff0,0x089e8000,128));
  assert(Wii::CopyFromGuest(out,0x041ffff0,128));
  for(unsigned i=0;i<128;++i)assert(out[i]==uint8_t(i*7));
  assert(!Wii::BufferAccess(Wii::addressSpace.ram+Wii::addressSpace.ramSize-4,1,0,512,4));
  assert(!Wii::BufferAccess(v,-1,0,512,4));
  memcpy(v,first,128);memcpy(v+0x200000-128,last,128);memcpy(r,ram,128);
  printf("PASS: real framebuffer/depth access, clear spans and GE copies wrap at 2 MiB\n");
}
