// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/GuestCopy.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include <vector>
#include <cassert>
#include <cstdio>
static void GETransferChecks() {
  const u32 base=0x08b00000;const unsigned region=0x20000;
  auto *memory=Wii::addressSpace.Contiguous(base,region);assert(memory);
  const auto command=[](unsigned cmd,u32 data) {
    const u32 op=(cmd<<24)|(data&0xffffff),old=gstate.cmdmem[cmd];
    gstate.cmdmem[cmd]=op;gpu->ExecuteOp(op,old^op);
  };
  unsigned cases=0;
  for(unsigned bpp:{2u,4u})for(unsigned width:{32u,64u})
    for(unsigned stride:{64u,80u})for(unsigned offset:{16u,0x10000u}) {
      for(unsigned i=0;i<region;++i)memory[i]=u8(i*13+(i>>8));
      std::vector<u8> original(memory,memory+region);
      const u32 src=base,dst=base+offset;const unsigned height=32;
      for(unsigned row=0;row<height;++row)
        assert(Wii::CopyGuest(dst+row*stride*bpp,src+row*stride*bpp,width*bpp));
      const std::vector<u8> expected(memory,memory+region);
      memcpy(memory,original.data(),region);
      command(GE_CMD_TRANSFERSRC,src&0xffffff);
      command(GE_CMD_TRANSFERSRCW,((src>>24)<<16)|stride);
      command(GE_CMD_TRANSFERDST,dst&0xffffff);
      command(GE_CMD_TRANSFERDSTW,((dst>>24)<<16)|stride);
      command(GE_CMD_TRANSFERSRCPOS,0);command(GE_CMD_TRANSFERDSTPOS,0);
      command(GE_CMD_TRANSFERSIZE,(width-1)|((height-1)<<10));
      command(GE_CMD_TRANSFERSTART,bpp==4?1:0);
      assert(!memcmp(memory,expected.data(),region));++cases;
    }
  printf("PASS: %u actual GE TRANSFERSTART comparisons: packed/padded, 16/32 bit and sequential overlap\n",cases);
}
