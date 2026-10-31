// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/GXUpload.h"
#include <cstring>
namespace PreviousGX {
using namespace Wii;
// Exact comparisons use the current shared GX buffer. Flush adjacent changed
// tiles as one aligned run, without allocating a texture-sized shadow.
template<unsigned TileBytes,class Convert,class Flush>
unsigned UploadSpriteTiles(uint8_t *dest,unsigned w,unsigned h,Convert convert,bool compare,Flush flush) {
  unsigned changed=0;
  for(unsigned y=0;y<h;y+=4) {
    uint8_t *run=nullptr;unsigned runBytes=0;
    for(unsigned x=0;x<w;x+=4) {
      alignas(32) uint8_t tile[TileBytes];
      auto *out=dest+((y/4)*(w/4)+x/4)*TileBytes;
      convert(compare?tile:out,x,y);
      if(!compare||memcmp(out,tile,TileBytes)) {
        if(compare)memcpy(out,tile,TileBytes);
        changed+=TileBytes;
        if(!run)run=out;
        runBytes+=TileBytes;
      } else if(run) {flush(run,runBytes);run=nullptr;runBytes=0;}
    }
    if(run)flush(run,runBytes);
  }
  return changed;
}
template<class Read,class Flush>
unsigned UploadSpritePatch(uint8_t *dest,unsigned w,unsigned h,Read read,bool compare,Flush flush) {
  return UploadSpriteTiles<64>(dest,w,h,[&](uint8_t *tile,unsigned x,unsigned y) {
    for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i) {
      const uint32_t color=read(x+i,y+j);PackGXPixel(tile,4,i,j,color,uint8_t(color>>24));
    }
  },compare,flush);
}
// GX expands packed channels instead of making Broadway expand each texel.
template<class Read,class Flush>
unsigned UploadNative16Patch(uint8_t *dest,unsigned w,unsigned h,Read read,unsigned format,bool compare,Flush flush) {
  return UploadSpriteTiles<32>(dest,w,h,[&](uint8_t *tile,unsigned x,unsigned y) {
    for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i) {
      const uint16_t pixel=NativeGX16(read(x+i,y+j),format);
      tile[j*8+i*2]=uint8_t(pixel>>8);tile[j*8+i*2+1]=uint8_t(pixel);
    }
  },compare,flush);
}
}
