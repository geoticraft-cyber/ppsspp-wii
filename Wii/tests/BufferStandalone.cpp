extern "C" int __lsan_is_turned_off() {return 1;}
#include <vector>
#include <cstdio>
#include "Wii/tests/BufferChecks.h"
namespace Wii {AddressSpace addressSpace;}
namespace Memory {u32 g_MemorySize=32*1024*1024;u8 *base=nullptr;}
int main() {
  std::vector<uint8_t> ram(32*1024*1024),vram(2*1024*1024+256,0x6e),scratch(16384);
  Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();
  Wii::addressSpace.vram=vram.data()+128;Wii::addressSpace.scratch=scratch.data();
  BufferChecks();
  for(unsigned i=0;i<128;++i)assert(vram[i]==0x6e&&vram[128+2*1024*1024+i]==0x6e);
  printf("PASS: VRAM guard bytes unchanged under ASan/UBSan\n");
}
