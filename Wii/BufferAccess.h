// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/AddressSpace.h"
#include <cstdint>
namespace Wii {
// Raw render buffers formerly depended on mirrored virtual VRAM mappings.
// Wii allocates only one backing store, so every access must mirror explicitly.
inline uint8_t *BufferAccess(uint8_t *base,int x,int y,int stride,unsigned bytes) {
  if(!base||(reinterpret_cast<uintptr_t>(base)&(bytes-1))||x<0||y<0||stride<0)return nullptr;
  const uint64_t offset=(uint64_t(y)*unsigned(stride)+unsigned(x))*bytes;
  const uintptr_t b=reinterpret_cast<uintptr_t>(base);
  uintptr_t distance=b-reinterpret_cast<uintptr_t>(addressSpace.vram);
  if(addressSpace.vram&&distance<2*1024*1024)
    return addressSpace.vram+((distance+offset)&0x1fffff);
  distance=b-reinterpret_cast<uintptr_t>(addressSpace.ram);
  if(addressSpace.ram&&distance<addressSpace.ramSize&&offset<=addressSpace.ramSize-distance&&bytes<=addressSpace.ramSize-distance-offset)
    return addressSpace.ram+distance+offset;
  distance=b-reinterpret_cast<uintptr_t>(addressSpace.scratch);
  if(addressSpace.scratch&&distance<16384&&offset<=16384-distance&&bytes<=16384-distance-offset)
    return addressSpace.scratch+distance+offset;
  return nullptr;
}
}
