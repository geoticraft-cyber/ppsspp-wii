// SPDX-License-Identifier: GPL-2.0-or-later
// Freestanding Linux/PPC test runner, executed by qemu-ppc (not a Wii binary).
#include "Core/MemMap.h"
#include "Wii/Pixel.h"
#include "Wii/InstructionFetch.h"
#include "Wii/GXLayout.h"
#include "Wii/ScalarInterpreter.h"
#include "Wii/MemoryInterpreter.h"
#include "Wii/BufferFill.h"
#include "Wii/GuestCopy.h"
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error These tests must execute on a big-endian PowerPC target.
#endif
extern "C" void *memset(void *p,int c,size_t n) {u8 *b=(u8 *)p;for(size_t i=0;i<n;++i)b[i]=c;return p;}
extern "C" void *memcpy(void *p,const void *s,size_t n) {u8 *d=(u8 *)p;const u8 *b=(const u8 *)s;for(size_t i=0;i<n;++i)d[i]=b[i];return p;}
extern "C" void *memmove(void *p,const void *s,size_t n) {u8 *d=(u8 *)p;const u8 *b=(const u8 *)s;if(d<b)for(size_t i=0;i<n;++i)d[i]=b[i];else while(n){--n;d[n]=b[n];}return p;}
namespace Wii { AddressSpace addressSpace; }
namespace Memory { u8 *base;u32 g_MemorySize=8192;u32 Read_U32(u32 a) { return ReadUnchecked_U32(a); } }
alignas(16) static u8 ram[8192], vram[2 * 1024 * 1024], scratch[16 * 1024];
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int Run() {
  Wii::addressSpace.ram = ram;
  Wii::addressSpace.ramSize = sizeof(ram);
  Wii::addressSpace.vram = vram;
  Wii::addressSpace.scratch = scratch;
  Memory::WriteUnchecked_U32(0x12345678, 0x08000000);
  CHECK(ram[0] == 0x78 && ram[1] == 0x56 && ram[2] == 0x34 && ram[3] == 0x12);
  CHECK(Memory::ReadUnchecked_U32(0x48000000) == 0x12345678);
  Memory::WriteUnchecked_U16(0x1357, 0x08000004);
  CHECK(ram[4] == 0x57 && ram[5] == 0x13);
  Memory::WriteUnchecked_Float(1.0f, 0x08000008);
  CHECK(ram[8] == 0 && ram[9] == 0 && ram[10] == 0x80 && ram[11] == 0x3f);
  CHECK(Memory::ReadUnchecked_Float(0x88000008) == 1.0f);
  PSPPointer<u32_le> ptr = PSPPointer<u32_le>::Create(0x08000000);
  CHECK(*ptr == 0x12345678);
  ptr[1] = 0xabcdef01;
  CHECK(ram[4] == 1 && ram[7] == 0xab);
  auto edge = PSPPointer<u8>::Create(0x041fffff);
  edge[1] = 0x42;
  CHECK(vram[0] == 0x42);
  Wii::InstructionFetch fetch;
  CHECK(fetch.Read(0x08000000)==0x12345678);
  Memory::WriteUnchecked_U32(0x24080001,0x08000000);
  CHECK(fetch.Read(0x08000000)==0x24080001);
  CHECK(fetch.Read(0x48000000)==0x24080001);
  Memory::WriteUnchecked_U32(0x24080002,0x08001000);
  CHECK(fetch.Read(0x08001000)==0x24080002);
  u32 registers[32]{};u32 pc=0x08804000;
  registers[8]=0xffffffff;
  CHECK(Wii::InterpretScalar(0x25080001,registers,pc)&&registers[8]==0&&pc==0x08804004);
  registers[8]=0x80000000;
  CHECK(Wii::InterpretScalar(0x00084843,registers,pc)&&registers[9]==0xc0000000);
  CHECK(Wii::InterpretScalar(0x2d09ffff,registers,pc)&&registers[9]==1);
  CHECK(Wii::InterpretScalar(0x25000001,registers,pc)&&registers[0]==0);
  CHECK(!Wii::InterpretScalar(0x00284802,registers,pc));
  registers[1]=0x88000000;registers[2]=0x123480fe;
  CHECK(Wii::InterpretMemory(0xac220000,registers,pc));
  CHECK(ram[0]==0xfe&&ram[1]==0x80&&ram[2]==0x34&&ram[3]==0x12);
  CHECK(Wii::InterpretMemory(0x80230000,registers,pc)&&registers[3]==0xfffffffe);
  CHECK(Wii::InterpretMemory(0x84230000,registers,pc)&&registers[3]==0xffff80fe);
  CHECK(Wii::InterpretMemory(0x8c230000,registers,pc)&&registers[3]==0x123480fe);
  registers[1]=0x88000001;CHECK(!Wii::InterpretMemory(0x8c230000,registers,pc));
  u8 tile[64]{};
  Wii::PackGXPixel(tile,4,3,3,0x123456);
  CHECK(tile[30]==255&&tile[31]==0x12&&tile[62]==0x34&&tile[63]==0x56);
  CHECK(Wii::UnpackGXPixel(tile,4,3,3)==0x123456);
  CHECK(Wii::RGBToPSPPixel(0xff0000,255,3)==0xff0000ff);
  const u8 rgba[] = {0xff, 0, 0, 0xff};
  const u8 rgb565[] = {0x1f, 0};
  CHECK(Wii::DecodePixel(rgba, 3) == 0xff0000);
  CHECK(Wii::DecodePixel(rgb565, 0) == 0xff0000);
  CHECK(Wii::ToYUYV(0, 0) == 0x10801080);
  CHECK(Wii::ToYUYV(0xffffff, 0xffffff) == 0xeb80eb80);
  auto wrapped=Wii::BufferAccess(vram+0x1ffffc,1,0,512,4);
  CHECK(wrapped==vram);*reinterpret_cast<u32_le *>(wrapped)=0x87654321;
  CHECK(vram[0]==0x21&&vram[3]==0x87);
  Wii::FillBuffer(vram+0x1ffffc,0,0,512,4,16,0x5a);
  CHECK(vram[0x1ffffc]==0x5a&&vram[0]==0x5a&&vram[59]==0x5a);
  CHECK(Wii::CopyGuest(0x041ffffc,0x08000000,16));
  u8 copy[16];CHECK(Wii::CopyFromGuest(copy,0x041ffffc,16));
  for(unsigned i=0;i<16;++i)CHECK(copy[i]==ram[i]);
  return 0;
}
extern "C" void _start() {
  const int result = Run();
  register long syscall asm("r0") = 1;
  register long status asm("r3") = result;
  asm volatile("sc" : : "r"(syscall), "r"(status) : "memory");
  __builtin_unreachable();
}
