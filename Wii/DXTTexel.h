#pragma once
#include "GPU/Common/TextureDecoder.h"

namespace Wii {
// Decode only the requested texel. No persistent cache: each sample observes
// current guest bytes, including framebuffer feedback and mirrored backing.
inline u32 DXTColorTexel(const DXT1Block &src,unsigned x,unsigned y,bool forceOpaque) {
  const unsigned index=(src.lines[y]>>(2*x))&3;
  const u16 c1=src.color1,c2=src.color2;
  const auto expand=[](u16 c)->u32 {
    const unsigned r=c&31,g=(c>>5)&63,b=c>>11;
    return 0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2));
  };
  if(index==0)return expand(c1);
  if(index==1)return expand(c2);
  if(c1<=c2&&!forceOpaque&&index==3)return expand(c2)&0xffffff;
  const u32 a=expand(c1),b=expand(c2);
  u32 result=0xff000000;
  for(unsigned shift=0;shift<24;shift+=8) {
    const int v1=(a>>shift)&255,v2=(b>>shift)&255;
    // Preserve the reference decoder's signed-shift interpolation and rounding.
    const int delta=((v2-v1)>>1)-((v2-v1)>>3);
    const unsigned value=c1<=c2&&!forceOpaque?(v1+v2+1)/2:index==2?v1+delta:v2-delta;
    result|=value<<shift;
  }
  return result;
}
inline u32 DXTTexel(const DXT1Block &src,unsigned x,unsigned y) {
  return DXTColorTexel(src,x,y,false);
}
inline u32 DXTTexel(const DXT3Block &src,unsigned x,unsigned y) {
  const unsigned alpha=(src.alphaLines[y]>>(4*x))&15;
  return (DXTColorTexel(src.color,x,y,true)&0xffffff)|(alpha*17u<<24);
}
inline u32 DXTTexel(const DXT5Block &src,unsigned x,unsigned y) {
  const u64 bits=(u64(u16(src.alphadata1))<<32)|u32(src.alphadata2);
  const unsigned index=(bits>>(3*(y*4+x)))&7;
  unsigned alpha;
  if(index<2)alpha=index?src.alpha2:src.alpha1;
  else if(src.alpha1<=src.alpha2&&index>=6)alpha=index==6?0:255;
  else {
    // Keep the existing float evaluation, including its truncation behavior.
    // The block decoder calls lerp with constant indices. Keep those exact
    // rounded float constants without a per-sample divide or int conversion.
    static const float seventh[]={1.0f/7.0f,2.0f/7.0f,3.0f/7.0f,4.0f/7.0f,5.0f/7.0f,6.0f/7.0f};
    static const float fifth[]={1.0f/5.0f,2.0f/5.0f,3.0f/5.0f,4.0f/5.0f};
    const float d=src.alpha1>src.alpha2?seventh[index-2]:fifth[index-2];
    alpha=u8(src.alpha1+(src.alpha2-src.alpha1)*d);
  }
  return (DXTColorTexel(src.color,x,y,true)&0xffffff)|(alpha<<24);
}
}
