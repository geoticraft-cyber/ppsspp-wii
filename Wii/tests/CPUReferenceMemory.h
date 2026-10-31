// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/MemMap.h"
#include "Wii/AddressSpace.h"
namespace Wii {
// Keep uncommon unaligned word merging out of the ordinary load/store loop.
// This is shared code, not a decoded guest instruction cache.
__attribute__((noinline)) inline bool InterpretMemoryMerge(uint32_t op,uint32_t *r,uint32_t &pc,uint8_t *p) {
  const unsigned kind=op>>26,rt=(op>>16)&31;
  const uint32_t address=r[(op>>21)&31]+uint32_t(int32_t(int16_t(op)));
  uint32_t value;
  const unsigned shift=(address&3)*8;
  const uint32_t mem=*reinterpret_cast<const u32_le *>(p);
  if(kind==34)value=(r[rt]&(0x00ffffffu>>shift))|(mem<<(24-shift));
  else if(kind==38)value=(r[rt]&(0xffffff00u<<(24-shift)))|(mem>>shift);
  else {
    value=kind==42?(r[rt]>>(24-shift))|(mem&(0xffffff00u<<shift)):
      (r[rt]<<shift)|(mem&(0x00ffffffu>>(24-shift)));
    *reinterpret_cast<u32_le *>(p)=value;pc+=4;return true;
  }
  if(rt)r[rt]=value;pc+=4;return true;
}

// Ordinary mapped RAM, VRAM mirrors and scratchpad. I/O, synchronization,
// coprocessors and invalid/alignment cases retain their existing handlers.
inline bool InterpretMemory(uint32_t op,uint32_t *r,uint32_t &pc) {
  const unsigned kind=op>>26;
  static constexpr uint8_t widths[15]={1,2,4,4,1,2,4,0,1,2,4,4,0,0,4};
  if(kind<32||kind>46)return false;
  const unsigned bytes=widths[kind-32];
  if(!bytes)return false;
  const uint32_t address=r[(op>>21)&31]+uint32_t(int32_t(int16_t(op)));
  const bool merge=kind==34||kind==38||kind==42||kind==46;
  if(!merge&&(address&(bytes-1)))return false;
  uint8_t *p=addressSpace.Contiguous(merge?address&~3u:address,bytes);
  if(!p)return false;
  unsigned rt=(op>>16)&31;uint32_t value;
  if(merge)return InterpretMemoryMerge(op,r,pc,p);
  switch(kind) {
    case 32:value=uint32_t(int32_t(int8_t(*p)));break;
    case 33:value=uint32_t(int32_t(int16_t(*reinterpret_cast<const u16_le *>(p))));break;
    case 35:value=*reinterpret_cast<const u32_le *>(p);break;
    case 36:value=*p;break;
    case 37:value=*reinterpret_cast<const u16_le *>(p);break;
    case 40:*p=uint8_t(r[rt]);pc+=4;return true;
    case 41:*reinterpret_cast<u16_le *>(p)=uint16_t(r[rt]);pc+=4;return true;
    case 43:*reinterpret_cast<u32_le *>(p)=r[rt];pc+=4;return true;
    default:return false;
  }
  if(rt)r[rt]=value;pc+=4;return true;
}
}
