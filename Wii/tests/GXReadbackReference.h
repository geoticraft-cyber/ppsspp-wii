// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include "Wii/GXLayout.h"
#include "Wii/Pixel.h"
namespace PreviousReadback {
using namespace Wii;
// Walk one tiled row in four-pixel groups. PSP alpha/stencil stays in guest
// memory; the EFB's unused alpha channel must never replace it.
inline void CopyGXColorRow(uint8_t *dest,const uint8_t *tiled,unsigned tiledWidth,
    unsigned y,unsigned count,unsigned format,uint32_t keepMask) {
  const unsigned bpp=format==3?4:2;
  const uint32_t maskRGB=((keepMask&255)<<16)|(keepMask&0xff00)|((keepMask>>16)&255);
  // Common unmasked RGBA8 destination: choose the pipeline once per row.
  // Advance tiled addresses directly; never overwrite the fourth stencil byte.
  if(format==3&&!maskRGB) {
    const uint8_t *p=tiled+GXTileOffset(0,y,tiledWidth);
    for(unsigned x=0;x<count;x+=4,p+=64) {
      const unsigned n=std::min(4u,count-x);
      for(unsigned i=0;i<n;++i) {
        dest[0]=p[i*2+1];dest[1]=p[i*2+32];dest[2]=p[i*2+33];dest+=4;
      }
    }
    return;
  }
  for(unsigned x=0;x<count;x+=4) {
    const auto *p=tiled+GXTileOffset(x,y,tiledWidth);
    for(unsigned i=0;i<std::min(4u,count-x);++i) {
      auto *out=dest+(x+i)*bpp;
      if(format==3) {
        if(!maskRGB) {
          out[0]=p[i*2+1];out[1]=p[i*2+32];out[2]=p[i*2+33];
        } else {
          const uint8_t rgb[]={p[i*2+1],p[i*2+32],p[i*2+33]};
          for(unsigned c=0;c<3;++c) {
            const unsigned mask=(keepMask>>(c*8))&255;
            out[c]=uint8_t((out[c]&mask)|(rgb[c]&~mask));
          }
        }
      } else {
        uint32_t rgb=uint32_t(p[i*2+1])<<16|uint32_t(p[i*2+32])<<8|p[i*2+33];
        if(maskRGB)rgb=(DecodePixel(out,format)&maskRGB)|(rgb&~maskRGB);
        const uint16_t stencil=format==1?0x8000:format==2?0xf000:0;
        const uint16_t old=stencil?(uint16_t(out[0])|uint16_t(out[1])<<8):0;
        const uint16_t value=uint16_t(RGBToPSPPixel(rgb,0,format))|(old&stencil);
        out[0]=uint8_t(value);out[1]=uint8_t(value>>8);
      }
    }
  }
}
}
