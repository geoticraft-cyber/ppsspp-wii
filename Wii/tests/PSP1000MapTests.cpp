#include "Wii/AddressSpace.h"
#include "Wii/GuestGeometry.h"
namespace Wii {AddressSpace addressSpace;}
#include <vector>
#include <cassert>
#include <cstdio>
int main() {
  std::vector<uint8_t> ram(0x2000000),vram(0x200000),scratch(0x4000);
  Wii::AddressSpace m;m.ram=ram.data();m.vram=vram.data();m.scratch=scratch.data();m.ramSize=ram.size();m.RebuildPages();
  for(uint32_t alias:{0u,0x40000000u,0x80000000u,0xc0000000u}) {
    for(uint32_t offset:{0u,0x7fffffu,0x800000u,0x1ffffffu})assert(m.Translate(alias|0x08000000|offset)==ram.data()+offset);
    assert(m.IsRangeValid(alias|0x09fffffc,4));assert(!m.IsRangeValid(alias|0x09fffffc,5));assert(!m.Translate(alias|0x0a000000));
    assert(m.Translate(alias|0x10000)==scratch.data());assert(m.Translate(alias|0x13fff)==scratch.data()+0x3fff);
    assert(!m.Translate(alias|0xffff)&&!m.Translate(alias|0x14000));
    for(uint32_t mirror=0;mirror<4;++mirror) {
      uint32_t address=alias|0x04000000|(mirror*0x200000);
      assert(m.Translate(address)==vram.data());assert(m.Translate(address+0x1fffff)==vram.data()+0x1fffff);
      assert(m.Span(address+0x1ffffc)==4);
    }
    assert(!m.Translate(alias|0x04800000));assert(!m.IsRangeValid(alias|0x047ffffc,5));
  }
  // Oversized backing metadata cannot expose PSP-2000 RAM on the fat model.
  m.ramSize=0x4000000;m.RebuildPages();assert(!m.Translate(0x0a000000));assert(m.ValidSpan(0x08000000)==0x2000000);
  assert(!m.IsRangeValid(0xc9fffffc,0xffffffffu));
  m.ramSize=0x180001;m.RebuildPages();assert(m.Translate(0x08180000)==ram.data()+0x180000);assert(!m.Translate(0x08180001));
  Wii::addressSpace=m;Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.RebuildPages();
  assert(Wii::ValidGeometry(0x09ffffe0,0,1,32,0));assert(!Wii::ValidGeometry(0x09ffffe0,0,2,32,0));
  ram[0]=3;ram[1]=0;ram[2]=0;ram[3]=0;
  assert(Wii::ValidGeometry(0x08000100,0x08000000,1,32,3));
  ram[2]=1;assert(!Wii::ValidGeometry(0x08000100,0x08000000,1,32,3));ram[2]=0;
  assert(!Wii::ValidGeometry(0x08000100,0x09ffffff,2,32,2));
  assert(!Wii::ValidGeometry(0x041ffff0,0,2,16,0)); // raw pointer cannot span a mirror
  puts("pass: psp-1000 RAM/user boundary, four aliases, scratchpad, four VRAM mirrors, holes, end guards, overflow and oversized backing rejection");
}
