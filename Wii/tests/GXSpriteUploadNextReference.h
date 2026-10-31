// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/GXUpload.h"
#include <cstring>
namespace PreviousGXNext {
using namespace Wii;
// Exact comparisons use the current shared GX buffer. Flush adjacent changed
// tiles as one aligned run, without allocating a texture-sized shadow.
template<unsigned TileBytes,class Convert,class Flush>
unsigned UploadSpriteTiles(uint8_t *dest,unsigned w,unsigned h,Convert convert,bool compare,Flush flush) {
  // Full replacements have no old pixels to inspect. All tiles form one
  // initialized, aligned span; flush once after conversion completes.
  if(!compare) {
    auto *out=dest;
    for(unsigned y=0;y<h;y+=4)for(unsigned x=0;x<w;x+=4,out+=TileBytes)
      convert(out,x,y);
    const unsigned bytes=(w/4)*(h/4)*TileBytes;
    if(bytes)flush(dest,bytes);
    return bytes;
  }
  unsigned changed=0;
  uint8_t *run=nullptr;unsigned runBytes=0;
  auto *out=dest;
  for(unsigned y=0;y<h;y+=4) {
    for(unsigned x=0;x<w;x+=4,out+=TileBytes) {
      alignas(32) uint8_t tile[TileBytes];
      convert(tile,x,y);
      if(memcmp(out,tile,TileBytes)) {
        memcpy(out,tile,TileBytes);
        changed+=TileBytes;
        if(!run)run=out;
        runBytes+=TileBytes;
      } else if(run) {flush(run,runBytes);run=nullptr;runBytes=0;}
    }
  }
  if(run)flush(run,runBytes);
  return changed;
}
template<class Read,class Flush>
unsigned UploadSpritePatch(uint8_t *dest,unsigned w,unsigned h,Read read,bool compare,Flush flush) {
  return UploadSpriteTiles<64>(dest,w,h,[&](uint8_t *tile,unsigned x,unsigned y) {
    for(unsigned j=0;j<4;++j) {
      auto *ar=tile+j*8,*gb=ar+32;
      for(unsigned i=0;i<4;++i,ar+=2,gb+=2) {
        const uint32_t color=read(x+i,y+j);
        ar[0]=uint8_t(color>>24);ar[1]=uint8_t(color>>16);
        gb[0]=uint8_t(color>>8);gb[1]=uint8_t(color);
      }
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
