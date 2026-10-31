// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/MIPS/MIPSTables.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <chrono>
extern MIPSInterpretFunc MIPSTestCachedHandler(MIPSOpcode op);
static void CoprocessorCacheChecks() {
  uint32_t seed=0x912843;
  const auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned kind:{17u,52u,60u})for(unsigned i=0;i<400000;++i) {
    MIPSOpcode word(kind<<26|(random()&0x03ffffff));
    assert(MIPSTestCachedHandler(word)==MIPSGetInterpretFunc(word));
  }
  for(unsigned format=0;format<32;++format)for(unsigned subop=0;subop<64;++subop)for(unsigned rt=0;rt<32;++rt) {
    const MIPSOpcode op(17u<<26|format<<21|rt<<16|(random()&0xffc0)|subop);
    assert(MIPSTestCachedHandler(op)==MIPSGetInterpretFunc(op));
  }
  // Exhaust the vector selectors, including matrix bit 20 (ignored by the
  // original decoder), and vary every non-selector bit independently.
  for(unsigned kind:{52u,60u})for(unsigned selector=0;selector<1024;++selector)
    for(unsigned operand=0;operand<18;++operand) {
      const u32 low=operand<16?1u<<operand:operand==16?0u:65535u;
      const MIPSOpcode op(kind<<26|selector<<16|low);
      assert(MIPSTestCachedHandler(op)==MIPSGetInterpretFunc(op));
    }
  puts("pass: 36864 exhaustive VFPU selector/non-selector-bit comparisons");
  puts("pass: 1200000 random plus 65536 exhaustive COP1 selector/reference comparisons, including invalid encodings");
  uint32_t regs[32],floats[32],expected[32],expectedFloats[32];
  for(unsigned i=0;i<100000;++i) {
    for(auto &v:regs)v=random();regs[0]=0;for(auto &v:floats)v=random();
    const uint32_t op=(i&1?0x44800000:0x44000000)|((random()%32)<<16)|((random()%32)<<11);
    const uint32_t pc=random()&~3u;
    memcpy(currentMIPS->r,regs,sizeof(regs));memcpy(currentMIPS->fi,floats,sizeof(floats));currentMIPS->pc=pc;
    auto handler=MIPSGetInterpretFunc(MIPSOpcode(op));assert(handler);handler(MIPSOpcode(op));
    memcpy(expected,currentMIPS->r,sizeof(regs));memcpy(expectedFloats,currentMIPS->fi,sizeof(floats));
    const uint32_t expectedPC=currentMIPS->pc;
    memcpy(currentMIPS->r,regs,sizeof(regs));memcpy(currentMIPS->fi,floats,sizeof(floats));currentMIPS->pc=pc;
    MIPSInterpret(MIPSOpcode(op));
    assert(!memcmp(expected,currentMIPS->r,sizeof(regs))&&!memcmp(expectedFloats,currentMIPS->fi,sizeof(floats))&&currentMIPS->pc==expectedPC);
  }
  const auto run=[&](bool cached) {
    volatile MIPSInterpretFunc result=nullptr;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<4000000;++i) {
      const MIPSOpcode op(0xd0000000u|((i&31)<<16)|(i&65535));
      result=cached?MIPSTestCachedHandler(op):MIPSGetInterpretFunc(op);
    }
    (void)result;
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  };
  const double previous=run(false),current=run(true);
  printf("pass: 100000 COP1 transfers retain original handler behavior\n");
  printf("host VFPU decode only: previous %.3f ms cached %.3f ms (4000000 lookups; not complete execution)\n",previous,current);
  for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn) {
    const bool cached=(turn^(round&1))!=0;
    volatile MIPSInterpretFunc selected=nullptr;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<4000000;++i) {
      // Single arithmetic, compares, word conversion, moves and branches.
      const uint32_t formats[]={16,16,16,20,0,4,8};
      const MIPSOpcode op(17u<<26|formats[i%7]<<21|((i&31)<<16)|(i&63));
      selected=cached?MIPSTestCachedHandler(op):MIPSGetInterpretFunc(op);
    }
    (void)selected;
    printf("host cop1 decode round %u %s %.3f ms\n",round,cached?"new":"old",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }

}
