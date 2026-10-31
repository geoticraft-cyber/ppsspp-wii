extern "C" int __lsan_is_turned_off() {return 1;}
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/Util/BlockAllocator.h"
#include "Wii/AllocationDiagnostics.h"
#include "Common/Log.h"
#include <cassert>
#include <cstdio>
static unsigned reports, lastFree, lastLargest;static size_t lastRequest;
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char*,...) {}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return false;}
namespace Reporting {void ReportMessage(const char *,...) {} bool ShouldLogOnce(const char *) {return false;}}
namespace Wii {
void AllocationFailure(const char *,size_t request,size_t,size_t,uintptr_t,int,uintptr_t,unsigned free,unsigned largest) {
  ++reports;lastRequest=request;lastFree=free;lastLargest=largest;
}
}
int main() {
  BlockAllocator blocks;const unsigned mib=1024*1024;
  blocks.Init(0x08800000,8*mib);
  u32 addresses[4];
  for(unsigned i=0;i<4;++i) {u32 size=2*mib;addresses[i]=blocks.Alloc(size);assert(addresses[i]!=u32(-1));}
  assert(blocks.Free(addresses[0]) && blocks.Free(addresses[2]));
  assert(blocks.GetTotalFreeBytes()==4*mib && blocks.GetLargestFreeBlockSize()==2*mib);
  u32 size=3*mib;assert(blocks.Alloc(size)==u32(-1));
  assert(reports==1 && lastRequest==3*mib && lastFree==4*mib && lastLargest==2*mib);
  assert(blocks.AllocAt(addresses[1],32)==u32(-1));assert(reports==2 && lastRequest==32);
  size=9*mib;assert(blocks.Alloc(size)==u32(-1));assert(reports==3 && lastRequest==9*mib);
  size=0;assert(blocks.Alloc(size)==u32(-1));assert(reports==4 && lastRequest==0);
  assert(blocks.AllocAt(0x08000000,32)==u32(-1));assert(reports==5 && lastRequest==32);
  assert(blocks.AllocAt(addresses[0]+2*mib-16,32)==u32(-1));assert(reports==6 && lastRequest==32);
  assert(blocks.AllocAt(addresses[0],9*mib)==u32(-1));assert(reports==7 && lastRequest==9*mib);
  assert(blocks.GetTotalFreeBytes()==4*mib && blocks.GetLargestFreeBlockSize()==2*mib);
  assert(blocks.Free(addresses[1]) && blocks.Free(addresses[3]));
  size=8*mib;assert(blocks.Alloc(size)!=u32(-1));assert(reports==7);
  blocks.Shutdown();
  blocks.Init(0x1000,256);
  assert(blocks.AllocAt(0x100f,2)==0x100f);
  assert(blocks.GetBlockStartFromAddress(0x100f)==0x1000);
  assert(blocks.GetBlockSizeFromAddress(0x100f)==32 && blocks.GetTotalFreeBytes()==224);
  assert(blocks.AllocAt(0x1010,1)==u32(-1)); // The requested tail is reserved too.
  assert(blocks.Free(0x100f) && blocks.GetTotalFreeBytes()==256);
  assert(blocks.AllocAt(0x1001,16)==0x1001 && blocks.GetBlockSizeFromAddress(0x1001)==32);
  assert(blocks.Free(0x1001));
  size=17;assert(blocks.AllocAligned(size,0x80000000,16)==u32(-1));
  assert(size==17 && blocks.GetTotalFreeBytes()==256);
  size=17;assert(blocks.AllocAligned(size,48,16)==u32(-1));
  assert(blocks.AllocAt(0x1000,0)==u32(-1));
  blocks.Init(0x1000,8192);
  size=8192;assert(blocks.AllocAligned(size,16,UINT32_MAX)==u32(-1));
  assert(blocks.GetTotalFreeBytes()==8192 && blocks.GetLargestFreeBlockSize()==8192);
  size=16;assert(blocks.AllocAligned(size,16,UINT32_MAX,true)==u32(-1));
  blocks.Init(0xfffffff0,256);assert(blocks.GetTotalFreeBytes()==0);
  size=16;assert(blocks.Alloc(size)==u32(-1));
  BlockAllocator empty;size=16;assert(empty.Alloc(size)==u32(-1));
  puts("pass: actual guest allocator reports 3 MiB failure with 4 MiB free / 2 MiB largest, fixed-address failures and invalid sizes; coalescing still works");
  puts("pass: unaligned fixed ranges reserve the full tail; zero sizes, oversized rounding, alignment overflow and invalid/uninitialized regions cannot corrupt allocator blocks");
}
