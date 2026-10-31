// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/GuestSurface.h"
#include "Wii/GXLayout.h"
#include <algorithm>
namespace Wii {
inline void StoreOpaqueRGB(uint8_t *dst,unsigned format,uint32_t rgb) {
  if(format==3) {dst[0]=rgb>>16;dst[1]=rgb>>8;dst[2]=rgb;return;}
  const uint16_t keep=format==1?0x8000:format==2?0xf000:0;
  const uint16_t old=uint16_t(dst[0])|uint16_t(dst[1])<<8;
  const uint16_t value=(old&keep)|uint16_t(RGBToPSPPixel(rgb,0,format));
  dst[0]=value;dst[1]=value>>8;
}
// Resolve the framebuffer encoding once per contiguous span, including stencil
// preservation. The pixel callback still runs exactly once in guest order.
template<unsigned Format,class Pixel>
inline void StoreOpaqueSpan(uint8_t *dst,unsigned n,unsigned x,unsigned y,Pixel &pixel) {
  const unsigned bpp=Format==3?4:2;
  for(unsigned i=0;i<n;++i)StoreOpaqueRGB(dst+i*bpp,Format,pixel(x+i,y));
}
// Restricted opaque spans: caller proves tests/blending/logic/masks inactive.
// Preflight all rows before the first write; mirror boundaries split host spans.
template<class Pixel>
bool DrawOpaqueSurface(uint32_t frame,unsigned stride,unsigned format,
    unsigned x,unsigned y,unsigned w,unsigned h,Pixel pixel,bool (*cancelled)()=nullptr) {
  if(format>3)return false;
  const unsigned bpp=format==3?4:2;
  if((frame&(bpp-1))||!ValidSurface(frame,stride,x,y,w,h,bpp))return false;
  for(unsigned row=0;row<h;++row) {
    if(cancelled&&cancelled())return true;
    uint32_t address;SurfaceRow(frame,stride,x,y+row,w,bpp,address);
    unsigned col=0;
    while(col<w) {
      const unsigned n=std::min<unsigned>(w-col,addressSpace.Span(address)/bpp);
      if(!n)return false;
      auto *dst=addressSpace.Translate(address);
      switch(format) {
        case 0:StoreOpaqueSpan<0>(dst,n,x+col,y+row,pixel);break;
        case 1:StoreOpaqueSpan<1>(dst,n,x+col,y+row,pixel);break;
        case 2:StoreOpaqueSpan<2>(dst,n,x+col,y+row,pixel);break;
        case 3:StoreOpaqueSpan<3>(dst,n,x+col,y+row,pixel);break;
      }
      address+=n*bpp;col+=n;
    }
  }
  return true;
}
}
