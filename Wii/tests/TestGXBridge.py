#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual GX bridge with a host command/cache mock, not Wii hardware.
The mock checks host control flow and texture bytes, not SDK ABI or GX timing.
"""
from pathlib import Path
import re, subprocess, tempfile, sys
full_upload="--full-upload" in sys.argv
benchmark="--logic-benchmark" in sys.argv or "--speed-benchmark" in sys.argv
root=Path(__file__).resolve().parents[2]
source=(root/'Wii/GXRender.cpp').read_text()
constants=sorted(set(re.findall(r'\bGX_[A-Z][A-Za-z0-9_]*\b',source)))
functions=set(re.findall(r'\b((?:GX_|gu)[A-Za-z0-9_]+)\s*\(',source))
constants=sorted(set(constants)-functions)
special={'GX_GetDrawSync','GX_SetDrawSync','GX_Flush','GX_InvalidateTexAll','GX_GetYScaleFactor','GX_SetDispCopyYScale','GX_SetTexCopyDst','GX_CopyTex','GX_CopyDisp','GX_InitTexObj','GX_LoadTexObj','GX_Begin','GX_Position3f32','GX_Color4u8','GX_TexCoord2f32','GX_SetNumTexGens','GX_AbortFrame','GX_End','GX_SetAlphaCompare','GX_SetBlendMode','GX_SetTevOp','GX_SetTevColorIn','GX_SetTevColorOp','GX_SetViewport'}
with tempfile.TemporaryDirectory(prefix='gx-bridge-',dir=Path.cwd()) as folder:
    folder=Path(folder);(folder/'ogc').mkdir()
    header='''#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <initializer_list>
#include "Wii/GXLayout.h"

using Mtx44=float[4][4];using Mtx=float[3][4];
struct GXColor {uint8_t r,g,b,a;};
struct GXRModeObj {unsigned fbWidth=640,efbHeight=480,xfbHeight=480,viHeight=480,copy_interlaced=0,aa=0,field_rendering=0;uint8_t sample_pattern[12][2]={},vfilter[7]={};};
struct GXTexObj {uint8_t *data;unsigned width,height,format;};
'''
    header+='enum { '+','.join(constants)+' };\n'
    header+='\n'.join('template<class... T> inline void '+f+'(T...) {}' for f in sorted(functions-special))+'\n'
    header+='''
namespace Mock {
static unsigned begins=0,viewports=0,dcCalls=0;
static unsigned token=0,queued=0,flushes=0,dcBytes=0,copyWidth=4,copyHeight=4,displays=0,aborts=0,invalidateAll=0;
static bool badAdd=false,customTev=false;
static unsigned tevInput[4],tevScale=0;
static bool hang=false,textured=false,failAllocation=false;
static GXTexObj texture;
static const uint8_t *liveTexture=nullptr;
static unsigned liveBytes=0,liveHash=0;
static unsigned TextureHash(const uint8_t *p,unsigned n) {
 unsigned hash=2166136261u;for(unsigned i=0;i<n;++i)hash=(hash^p[i])*16777619u;return hash;
}
static void CheckLiveTexture() {
 if(liveTexture)assert(TextureHash(liveTexture,liveBytes)==liveHash);
}
static uint32_t efb[1024*1024]={};
static unsigned blendMode,logicOp;static bool coverage=false;
static float px[4],py[4],tu[4],tv[4];static unsigned vertices=0,color=0;
}
inline void GX_SetViewport(float,float,float,float,float,float) {++Mock::viewports;}
inline void GX_SetTevOp(unsigned,unsigned) {Mock::customTev=false;}
inline void GX_SetTevColorIn(unsigned,unsigned a,unsigned b,unsigned c,unsigned d) {Mock::tevInput[0]=a;Mock::tevInput[1]=b;Mock::tevInput[2]=c;Mock::tevInput[3]=d;}
inline void GX_SetTevColorOp(unsigned,unsigned op,unsigned bias,unsigned scale,bool clamp,unsigned reg) {
 assert(op==GX_TEV_ADD&&bias==GX_TB_ZERO&&(scale==GX_CS_SCALE_1||scale==GX_CS_SCALE_2)&&clamp&&reg==GX_TEVPREV);Mock::customTev=true;Mock::tevScale=scale;
}
inline unsigned GX_GetDrawSync() {return Mock::token;}
inline void GX_SetDrawSync(unsigned t) {Mock::queued=t;}
inline void GX_Flush() {Mock::CheckLiveTexture();++Mock::flushes;if(!Mock::hang){Mock::token=Mock::queued;Mock::liveTexture=nullptr;}}
inline void GX_InvalidateTexAll() {++Mock::invalidateAll;}
inline void GX_AbortFrame() {++Mock::aborts;Mock::liveTexture=nullptr;}
inline float GX_GetYScaleFactor(unsigned a,unsigned b) {return float(b)/a;}
inline unsigned GX_SetDispCopyYScale(float) {return 480;}
inline void GX_SetTexCopyDst(unsigned w,unsigned h,unsigned,bool) {Mock::copyWidth=w;Mock::copyHeight=h;}
inline void GX_InitTexObj(GXTexObj *t,void *p,unsigned w,unsigned h,unsigned f,unsigned,unsigned,bool) {*t={static_cast<uint8_t *>(p),w,h,f};}
inline void GX_LoadTexObj(GXTexObj *t,unsigned) {Mock::texture=*t;}
inline void GX_SetNumTexGens(unsigned n) {Mock::textured=n!=0;}
inline void GX_Begin(unsigned,unsigned,unsigned) {Mock::CheckLiveTexture();++Mock::begins;Mock::vertices=0;}
inline void GX_Position3f32(float x,float y,float) {unsigned v=Mock::vertices++;Mock::px[v]=x;Mock::py[v]=y;}
inline void GX_Color4u8(uint8_t r,uint8_t g,uint8_t b,uint8_t) {Mock::color=unsigned(r)<<16|unsigned(g)<<8|b;}
inline void GX_TexCoord2f32(float u,float v) {Mock::tu[Mock::vertices-1]=u;Mock::tv[Mock::vertices-1]=v;}
inline void GX_SetAlphaCompare(unsigned compare,unsigned,unsigned,unsigned,unsigned) {Mock::coverage=compare==GX_GREATER;}
inline void GX_SetBlendMode(unsigned mode,unsigned,unsigned,unsigned op) {Mock::blendMode=mode;Mock::logicOp=op;}
inline unsigned Logic(unsigned op,unsigned s,unsigned d) {
  switch(op) {
    case GX_LO_CLEAR:return 0;case GX_LO_AND:return s&d;case GX_LO_REVAND:return s&~d;case GX_LO_COPY:return s;
    case GX_LO_INVAND:return ~s&d;case GX_LO_NOOP:return d;case GX_LO_XOR:return s^d;case GX_LO_OR:return s|d;
    case GX_LO_NOR:return ~(s|d);case GX_LO_EQUIV:return ~(s^d);case GX_LO_INV:return ~d;case GX_LO_REVOR:return s|~d;
    case GX_LO_INVCOPY:return ~s;case GX_LO_INVOR:return ~s|d;case GX_LO_NAND:return ~(s&d);case GX_LO_SET:return 0xffffff;
  }assert(false);return 0;
}
inline void GX_End() {
  for(unsigned y=unsigned(Mock::py[0]);y<unsigned(Mock::py[2]);++y)for(unsigned x=unsigned(Mock::px[0]);x<unsigned(Mock::px[1]);++x) {
    assert(x<1024&&y<1024);unsigned alpha=255;
    unsigned rgb=Mock::color;
    if(Mock::textured) {
      auto &t=Mock::texture;
      unsigned sx=unsigned((Mock::tu[0]+(x+0.5f)/Mock::px[1]*(Mock::tu[1]-Mock::tu[0]))*t.width);
      unsigned sy=unsigned((Mock::tv[0]+(y+0.5f)/Mock::py[2]*(Mock::tv[2]-Mock::tv[0]))*t.height);
      if(sx>=t.width)sx=t.width-1;if(sy>=t.height)sy=t.height-1;
      if(t.format==GX_TF_RGBA8) {rgb=Wii::UnpackGXPixel(t.data,t.width,sx,sy);alpha=t.data[Wii::GXTileOffset(sx,sy,t.width)];}
      else {
        unsigned at=Wii::GX565TileOffset(sx,sy,t.width),v=unsigned(t.data[at])<<8|t.data[at+1];
        unsigned r,g,b;
        if(t.format==GX_TF_RGB565) {r=(v>>11)&31;g=(v>>5)&63;b=v&31;r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);}
        else if(v&0x8000) {r=(v>>10)&31;g=(v>>5)&31;b=v&31;r=(r<<3)|(r>>2);g=(g<<3)|(g>>2);b=(b<<3)|(b>>2);}
        else {r=((v>>8)&15)*17;g=((v>>4)&15)*17;b=(v&15)*17;}
        rgb=r<<16|g<<8|b;
      }
    }
    if(Mock::customTev) {
      unsigned result=0;
      for(unsigned shift:{0u,8u,16u}) {
        auto input=[&](unsigned n)->unsigned {
          if(n==GX_CC_TEXC)return (rgb>>shift)&255;
          if(n==GX_CC_RASC)return (Mock::color>>shift)&255;
          if(n==GX_CC_ONE)return 255;
          assert(n==GX_CC_ZERO);return 0;
        };
        const unsigned a=input(Mock::tevInput[0]),b=input(Mock::tevInput[1]),c=input(Mock::tevInput[2]),d=input(Mock::tevInput[3]);
        // GX TEV color equation: D + (A - B) * C, with 8-bit C.
        int v=int(d)+(int(a)-int(b))*int(c)/255;
        if(Mock::tevScale==GX_CS_SCALE_2)v*=2;
        if(v<0)v=0;if(v>255)v=255;result|=unsigned(v)<<shift;
      }
      rgb=result^(Mock::badAdd?1:0);
    }
    if(Mock::coverage&&!alpha)continue;
    unsigned &dst=Mock::efb[y*1024+x];
    if(Mock::blendMode==GX_BM_LOGIC)rgb=Logic(Mock::logicOp,rgb,dst)&0xffffff;
    if(Mock::blendMode==GX_BM_BLEND) {
      unsigned result=0;
      for(unsigned shift:{0u,8u,16u})result|=((((rgb>>shift)&255)*alpha+((dst>>shift)&255)*(255-alpha))/255)<<shift;
      rgb=result;
    }
    dst=rgb;
  }
  if(Mock::textured) {
    Mock::liveTexture=Mock::texture.data;
    Mock::liveBytes=Mock::texture.width*Mock::texture.height*(Mock::texture.format==GX_TF_RGBA8?4:2);
    Mock::liveHash=Mock::TextureHash(Mock::liveTexture,Mock::liveBytes);
  }
}
inline void GX_CopyTex(void *p,bool) {
  Mock::CheckLiveTexture();Mock::liveTexture=nullptr; // ordered copy follows texture consumption
  for(unsigned y=0;y<Mock::copyHeight;++y)for(unsigned x=0;x<Mock::copyWidth;++x)
    Wii::PackGXPixel(static_cast<uint8_t *>(p),Mock::copyWidth,x,y,Mock::efb[y*1024+x]);
}
inline void GX_CopyDisp(void *,bool) {++Mock::displays;}
inline void DCFlushRange(void *,unsigned n) {Mock::dcBytes+=n;++Mock::dcCalls;}
inline void DCInvalidateRange(void *,unsigned) {}
'''
    (folder/'gccore.h').write_text(header)
    (folder/'ogc/lwp_watchdog.h').write_text('#pragma once\n#include <cstdint>\ninline uint64_t gettime() {static uint64_t t=0;return t+=1000;}\ninline uint64_t ticks_to_millisecs(uint64_t t) {return t;}\n')
    test=folder/'test.cpp'
    test.write_text('''
#include "Wii/GXRender.cpp"
#include <vector>
namespace Wii {
AddressSpace addressSpace;
static Settings settings;
Settings &GetSettings() {return settings;}
void RuntimeProgress(uint32_t,unsigned) {}
void DiagnosticPhase(const char *) {}
void AllocationFailure(const char *,size_t,size_t,size_t,uintptr_t,int,uintptr_t,unsigned,unsigned) {}
}
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char*,...) {}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return false;}
namespace Memory {u32 g_MemorySize=32*1024*1024;}
extern "C" void *__real_memalign(size_t,size_t);
extern "C" void *__wrap_memalign(size_t alignment,size_t bytes) {
  if(Mock::failAllocation) {Mock::failAllocation=false;return nullptr;}
  return __real_memalign(alignment,bytes);
}
extern "C" int __lsan_is_turned_off() {return 1;}
static void VerifyDisplayTexture(uint32_t frame,unsigned stride,unsigned format) {
  const unsigned sourceBpp=format==3?4:2;
  for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x) {
    uint8_t pixel[4];assert(Wii::CopyFromGuest(pixel,frame+(y*stride+x)*sourceBpp,sourceBpp));
    if(Mock::texture.format==GX_TF_RGBA8)
      assert(Wii::UnpackGXPixel(Mock::texture.data,480,x,y)==Wii::DecodePixel(pixel,format));
    else {
      const unsigned at=Wii::GX565TileOffset(x,y,480);
      const unsigned actual=unsigned(Mock::texture.data[at])<<8|Mock::texture.data[at+1];
      assert(actual==Wii::NativeGX16(unsigned(pixel[0])|unsigned(pixel[1])<<8,format));
    }
  }
}
int main() {
  std::vector<uint8_t> ram(32*1024*1024),vram(2*1024*1024);
  auto &map=Wii::addressSpace;map.ram=ram.data();map.ramSize=ram.size();map.vram=vram.data();map.RebuildPages();
  GXRModeObj mode;uint8_t xfb=0;
  for(unsigned i=0;i<vram.size();++i)vram[i]=i*13+i/31;
  assert(!Wii::GXPresent(0x04000000,512,3,nullptr,&xfb));
  // Both sides of the 2 MiB mirror, all PSP formats and native self-tests.
  for(unsigned format=0;format<4;++format) {
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb)); // admit stable bands
    unsigned first=Mock::dcBytes,viewport=Mock::viewports,invalidations=Mock::invalidateAll;
    const auto converted=Wii::GetGraphicsWork().gxDisplayConvertedPixels,skipped=Wii::GetGraphicsWork().gxSourceSkippedPixels;
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    assert(Mock::viewports==viewport+1); // one common setup, not two
#ifdef WII_GX_FULL_UPLOAD
    assert(Mock::dcBytes-first==480*272*(format==3?4:2));
    assert(Mock::invalidateAll==invalidations+1);
#else
    assert(Mock::dcBytes==first); // no texture writes/cache flushes for a static frame
    assert(Wii::GetGraphicsWork().gxDisplayConvertedPixels==converted);
    assert(Wii::GetGraphicsWork().gxSourceSkippedPixels==skipped+480*272);
    assert(Mock::invalidateAll==invalidations); // unchanged texture stays valid
#endif
    first=Mock::dcBytes;
    invalidations=Mock::invalidateAll;
    unsigned bpp=format==3?4:2;map.Translate(0x041ffff0)[0]^=0x10;
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    assert(Mock::invalidateAll==invalidations+1);
#ifdef WII_GX_FULL_UPLOAD
    assert(Mock::dcBytes-first==480*272*bpp);
#else
    assert(Mock::dcBytes-first==16*bpp);
    assert(Wii::GetGraphicsWork().gxDisplayConvertedPixels==converted+16);
    // Repeated sparse edits retain exact raw snapshots, not just the first edit.
    unsigned bytesBefore=Mock::dcBytes;
    auto convertedBefore=Wii::GetGraphicsWork().gxDisplayConvertedPixels;
    map.Translate(0x041ffff0+33*bpp)[0]^=0x10;
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    assert(Mock::dcBytes-bytesBefore==16*bpp);
    assert(Wii::GetGraphicsWork().gxDisplayConvertedPixels-convertedBefore==16);
    assert(Wii::displaySource.HasBand(0));
    for(unsigned x=0;x<480;x+=4)map.Translate(0x041ffff0+x*bpp)[0]^=0x10;
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    assert(!Wii::displaySource.HasBand(0)); // broad animation retires the memo
#endif
    VerifyDisplayTexture(0x041ffff0,512,format);
    assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
    unsigned random=0x12345678;
    for(unsigned iteration=0;iteration<12;++iteration) {
      random=random*1664525+1013904223;
      const unsigned x=(random>>8)%480,y=(random>>20)%272;
      map.Translate(0x041ffff0+(y*512+x)*bpp)[0]^=0x10;
      assert(Wii::GXPresent(0x041ffff0,512,format,&mode,&xfb));
      VerifyDisplayTexture(0x041ffff0,512,format);
    }
  }
  for(bool wide:{false,true}) {
    Wii::GetSettings().aspect=wide;
    const unsigned before=Mock::begins;
    assert(Wii::GXPresent(0x04000000,512,3,&mode,&xfb));
    assert(Mock::begins-before==(wide?1:2)); // clear quad only for letterbox bars
  }
  assert(Wii::GXPresent(0x04000000,512,3,&mode,&xfb));assert(Mock::texture.format==GX_TF_RGBA8);
  // Low-memory mode drops the raw shadow and uses exact converted tiles.
  Wii::GetSettings().lowMemory=true;
  assert(Wii::GXPresent(0x04000000,512,3,&mode,&xfb));assert(!Wii::displaySource.Bytes());
  Wii::GetSettings().lowMemory=false;
  const unsigned count=Mock::displays;
  assert(!Wii::GXPresent(0x09fffff0,512,3,&mode,&xfb));assert(count==Mock::displays);
  assert(!Wii::GXPresent(0x04000000,0xffffffff,3,&mode,&xfb));
  assert(!Wii::GXSprite(0x04000000,512,0,0,480,272,0x08000000,0xffffffff,3,3,480,256,0,0,480,256));
  assert(!Wii::GXSprite(0x04000000,512,0,0,480,272,0x08000000,480,3,3,480,256,NAN,0,480,256));
  // Real sprite output preserves guest alpha/stencil, and invalidates the
  // presentation texture even when both users share the same allocation.
  for(unsigned i=0;i<256*128*4;++i)ram[i]=i*11+i/17;
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x)vram[(y*512+x)*4+3]=0x71;
  auto sprite=[&]() {return Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,256,3,3,256,128,0,0,256,128);};
  assert(sprite());assert(!Wii::presentTextureValid);
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
    assert(!memcmp(vram.data()+(y*512+x)*4,ram.data()+(y*256+x)*4,3));
    assert(vram[(y*512+x)*4+3]==0x71);
  }
  // Indexed uploads use a caller-provided palette decoder and preserve the
  // same framebuffer/stencil semantics as direct textures.
  auto palette=[](unsigned x,unsigned y,const void *)->uint32_t {return ((x&255)<<16)|((y&255)<<8)|((x^y)&255);};
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,256,4,3,256,128,0,0,256,128,palette,nullptr));
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
    auto p=vram.data()+(y*512+x)*4;
    assert(p[0]==x&&p[1]==y&&p[2]==(x^y)&&p[3]==0x71);
  }
  // An enlarged crop of a 512x512 atlas no longer uploads the entire atlas.
  unsigned calls=0;
  auto crop=[](unsigned x,unsigned y,const void *context)->uint32_t {
    ++*static_cast<unsigned *>(const_cast<void *>(context));return (x<<16)|(y<<8)|0x31;
  };
  auto atlas=[&]() {return Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,512,3,3,512,512,64,32,128,64,crop,&calls);};
  assert(atlas());assert(calls==64*32);
  unsigned flushed=Mock::dcBytes;calls=0;assert(atlas());assert(calls==64*32&&Mock::dcBytes==flushed);
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
    auto p=vram.data()+(y*512+x)*4;assert(p[0]==64+x/4&&p[1]==32+y/4&&p[2]==0x31&&p[3]==0x71);
  }
  // Destination preload must survive texture reuse. Test coverage, all logic
  // truth tables, channel masks and rejection of approximate blending.
  auto rgba=[](unsigned x,unsigned y,const void *)->uint32_t {return (((x+y)&1)?0x80000000:0)|0x345678;};
  Wii::GXSpriteState advanced;advanced.coverage=true;
  for(unsigned operation=0;operation<18;++operation) {
    advanced.logic=operation<16;advanced.logicOp=operation<16?operation:3;
    advanced.blend=operation==16;advanced.keepMask=operation==17?0x0000ff:0;
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      auto p=vram.data()+(y*512+x)*4;p[0]=0x12;p[1]=0x89;p[2]=0xab;p[3]=0x71;
    }
    if(operation==16) {
      const auto saved=vram;const unsigned fences=Mock::flushes,begins=Mock::begins;
      assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,rgba,nullptr,&advanced));
      assert(vram==saved&&Mock::flushes==fences&&Mock::begins==begins);continue;
    }
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,rgba,nullptr,&advanced));
    const unsigned ops[]={GX_LO_CLEAR,GX_LO_AND,GX_LO_REVAND,GX_LO_COPY,GX_LO_INVAND,GX_LO_NOOP,GX_LO_XOR,GX_LO_OR,
      GX_LO_NOR,GX_LO_EQUIV,GX_LO_INV,GX_LO_REVOR,GX_LO_INVCOPY,GX_LO_INVOR,GX_LO_NAND,GX_LO_SET};
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      unsigned expected=0x1289ab;
      if(((x/2+y/2)&1)) {
        expected=operation<16?Logic(ops[operation],0x345678,0x1289ab)&0xffffff:operation==17?0x125678:0;
        if(operation==16)for(unsigned sh:{0u,8u,16u})expected|=(((((0x345678u>>sh)&255)*128+((0x1289abu>>sh)&255)*127)/255)<<sh);
      }
      auto p=vram.data()+(y*512+x)*4;
      assert(p[0]==(expected>>16)&&p[1]==((expected>>8)&255)&&p[2]==(expected&255)&&p[3]==0x71);
    }
  }
  // Warm destination-dependent draws use one completion fence. Preload uses
  // readback scratch, leaving the cached source texture untouched.
  unsigned warmFences=Mock::flushes,warmBytes=Mock::dcBytes;
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,rgba,nullptr,&advanced));
  assert(Mock::flushes==warmFences+1);
  assert(Mock::dcBytes-warmBytes==256*128*4); // preload only; source is unchanged
  assert(Mock::texture.data==Wii::texture&&Wii::texture!=Wii::readback);
  // Native packed crops use half the RGBA upload bytes, including unaligned
  // atlas origins, tile padding, stencil preservation and reversed UVs.
  for(unsigned format=0;format<3;++format) {
    const unsigned sx=3,sy=5,cw=63,ch=31;
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      const uint16_t pixel=uint16_t((x*311+y*71)^0xad35);
      ram[(y*256+x)*2]=pixel;ram[(y*256+x)*2+1]=pixel>>8;
    }
    for(bool flip:{false,true}) {
      const unsigned first=Mock::dcBytes,calls=Mock::dcCalls;
      assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,256,format,3,256,128,
        flip?sx+cw:sx,sy,flip?sx:sx+cw,sy+ch));
      assert(Mock::texture.format==(format==0?GX_TF_RGB565:GX_TF_RGB5A3));
      assert(Mock::dcBytes-first<=64*32*2); // native crop, not full atlas/RGBA
      assert(Mock::dcCalls-calls<=8); // at most one flush per tile band
      for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
        const unsigned u=flip?sx+cw-1-unsigned((x+0.5f)*cw/256):sx+unsigned((x+0.5f)*cw/256);
        const unsigned v=sy+unsigned((y+0.5f)*ch/128);
        const unsigned rgb=Wii::DecodePixel(ram.data()+(v*256+u)*2,format);
        const auto *out=vram.data()+(y*512+x)*4;
        assert(out[0]==rgb>>16&&out[1]==((rgb>>8)&255)&&out[2]==(rgb&255)&&out[3]==0x71);
      }
    }
    unsigned first=Mock::dcBytes;
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,256,format,3,256,128,sx,sy,sx+cw,sy+ch));
    assert(Mock::dcBytes==first); // same source patch even when UV direction changes
  }
  // Equal bytes in a fresh/recycled CPU allocation do not certify a GX flush.
  // Keep the existing bytes equal to the next source but remove that certificate.
  Wii::flushedTextureBytes=0;
  unsigned forcedBefore=Mock::dcBytes;
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,256,2,3,256,128,3,5,66,36));
  assert(Mock::dcBytes-forcedBefore==64*32*2);
  assert(Wii::flushedTextureBytes>=64*32*2);
  // ADD must work with direct textures, packed textures, and palette readers.
  // It must not preload destination pixels or call the CPU combiner.
  Wii::GXSpriteState add;add.add=true;add.primary=0x137fe1;
  const auto addReference=[](uint32_t rgb,uint32_t primary) {
    uint32_t out=0;for(unsigned sh:{0u,8u,16u})out|=std::min(255u,((rgb>>sh)&255)+((primary>>sh)&255))<<sh;return out;
  };
  for(unsigned format=0;format<4;++format) {
    const unsigned bpp=format==3?4:2;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x)for(unsigned i=0;i<bpp;++i)
      ram[(y*128+x)*bpp+i]=uint8_t(x*17+y*31+i*29);
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,format,3,128,64,0,0,128,64,nullptr,nullptr,&add));
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      const uint32_t rgb=addReference(Wii::DecodePixel(ram.data()+((y/2)*128+x/2)*bpp,format),add.primary);
      auto *out=vram.data()+(y*512+x)*4;
      assert(out[0]==rgb>>16&&out[1]==((rgb>>8)&255)&&out[2]==(rgb&255)&&out[3]==0x71);
    }
  }
  unsigned paletteReads=0;
  auto addPalette=[](unsigned x,unsigned y,const void *p)->uint32_t {
    ++*static_cast<unsigned *>(const_cast<void *>(p));return (x*17+y*31)&0xffffff;
  };
  unsigned beforeFence=Mock::flushes;
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,5,3,128,64,0,0,128,64,addPalette,&paletteReads,&add));
  assert(paletteReads==128*64&&Mock::flushes==beforeFence+1); // one readback fence, no preload
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
    const auto rgb=addReference(((x/2)*17+(y/2)*31)&0xffffff,add.primary);
    auto *out=vram.data()+(y*512+x)*4;assert(out[0]==rgb>>16&&out[1]==((rgb>>8)&255)&&out[2]==(rgb&255)&&out[3]==0x71);
  }
  // Mixed state and hardware precision failures reject before guest writes.
  add.coverage=true;assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&add));add.coverage=false;
  Wii::addTested=Wii::addReady=false;Mock::badAdd=true;
  const auto beforeFailure=vram;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&add));
  assert(vram==beforeFailure&&!Wii::addReady);Mock::badAdd=false;Wii::addTested=false;
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&add));
  // Pure doubling reuses native16 and indexed uploads, never bakes pixels.
  Wii::GXSpriteState doubled;doubled.doubleColor=true;
  for(unsigned format=0;format<4;++format) {
    const unsigned bpp=format==3?4:2;
    std::fill(vram.begin(),vram.end(),0xa5);
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,format,3,128,64,0,0,128,64,nullptr,nullptr,&doubled));
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      const uint32_t rgb=Wii::DecodePixel(ram.data()+((y/2)*128+x/2)*bpp,format);
      unsigned expected=0;
      for(unsigned shift:{0u,8u,16u})expected|=std::min(255u,((rgb>>shift)&255)*2)<<shift;
      const auto *p=vram.data()+(y*512+x)*4;
      assert((unsigned(p[0])<<16|unsigned(p[1])<<8|p[2])==expected&&p[3]==0xa5);
    }
  }
  paletteReads=0;beforeFence=Mock::flushes;
  assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,5,3,128,64,128,64,0,0,addPalette,&paletteReads,&doubled));
  assert(paletteReads==128*64&&Mock::flushes==beforeFence+1);
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
    const unsigned rgb=(((127-x/2)*17+(63-y/2)*31)&0xffffff);
    unsigned expected=0;for(unsigned sh:{0u,8u,16u})expected|=std::min(255u,((rgb>>sh)&255)*2)<<sh;
    const auto *p=vram.data()+(y*512+x)*4;
    assert((unsigned(p[0])<<16|unsigned(p[1])<<8|p[2])==expected&&p[3]==0xa5);
  }
  for(unsigned kind=1;kind<5;++kind) {
    auto invalid=doubled;
    if(kind==0)invalid.add=true;if(kind==1)invalid.coverage=true;if(kind==2)invalid.blend=true;
    if(kind==3)invalid.logic=true;if(kind==4)invalid.keepMask=1;
    const auto saved=vram;
    assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&invalid));
    assert(vram==saved);
  }
  const auto savedFeedback=vram;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x04000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&doubled));
  assert(vram==savedFeedback);
  // ADD followed by doubling runs in the same TEV stage, no CPU bake.
  add.doubleColor=true;
  for(unsigned format=0;format<4;++format) {
    const unsigned bpp=format==3?4:2;
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,format,3,128,64,0,0,128,64,nullptr,nullptr,&add));
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      const unsigned rgb=addReference(Wii::DecodePixel(ram.data()+((y/2)*128+x/2)*bpp,format),add.primary);
      unsigned expected=0;for(unsigned sh:{0u,8u,16u})expected|=std::min(255u,((rgb>>sh)&255)*2)<<sh;
      const auto *p=vram.data()+(y*512+x)*4;
      assert((unsigned(p[0])<<16|unsigned(p[1])<<8|p[2])==expected&&p[3]==0xa5);
    }
  }
  Wii::addDoubleTested=Wii::addDoubleReady=false;Mock::badAdd=true;
  const auto beforeCombinedFailure=vram;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&add));
  assert(vram==beforeCombinedFailure&&!Wii::addDoubleReady);
  Mock::badAdd=false;Wii::addDoubleTested=false;
  add.doubleColor=false;
  Wii::doubleTested=Wii::doubleReady=false;Mock::badAdd=true;
  auto beforeDoubleFailure=vram;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,nullptr,nullptr,&doubled));
  assert(vram==beforeDoubleFailure&&!Wii::doubleReady);Mock::badAdd=false;Wii::doubleTested=false;
  // Display and ordinary sprites reset TEV state after ADD.
  assert(sprite());assert(!Mock::customTev);
  // Packed PSP targets retain stencil and apply the RGB mask before packing.
  advanced.logic=true;advanced.logicOp=6;advanced.blend=false;advanced.keepMask=0x00ff00;
  for(unsigned format=0;format<3;++format) {
    const uint16_t oldPixel=uint16_t(Wii::RGBToPSPPixel(0x1289ab,0xc3,format));
    uint8_t old[2]={uint8_t(oldPixel),uint8_t(oldPixel>>8)};
    const unsigned oldRGB=Wii::DecodePixel(old,format);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      auto p=vram.data()+(y*512+x)*2;p[0]=old[0];p[1]=old[1];
    }
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,format,128,64,0,0,128,64,rgba,nullptr,&advanced));
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
      unsigned rgb=oldRGB;
      if((x/2+y/2)&1)rgb=((oldRGB^0x345678)&0xff00ff)|(oldRGB&0xff00);
      const unsigned keep=format==1?0x8000:format==2?0xf000:0;
      const uint16_t expected=uint16_t(Wii::RGBToPSPPixel(rgb,0,format))|(oldPixel&keep);
      auto p=vram.data()+(y*512+x)*2;assert((unsigned(p[0])|(unsigned(p[1])<<8))==expected);
    }
  }
  // Fully protected targets need no GPU work, including packed masks that
  // cover every stored RGB bit without masking unused low channel bits.
  for(unsigned format=0;format<4;++format) {
    advanced.keepMask=format==0?0x00f8fcf8:format==1?0x00f8f8f8:format==2?0x00f0f0f0:0x00ffffff;
    const auto saved=vram;const unsigned fences=Mock::flushes,begins=Mock::begins;
    assert(Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,format,128,64,0,0,128,64,rgba,nullptr,&advanced));
    assert(vram==saved&&Mock::flushes==fences&&Mock::begins==begins);
  }
  advanced.keepMask=0x00ff00;


  // Dependency proof against every logic truth table and framebuffer format,
  // with coverage disabled. The earlier suite covers discarded texels.
  for(unsigned format=0;format<4;++format)for(unsigned op=0;op<16;++op) {
    advanced.logic=true;advanced.logicOp=op;advanced.coverage=false;advanced.keepMask=0;
    auto draw=[&](){return Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,format,128,64,0,0,128,64,rgba,nullptr,&advanced);};
    assert(draw());
    const unsigned bpp=format==3?4:2,oldPixel=Wii::RGBToPSPPixel(0x1289ab,0x71,format);
    uint8_t old[4]={uint8_t(oldPixel),uint8_t(oldPixel>>8),uint8_t(oldPixel>>16),uint8_t(oldPixel>>24)};
    const unsigned oldRGB=Wii::DecodePixel(old,format);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x)memcpy(vram.data()+(y*512+x)*bpp,old,bpp);
    unsigned begins=Mock::begins,fences=Mock::flushes;
    assert(draw());
    const unsigned sourceOnly=(op==0||op==3||op==12||op==15);
    assert(Mock::begins-begins==(op==5?0:sourceOnly?1:2));
    assert(Mock::flushes-fences==(op==5?0:1));
    const unsigned ops[]={GX_LO_CLEAR,GX_LO_AND,GX_LO_REVAND,GX_LO_COPY,GX_LO_INVAND,GX_LO_NOOP,GX_LO_XOR,GX_LO_OR,
      GX_LO_NOR,GX_LO_EQUIV,GX_LO_INV,GX_LO_REVOR,GX_LO_INVCOPY,GX_LO_INVOR,GX_LO_NAND,GX_LO_SET};
    const unsigned rgb=Logic(ops[op],0x345678,oldRGB)&0xffffff;
    unsigned expected=Wii::RGBToPSPPixel(rgb,0x71,format);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x){
      const auto *p=vram.data()+(y*512+x)*bpp;
      for(unsigned byte=0;byte<bpp;++byte)assert(p[byte]==uint8_t(expected>>(byte*8)));
    }
  }
  advanced.logic=true;advanced.logicOp=6;advanced.coverage=true;advanced.keepMask=0x00ff00;
  assert(Wii::advancedPassed==Wii::advancedTested);
  Wii::GetSettings().gxAdvancedSprites=false;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,rgba,nullptr,&advanced));
  Wii::GetSettings().gxAdvancedSprites=true;
  // A preload scratch allocation failure happens before submitting draws or
  // touching guest pixels. Releasing scratch in low-memory mode stays safe.
  Wii::GXReleaseReadbackScratch();Mock::failAllocation=true;
  auto untouched=vram;unsigned beginsBeforeFailure=Mock::begins;
  assert(!Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,3,3,128,64,0,0,128,64,rgba,nullptr,&advanced));
  assert(!Wii::commandsPending&&Mock::begins==beginsBeforeFailure&&vram==untouched);
  assert(sprite());
  // Readback allocation failure must now precede draw submission. Independently
  // verify that recovery still fences already-pending texture reads.
  assert(Wii::readback&&Wii::readbackBytes);
  Wii::commandsPending=true;auto *scratch=Wii::readback;
  assert(!Wii::GXReleaseReadbackScratch()&&Wii::readback==scratch);
  Wii::commandsPending=false;
  Wii::GXReleaseReadbackScratch();Mock::failAllocation=true;
  assert(!sprite()&&!Wii::commandsPending);
  // Explicitly exercise recovery of a previously submitted draw.
  Wii::commandsPending=true;
  unsigned fences=Mock::flushes;
  assert(Wii::EnsureTexture(32));assert(Mock::flushes==fences+1&&!Wii::commandsPending);
  assert(sprite());assert(Wii::GXPresent(0x04000000,512,3,&mode,&xfb));
  assert(Wii::ready&&Wii::rgb565Ready&&Wii::rgb5a3Ready);
  Mock::hang=true;assert(!Wii::GXPresent(0x04000000,512,3,&mode,&xfb));
  assert(!Wii::GXEnabled()&&Mock::aborts==1);
  assert(!Wii::GXPresent(0x04000000,512,3,&mode,&xfb));
  // An explicit menu re-enable rebuilds a failed GX context. It must not
  // remain permanently faulted while reporting GX enabled.
  Mock::hang=false;Wii::SetGXEnabled(true);
  assert(Wii::GXEnabled()&&!Wii::faulted&&!Wii::attempted);
  assert(Wii::GXPresent(0x04000000,512,3,&mode,&xfb)&&Wii::ready);
  Wii::GXShutdown();assert(Wii::flushedTextureBytes==0);
  puts("pass: actual gx bridge with command mock: native self-tests, wrapped scanout, dirty flushes, guards, pending fences and timeout fallback");
}
''')

    if benchmark:
        before=folder/'previous.cpp'
        before.write_text((root/('Wii/tests/GXSpeedReference.cpp' if '--speed-benchmark' in sys.argv else 'Wii/tests/GXPolicyReference.cpp')).read_text().replace('namespace Wii {','namespace PreviousGXPolicy {\nusing namespace Wii;',1).replace('TexturedRectangle','PreviousTexturedRectangle').replace('TestSpriteState','PreviousTestSpriteState'))
        code=test.read_text().replace('#include "Wii/GXRender.cpp"','#include "Wii/GXRender.cpp"\n#include "'+str(before)+'"\n#include <chrono>')
        code=code[:code.index('int main() {')]+r'''
int main(){
 std::vector<uint8_t>ram(32*1024*1024),vram(2*1024*1024);
 auto &map=Wii::addressSpace;map.ram=ram.data();map.ramSize=ram.size();map.vram=vram.data();map.RebuildPages();
 auto read=[](unsigned x,unsigned y,const void*)->uint32_t{return 0xff000000|((x*73+y*19)*0x125689&0xffffff);};
 for(unsigned i=0;i<ram.size();++i)ram[i]=uint8_t(i*13);
 for(unsigned frameFormat=0;frameFormat<4;++frameFormat)for(unsigned mode=0;mode<20;++mode){
  Wii::GXSpriteState state;state.logic=mode<16;state.logicOp=mode<16?mode:3;state.coverage=mode==16||mode==18;
  if(mode==18){state.logic=true;state.logicOp=3;}
  auto invoke=[&](unsigned which){return which?
   Wii::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,mode==19?frameFormat:3,frameFormat,128,64,0,0,128,64,mode==19?nullptr:+read,nullptr,mode==19?nullptr:&state):
   PreviousGXPolicy::GXSprite(0x04000000,512,0,0,256,128,0x08000000,128,mode==19?frameFormat:3,frameFormat,128,64,0,0,128,64,mode==19?nullptr:+read,nullptr,mode==19?nullptr:&state);};
  assert(invoke(0)&&invoke(1));
  for(unsigned round=0;round<11;++round)for(unsigned turn=0;turn<2;++turn){
   const unsigned which=(round+turn)%2;std::fill(vram.begin(),vram.end(),0x71);
   auto start=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<12;++repeat)assert(invoke(which));
   printf("policy,%u,%u,%u,%s,%.6f\n",frameFormat,mode,round,which?"updated":"previous",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
 }
 Wii::GXShutdown();PreviousGXPolicy::GXShutdown();
}
'''
        test.write_text(code)

    exe=folder/'test'
    subprocess.run(['g++']+(['-DWII_GX_FULL_UPLOAD'] if full_upload else [])+(['-std=c++11','-Os'] if benchmark else ['-std=c++11','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all'])+['-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(folder),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(test),'-Wl,--wrap=memalign','-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
