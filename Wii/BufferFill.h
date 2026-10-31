#pragma once
#include "Wii/BufferAccess.h"
#include <cstring>
#include <algorithm>
#include "Common/Swap.h"
namespace Wii {
// Split clears at VRAM's mirror boundary, retaining memset throughput.
inline void FillBuffer(uint8_t *base,int x,int y,int stride,unsigned bytes,unsigned count,uint8_t value) {
  while(count) {
    uint8_t *p=BufferAccess(base,x,y,stride,bytes);if(!p)return;
    const uintptr_t distance=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(addressSpace.vram);
    unsigned n=1;
    if(addressSpace.vram&&distance<2*1024*1024)n=unsigned((2*1024*1024-distance)/bytes);
    else {
      const uintptr_t r=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(addressSpace.ram);
      if(addressSpace.ram&&r<addressSpace.ramSize)n=unsigned((addressSpace.ramSize-r)/bytes);
    }
    if(n>count)n=count;if(!n)return;memset(p,value,n*bytes);count-=n;x+=n;
  }
}
// Clear a row through contiguous host spans instead of translating every pixel.
// Keep one shared body for color, stencil-only and depth clears. Encoded values
// are prepared once so the pixel loop stores PSP little-endian bytes on Wii.
class PreparedBufferPattern {
  unsigned bytes;uint32_t keepMask,value;
  uint16_t native16,keep16;uint32_t native32,keep32;
  bool valid,uniform;
public:
  PreparedBufferPattern(unsigned pixelBytes,uint32_t pixelValue,uint32_t mask=0)
      :bytes(pixelBytes),keepMask(mask),value(pixelValue) {
    valid=bytes==2||bytes==4;
    const uint32_t full=bytes==2?0xffffu:0xffffffffu;
    keepMask&=full;value&=full;
    valid=valid&&keepMask!=full;
    uniform=!keepMask&&(value&255)==((value>>8)&255)&&
      (bytes==2||(value&65535)==(value>>16));
    const u16_le little16=uint16_t(value&~keepMask),mask16=uint16_t(keepMask);
    const u32_le little32=value&~keepMask,mask32=keepMask;
    memcpy(&native16,&little16,2);memcpy(&keep16,&mask16,2);
    memcpy(&native32,&little32,4);memcpy(&keep32,&mask32,4);
  }
  __attribute__((noinline)) void Fill(uint8_t *base,int x,int y,int stride,unsigned count) const {
    if(!valid)return;
    if(uniform){FillBuffer(base,x,y,stride,bytes,count,uint8_t(value));return;}
  while(count) {
    auto *p=BufferAccess(base,x,y,stride,bytes);
    // Per-pixel setters ignore invalid pixels and still attempt later ones.
    if(!p) {++x;--count;continue;}
    const uintptr_t address=reinterpret_cast<uintptr_t>(p);
    size_t available=bytes;
    const uintptr_t v=address-reinterpret_cast<uintptr_t>(addressSpace.vram);
    const uintptr_t r=address-reinterpret_cast<uintptr_t>(addressSpace.ram);
    const uintptr_t t=address-reinterpret_cast<uintptr_t>(addressSpace.scratch);
    if(addressSpace.vram&&v<0x200000)available=0x200000-v;
    else if(addressSpace.ram&&r<addressSpace.ramSize)available=addressSpace.ramSize-r;
    else if(addressSpace.scratch&&t<16384)available=16384-t;
    const unsigned n=std::min<size_t>(count,available/bytes);
    if(!n)return;
    if(bytes==2) {
      auto *out=reinterpret_cast<uint16_t *>(p);
      if(keepMask)for(unsigned i=0;i<n;++i)out[i]=(out[i]&keep16)|native16;
      else std::fill(out,out+n,native16);
    } else {
      auto *out=reinterpret_cast<uint32_t *>(p);
      if(keepMask)for(unsigned i=0;i<n;++i)out[i]=(out[i]&keep32)|native32;
      else std::fill(out,out+n,native32);
    }
    count-=n;x+=n;
  }
  }
};
__attribute__((noinline)) inline void FillBufferPattern(uint8_t *base,int x,int y,int stride,
    unsigned bytes,unsigned count,uint32_t value,uint32_t keepMask=0) {
  PreparedBufferPattern(bytes,value,keepMask).Fill(base,x,y,stride,count);
}

}
