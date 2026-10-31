// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include "Wii/GXLayout.h"
#include "Wii/Pixel.h"
namespace Wii {
// Expand directly into the two GX planes. Format and tiled address selection
// are outside the pixel loop; byte accesses also work on unaligned PSP rows.
template<unsigned Format>
inline void UploadRGB16Row(uint8_t *dest,unsigned width,unsigned y,const uint8_t *src) {
  unsigned tile=((y/4)*(width/4))*64+(y%4)*8;
  for(unsigned x=0;x<width;x+=4,tile+=64) {
    for(unsigned i=0;i<4;++i) {
      const uint32_t rgb=DecodePixel(src+(x+i)*2,Format);
      dest[tile+i*2]=255;dest[tile+i*2+1]=uint8_t(rgb>>16);
      dest[tile+32+i*2]=uint8_t(rgb>>8);dest[tile+33+i*2]=uint8_t(rgb);
    }
  }
}
inline void UploadRGB16Row(uint8_t *dest,unsigned width,unsigned y,const uint8_t *src,unsigned format) {
  if(format==0)UploadRGB16Row<0>(dest,width,y,src);
  else if(format==1)UploadRGB16Row<1>(dest,width,y,src);
  else UploadRGB16Row<2>(dest,width,y,src);
}
// PSP BGR little-endian -> GX RGB big-endian. GX expands 4/5/6-bit channels.
// Display alpha is ignored. RGB5A3 uses opaque 555 or opaque 3444 encoding.
inline uint16_t NativeGX16(uint16_t pixel,unsigned format) {
  if(format==0)return uint16_t(((pixel&31)<<11)|(pixel&0x7e0)|(pixel>>11));
  if(format==1)return uint16_t(0x8000|((pixel&31)<<10)|(pixel&0x3e0)|((pixel>>10)&31));
  return uint16_t(0x7000|((pixel&15)<<8)|(pixel&0xf0)|((pixel>>8)&15));
}
inline void UploadNative16Row(uint8_t *dest,unsigned width,unsigned y,const uint8_t *src,unsigned format) {
  unsigned tile=((y/4)*(width/4))*32+(y%4)*8;
  for(unsigned x=0;x<width;x+=4,tile+=32) {
    for(unsigned i=0;i<4;++i) {
      const uint16_t value=NativeGX16(uint16_t(src[(x+i)*2])|uint16_t(src[(x+i)*2+1])<<8,format);
      dest[tile+i*2]=uint8_t(value>>8);dest[tile+i*2+1]=uint8_t(value);
    }
  }
}
inline void UploadRGBA8Row(uint8_t *dest,unsigned width,unsigned y,const uint8_t *src) {
  unsigned tile=((y/4)*(width/4))*64+(y%4)*8;
  for(unsigned x=0;x<width;x+=4,tile+=64) {
    for(unsigned i=0;i<4;++i) {
      dest[tile+i*2]=255;dest[tile+i*2+1]=src[(x+i)*4];
      dest[tile+32+i*2]=src[(x+i)*4+1];dest[tile+33+i*2]=src[(x+i)*4+2];
    }
  }
}
// Destination preloads use the same opaque RGB encoding as scanout. Process
// complete tiles with the row converters; replicate only the final partial
// tile. The band base retains the padded destination pitch on later rows.
inline void UploadPreloadRow(uint8_t *dest,unsigned tiledWidth,unsigned y,
    const uint8_t *src,unsigned count,unsigned format) {
  uint8_t *band=dest+(y/4)*tiledWidth*16;
  const unsigned full=count&~3u,row=y&3u,bpp=format==3?4:2;
  if(full) {
    if(format==3)UploadRGBA8Row(band,full,row,src);
    else UploadRGB16Row(band,full,row,src,format);
  }
  if(full!=count) {
    uint8_t tail[16];
    for(unsigned i=0;i<4;++i) {
      const unsigned x=full+i<count?full+i:count-1;
      for(unsigned byte=0;byte<bpp;++byte)tail[i*bpp+byte]=src[x*bpp+byte];
    }
    uint8_t *tile=band+(full/4)*64;
    if(format==3)UploadRGBA8Row(tile,4,row,tail);
    else UploadRGB16Row(tile,4,row,tail,format);
  }
}
}
