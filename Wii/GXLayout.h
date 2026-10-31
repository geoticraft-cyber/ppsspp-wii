#pragma once
#include <cstdint>
namespace Wii {
inline unsigned GXTileOffset(unsigned x,unsigned y,unsigned width) {
  return ((y/4)*(width/4)+x/4)*64+((y%4)*4+x%4)*2;
}
inline unsigned GX565TileOffset(unsigned x,unsigned y,unsigned width) {
  return ((y/4)*(width/4)+x/4)*32+((y%4)*4+x%4)*2;
}
inline void PackGX565Pixel(uint8_t *dest,unsigned width,unsigned x,unsigned y,uint32_t rgb) {
  const uint16_t v=uint16_t(((rgb>>19)&0x1f)<<11 | ((rgb>>10)&0x3f)<<5 | ((rgb>>3)&0x1f));
  const unsigned i=GX565TileOffset(x,y,width);dest[i]=uint8_t(v>>8);dest[i+1]=uint8_t(v);
}
inline uint32_t ExpandGX565(uint32_t rgb) {
  const unsigned r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
  const unsigned r5=r>>3,g6=g>>2,b5=b>>3;
  return ((r5<<3)|(r5>>2))<<16 | ((g6<<2)|(g6>>4))<<8 | ((b5<<3)|(b5>>2));
}
inline void PackGXPixel(uint8_t *dest,unsigned width,unsigned x,unsigned y,uint32_t rgb,uint8_t a=255) {
  unsigned i=GXTileOffset(x,y,width);
  dest[i]=a;dest[i+1]=uint8_t(rgb>>16);dest[i+32]=uint8_t(rgb>>8);dest[i+33]=uint8_t(rgb);
}
inline uint32_t UnpackGXPixel(const uint8_t *src,unsigned width,unsigned x,unsigned y) {
  unsigned i=GXTileOffset(x,y,width);
  return uint32_t(src[i+1])<<16|uint32_t(src[i+32])<<8|src[i+33];
}
inline uint32_t RGBToPSPPixel(uint32_t rgb,uint8_t alpha,unsigned format) {
  unsigned r=rgb>>16&255,g=rgb>>8&255,b=rgb&255;
  switch(format) {
    case 0:return (r>>3)|((g>>2)<<5)|((b>>3)<<11);
    case 1:return (r>>3)|((g>>3)<<5)|((b>>3)<<10)|((alpha>>7)<<15);
    case 2:return (r>>4)|((g>>4)<<4)|((b>>4)<<8)|((alpha>>4)<<12);
    default:return r|(g<<8)|(b<<16)|(uint32_t(alpha)<<24);
  }
}
}
