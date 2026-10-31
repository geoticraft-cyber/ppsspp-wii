// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/BufferAccess.h"
namespace Wii {
// Snapshot address translation, not pixels. Guest writes (including feedback
// and depth/color aliasing) remain visible through each returned pointer.
struct PreparedBuffer {
  uint8_t *base=nullptr,*backing=nullptr;
  size_t distance=0,remaining=0;
  int stride=0;
  unsigned bytes=0;
  bool mirror=false;
  void Prepare(uint8_t *p,int pitch,unsigned bpp) {
    base=p;stride=pitch;bytes=bpp;backing=nullptr;mirror=false;
    if(!p||pitch<0||(bpp!=2&&bpp!=4)||(reinterpret_cast<uintptr_t>(p)&(bpp-1)))return;
    const uintptr_t b=reinterpret_cast<uintptr_t>(p);
    distance=b-reinterpret_cast<uintptr_t>(addressSpace.vram);
    if(addressSpace.vram&&distance<0x200000) {backing=addressSpace.vram;mirror=true;return;}
    distance=b-reinterpret_cast<uintptr_t>(addressSpace.ram);
    if(addressSpace.ram&&distance<addressSpace.ramSize) {backing=p;remaining=addressSpace.ramSize-distance;return;}
    distance=b-reinterpret_cast<uintptr_t>(addressSpace.scratch);
    if(addressSpace.scratch&&distance<16384) {backing=p;remaining=16384-distance;}
  }
  uint8_t *At(int x,int y) const {
    if(!backing||x<0||y<0)return nullptr;
    // DrawingCoords are signed 16-bit. For these pitches, even their largest
    // positive coordinates fit a 32-bit byte offset, avoiding 64-bit multiply
    // on Broadway. Unusual callers retain the exact bounded reference path.
    if(unsigned(x)>32767||unsigned(y)>32767||unsigned(stride)>32767)
      return BufferAccess(base,x,y,stride,bytes);
    const uint32_t offset=(uint32_t(y)*unsigned(stride)+unsigned(x))*bytes;
    if(mirror)return backing+((distance+offset)&0x1fffff);
    return offset<=remaining&&bytes<=remaining-offset?backing+offset:nullptr;
  }
};
}
