// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#include "Wii/AddressSpace.h"
#include "Wii/Pixel.h"
#include "Core/MemMap.h"
#include "Wii/InstructionFetch.h"
#include "Wii/GXLayout.h"

namespace Wii { AddressSpace addressSpace; }
namespace Memory { u32 g_MemorySize=32*1024*1024; u8 *base = nullptr; u32 Read_U32(u32 a) { return ReadUnchecked_U32(a); } }

int main() {
  std::vector<uint8_t> ram(32 * 1024 * 1024), vram(2 * 1024 * 1024), scratch(16 * 1024);
  Wii::addressSpace.ram = ram.data();
  Wii::addressSpace.vram = vram.data();
  Wii::addressSpace.scratch = scratch.data();
  Wii::addressSpace.ramSize = ram.size();
  auto &map = Wii::addressSpace;
  for (uint32_t alias : {0x08000000u, 0x48000000u, 0x88000000u, 0xc8000000u}) {
    assert(map.Translate(alias + 0x1234) == ram.data() + 0x1234);
    assert(map.Translate(alias + ram.size() - 1) == ram.data() + ram.size() - 1);
    assert(map.Translate(alias + ram.size()) == nullptr);
  }
  for (uint32_t alias : {0x04000000u, 0x04200000u, 0x04400000u, 0x04600000u, 0x44000000u})
    assert(map.Translate(alias + 0x100) == vram.data() + 0x100);
  assert(map.Translate(0x04800000) == nullptr);
  assert(map.Translate(0x00010000) == scratch.data());
  assert(map.Translate(0x40013fff) == scratch.data() + scratch.size() - 1);
  assert(map.Translate(0x00014000) == nullptr);
  assert(map.Translate(0) == nullptr);
  assert(map.Translate(0x04000000 + vram.size() - 1) + 1 != map.Translate(0x04200000));

  // Exercise actual PPSSPP accessors, including pointers embedded in guest structures.
  Memory::WriteUnchecked_U32(0x12345678, 0x08800000);
  assert(Memory::ReadUnchecked_U32(0x48800000) == 0x12345678);
  assert(ram[0x800000] == 0x78 && ram[0x800003] == 0x12);
  Memory::WriteUnchecked_Float(1.5f, 0x08800004);
  assert(Memory::ReadUnchecked_Float(0x88800004) == 1.5f);
  PSPPointer<u32_le> pointer = PSPPointer<u32_le>::Create(0x08800000);
  assert(*pointer == 0x12345678);
  pointer[0] = 0xabcdef01;
  assert(Memory::ReadUnchecked_U32(0x08800000) == 0xabcdef01);
  const u32_le *raw = pointer;
  assert(raw == reinterpret_cast<u32_le *>(ram.data() + 0x800000));
  PSPPointer<u8> mirrorEdge = PSPPointer<u8>::Create(0x041fffff);
  mirrorEdge[1] = 0x5a;
  assert(vram[0] == 0x5a);

  Wii::InstructionFetch fetch;
  Memory::WriteUnchecked_U32(0x24080001,0x08800ffc);
  Memory::WriteUnchecked_U32(0x24080002,0x08801000);
  assert(fetch.Read(0x08800ffc)==0x24080001);
  assert(fetch.Read(0x08801000)==0x24080002);
  Memory::WriteUnchecked_U32(0x24080003,0x08801000);
  assert(fetch.Read(0x08801000)==0x24080003);
  assert(fetch.Read(0x48801000)==0x24080003);
  std::vector<uint8_t> tiles(480*272*4);
  for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x)
    Wii::PackGXPixel(tiles.data(),480,x,y,(x&255)<<16|(y&255)<<8|((x^y)&255));
  for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x)
    assert(Wii::UnpackGXPixel(tiles.data(),480,x,y)==((x&255)<<16|(y&255)<<8|((x^y)&255)));
  assert(Wii::RGBToPSPPixel(0xff0000,255,0)==0x1f);
  assert(Wii::RGBToPSPPixel(0xff0000,255,1)==0x801f);
  assert(Wii::RGBToPSPPixel(0xff0000,255,2)==0xf00f);
  assert(Wii::RGBToPSPPixel(0xff0000,255,3)==0xff0000ff);
  const uint8_t red565[] = {0x1f, 0x00}, green565[] = {0xe0, 0x07}, blue565[] = {0x00, 0xf8};
  assert(Wii::DecodePixel(red565, 0) == 0xff0000);
  assert(Wii::DecodePixel(green565, 0) == 0x00ff00);
  assert(Wii::DecodePixel(blue565, 0) == 0x0000ff);
  const uint8_t red5551[] = {0x1f, 0x80}, blue4444[] = {0x00, 0xff}, rgba[] = {0x12, 0x34, 0x56, 0xff};
  assert(Wii::DecodePixel(red5551, 1) == 0xff0000);
  assert(Wii::DecodePixel(blue4444, 2) == 0x0000ff);
  assert(Wii::DecodePixel(rgba, 3) == 0x123456);
  assert(Wii::ToYUYV(0, 0) == 0x10801080);
  assert(Wii::ToYUYV(0xffffff, 0xffffff) == 0xeb80eb80);
  std::puts("PASS: Wii address aliases, PPSSPP accessors, little-endian pixels, YUYV output");
}
