// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/CachedInterpreter.h"
#include "Wii/ScalarInterpreter.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
  uint32_t seed=1;auto random=[&](){seed=seed*1664525+1013904223;return seed;};
  unsigned folded=0,rejected=0;
  for(unsigned i=0;i<200000;++i) {
    uint32_t r[32],expected[32];for(auto &v:r)v=random();r[0]=0;
    memcpy(expected,r,sizeof(r));uint32_t pc=i<2?0xfffffff8u+i*4:random()&~3u,refPC=pc;
    const unsigned reg=i%32;
    const uint32_t first=0x3c000000|reg<<16|(random()&65535);
    const uint32_t second=(i&1?9u:13u)<<26|reg<<21|reg<<16|(random()&65535);
    const bool ok=Wii::FoldConstantPair(first,second,r,pc);
    if(reg) {
      assert(ok);assert(Wii::InterpretScalar(first,expected,refPC));
      assert(Wii::InterpretScalar(second,expected,refPC));
      assert(!memcmp(r,expected,sizeof(r))&&pc==refPC);++folded;
    } else {assert(!ok&&pc==refPC&&!memcmp(r,expected,sizeof(r)));++rejected;}
    // Any rejected pattern leaves the state untouched.
    memcpy(expected,r,sizeof(r));refPC=pc;
    assert(!Wii::FoldConstantPair(first^0x00200000,second,r,pc));
    assert(pc==refPC&&!memcmp(r,expected,sizeof(r)));
    assert(!Wii::FoldConstantPair(first,second^0x04000000,r,pc));
    assert(pc==refPC&&!memcmp(r,expected,sizeof(r)));
  }
  printf("pass: %u constant pairs, %u zero-register fallbacks, wraparound and rejection side effects\n",folded,rejected);
}
