// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
// One static superinstruction for the common two-instruction constant load.
// No memory access, branch, exception or HI/LO effects. The caller must keep
// both guest instruction boundaries observable to timing/debugging.
inline bool FoldConstantPair(uint32_t first,uint32_t second,uint32_t *r,uint32_t &pc) {
  if((first&0xffe00000u)!=0x3c000000u)return false;
  const unsigned reg=(first>>16)&31,kind=second>>26;
  if(!reg||((second>>21)&31)!=reg||((second>>16)&31)!=reg)return false;
  const uint32_t high=first<<16;
  if(kind==13)r[reg]=high|(second&65535);
  else if(kind==9)r[reg]=high+uint32_t(int32_t(int16_t(second)));
  else return false;
  pc+=8;return true;
}
}
