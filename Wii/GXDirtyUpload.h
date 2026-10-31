// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/GXUpload.h"
#include "Wii/Pixel.h"
#include <cstring>

namespace Wii {
struct NoTileReuse {bool operator()(unsigned)const{return false;}};
// Convert one tile at a time: at most 64 bytes of scratch, independent of width.
// Reuse is permitted only when the caller has verified exact raw source bytes
// and knows this destination still contains the corresponding presentation.
// Flush only aligned runs of initialized, changed tiles.
template<class Flush,class Reuse>
unsigned UploadDirtyBandReuse(uint8_t *dest,unsigned width,unsigned y,
    const uint8_t *const rows[4],unsigned sourceFormat,bool native16,
    bool force,Flush flush,Reuse reuse) {
  if(!dest||!width||width>480||(width&3)||(y&3)||sourceFormat>3)return 0;
  for(unsigned row=0;row<4;++row)if(!rows[row])return 0;
  alignas(32) uint8_t tile[64];
  const unsigned bpp=native16?2:4,sourceBpp=sourceFormat==3?4:2;
  const unsigned bytes=width*4*bpp,tileBytes=16*bpp;
  uint8_t *out=dest+(y/4)*bytes;
  // A forced band is entirely new: convert full rows directly into its tiles.
  // Select the converter four times per band rather than four times per tile.
  // Preserve the old unusual native16/RGBA-source combination below.
  if(force&&!(native16&&sourceFormat==3)) {
    for(unsigned row=0;row<4;++row) {
      if(native16)UploadNative16Row(out,width,row,rows[row],sourceFormat);
      else if(sourceFormat==3)UploadRGBA8Row(out,width,row,rows[row]);
      else UploadRGB16Row(out,width,row,rows[row],sourceFormat);
    }
    flush(out,bytes);
    return bytes;
  }
  unsigned changed=0,run=bytes;
  for(unsigned x=0,offset=0;x<width;x+=4,offset+=tileBytes) {
    bool different=false;
    if(force||!reuse(x)) {
      uint8_t *converted=force?out+offset:tile;
      for(unsigned row=0;row<4;++row) {
        const uint8_t *src=rows[row]+x*sourceBpp;
        if(native16)UploadNative16Row(converted,4,row,src,sourceFormat);
        else if(sourceFormat==3)UploadRGBA8Row(converted,4,row,src);
        else UploadRGB16Row(converted,4,row,src,sourceFormat);
      }
      different=force||std::memcmp(out+offset,tile,tileBytes)!=0;
      if(different&&!force)std::memcpy(out+offset,tile,tileBytes);
    }
    if(different) {changed+=tileBytes;if(run==bytes)run=offset;}
    else if(run!=bytes) {flush(out+run,offset-run);run=bytes;}
  }
  if(run!=bytes)flush(out+run,bytes-run);
  return changed;
}
template<class Flush>
unsigned UploadDirtyBand(uint8_t *dest,unsigned width,unsigned y,
    const uint8_t *const rows[4],unsigned sourceFormat,bool native16,
    bool force,Flush flush) {
  return UploadDirtyBandReuse(dest,width,y,rows,sourceFormat,native16,force,flush,NoTileReuse());
}
}
