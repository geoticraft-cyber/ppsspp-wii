// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include "Wii/GXLayout.h"
#include "Wii/Pixel.h"
namespace Wii {
inline bool GXColorWriteIsNoop(unsigned format,uint32_t keepMask) {
  if(format>3)return false;
  if(format==3)return (keepMask&0xffffffu)==0xffffffu;
  const uint32_t rgb=((keepMask&255)<<16)|(keepMask&0xff00)|((keepMask>>16)&255);
  const uint16_t stencil=format==1?0x8000:format==2?0xf000:0;
  return (uint16_t(RGBToPSPPixel(rgb,0,format))|stencil)==0xffffu;
}
template<unsigned Format>
inline void CopyGXColorRow16(uint8_t *dest,const uint8_t *tiled,unsigned tiledWidth,
    unsigned y,unsigned count,uint32_t maskRGB) {
    // Mask in packed PSP space: decoding old RGB and then quantizing it again
    // cannot change any retained packed color bit. Stencil is always retained.
    const uint16_t stencil=Format==1?0x8000:Format==2?0xf000:0;
    const uint16_t keep=uint16_t(RGBToPSPPixel(maskRGB,0,Format))|stencil;
    const unsigned rBits=Format==2?4:5,gBits=Format==0?6:rBits;
    const unsigned rDrop=8-rBits,gDrop=8-gBits;
    const uint8_t *p=tiled+GXTileOffset(0,y,tiledWidth);
    for(unsigned x=0;x<count;x+=4,p+=64) {
      const unsigned n=std::min(4u,count-x);
      for(unsigned i=0;i<n;++i,dest+=2) {
        uint16_t value=(p[i*2+1]>>rDrop)|((p[i*2+32]>>gDrop)<<rBits)|
          ((p[i*2+33]>>rDrop)<<(rBits+gBits));
        if(keep) {
          const uint16_t old=uint16_t(dest[0])|uint16_t(dest[1])<<8;
          value=(old&keep)|(value&~keep);
        }
        dest[0]=uint8_t(value);dest[1]=uint8_t(value>>8);
      }
    }
}
// Walk one tiled row in four-pixel groups. PSP alpha/stencil stays in guest
// memory; the EFB's unused alpha channel must never replace it.
inline void CopyGXColorRow(uint8_t *dest,const uint8_t *tiled,unsigned tiledWidth,
    unsigned y,unsigned count,unsigned format,uint32_t keepMask) {
  // Callers accept only the four PSP framebuffer formats.
  if(format>3)return;
  if(GXColorWriteIsNoop(format,keepMask))return;
  const uint32_t maskRGB=((keepMask&255)<<16)|(keepMask&0xff00)|((keepMask>>16)&255);
  if(format<4&&maskRGB==0xffffffu)return;
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
  // RGBA4444 uses constant nibble shifts; retain the existing other paths.
  if(format==2) {
    CopyGXColorRow16<2>(dest,tiled,tiledWidth,y,count,maskRGB);
    return;
  }
  if(format<3) {
    // Mask in packed PSP space: decoding old RGB and then quantizing it again
    // cannot change any retained packed color bit. Stencil is always retained.
    const uint16_t stencil=format==1?0x8000:format==2?0xf000:0;
    const uint16_t keep=uint16_t(RGBToPSPPixel(maskRGB,0,format))|stencil;
    const unsigned rBits=format==2?4:5,gBits=format==0?6:rBits;
    const unsigned rDrop=8-rBits,gDrop=8-gBits;
    const uint8_t *p=tiled+GXTileOffset(0,y,tiledWidth);
    for(unsigned x=0;x<count;x+=4,p+=64) {
      const unsigned n=std::min(4u,count-x);
      for(unsigned i=0;i<n;++i,dest+=2) {
        uint16_t value=(p[i*2+1]>>rDrop)|((p[i*2+32]>>gDrop)<<rBits)|
          ((p[i*2+33]>>rDrop)<<(rBits+gBits));
        if(keep) {
          const uint16_t old=uint16_t(dest[0])|uint16_t(dest[1])<<8;
          value=(old&keep)|(value&~keep);
        }
        dest[0]=uint8_t(value);dest[1]=uint8_t(value>>8);
      }
    }
    return;
  }
  // Only masked RGBA8 remains. Hoist channel masks out of the pixel loop.
  const unsigned mr=keepMask&255,mg=(keepMask>>8)&255,mb=(keepMask>>16)&255;
  const uint8_t *p=tiled+GXTileOffset(0,y,tiledWidth);
  for(unsigned x=0;x<count;x+=4,p+=64) {
    const unsigned n=std::min(4u,count-x);
    for(unsigned i=0;i<n;++i,dest+=4) {
      dest[0]=uint8_t((dest[0]&mr)|(p[i*2+1]&~mr));
      dest[1]=uint8_t((dest[1]&mg)|(p[i*2+32]&~mg));
      dest[2]=uint8_t((dest[2]&mb)|(p[i*2+33]&~mb));
    }
  }
}
}
