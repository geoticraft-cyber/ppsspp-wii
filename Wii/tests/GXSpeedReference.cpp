// SPDX-License-Identifier: GPL-2.0-or-later
#include <gccore.h>
#include <malloc.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include "Wii/GXBridge.h"
#include "Wii/AllocationDiagnostics.h"
#include "Wii/GXLayout.h"
#include "Wii/GXUpload.h"
#include "Wii/GXDirtyUpload.h"
#include "Wii/DisplaySourceMemo.h"
#include "Wii/GXSpriteUpload.h"
#include "Wii/GXReadback.h"
#include "Wii/GraphicsWork.h"
#include "Wii/GuestSurface.h"
#include "Wii/Presentation.h"
#include "Wii/Pixel.h"
#include "Wii/Runtime.h"
#include "Wii/GuestCopy.h"
#include "Wii/Settings.h"
#include "Core/MemMap.h"
#include "Common/Log.h"
namespace Wii {
static bool enabled=true,attempted=false,ready=false,faulted=false;
static bool spriteTested=false,spriteReady=false;
static bool rgb565Tested=false,rgb565Ready=false;
static bool addTested=false,addReady=false;
static bool rgb5a3Tested=false,rgb5a3Ready=false;
static void *fifo;
static uint8_t *texture,*readback;
static unsigned readbackBytes,textureBytes;
// CPU-byte equality permits reuse only after this prefix reached GX memory.
static unsigned flushedTextureBytes;
static DisplaySourceMemo displaySource;
static bool presentTextureValid=false,commandsPending=false;
static constexpr unsigned fifoBytes=256*1024,bufferBytes=512*272*4;
void SetGXEnabled(bool value) {
  // A timeout remains fail-closed during gameplay. An explicit menu enable
  // must rebuild the failed context rather than leave a permanently dead GX.
  if(value&&faulted) {GXShutdown();faulted=false;}
  enabled=value;
}
bool GXEnabled() { return enabled; }
const char *GXStatus() { return faulted?"fault / cpu":!enabled?"off / cpu":ready?"ready":attempted?"unavailable / cpu":"waiting"; }
static void VertexPipeline(bool textured) {
  GX_SetNumTexGens(textured?1:0);GX_SetNumTevStages(1);
  GX_SetTevOrder(GX_TEVSTAGE0,textured?GX_TEXCOORD0:GX_TEXCOORDNULL,textured?GX_TEXMAP0:GX_TEXMAP_NULL,GX_COLOR0A0);
  GX_SetTevOp(GX_TEVSTAGE0,textured?GX_REPLACE:GX_PASSCLR);
  GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
  GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
  GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
  if(textured) {
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
  }
}
static void Setup(unsigned width,unsigned height,bool textured) {
  commandsPending=true;
  GX_SetViewport(0,0,width,height,0,1);
  GX_SetScissor(0,0,width,height);
  Mtx44 projection;guOrtho(projection,0,float(height),0,float(width),0,1);
  GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
  Mtx model;guMtxIdentity(model);GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);
  GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);
  GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
  GX_SetZCompLoc(GX_TRUE);GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);
  GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
  GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
  GX_SetFog(GX_FOG_NONE,0,1,0,1,GXColor{0,0,0,255});
  GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
  VertexPipeline(textured);
}
static void Quad(float x,float y,float w,float h,uint32_t rgb,bool tex) {
  auto vertex=[&](float px,float py,float u,float v) {
    GX_Position3f32(px,py,0);GX_Color4u8(rgb>>16,rgb>>8,rgb,255);
    if(tex)GX_TexCoord2f32(u,v);
  };
  GX_Begin(GX_QUADS,GX_VTXFMT0,4);
  vertex(x,y,0,0);vertex(x+w,y,1,0);vertex(x+w,y+h,1,1);vertex(x,y+h,0,1);GX_End();
}
static bool WaitGX(const char *stage) {
  DiagnosticPhase(stage);
  RuntimeProgress(0,4);
  const u16 token=u16(GX_GetDrawSync()+1);
  GX_SetDrawSync(token);GX_Flush();const u64 began=gettime();
  while(GX_GetDrawSync()!=token) {
    if(ticks_to_millisecs(gettime()-began)>1500) {
      AllocationFailure("gx-timeout",0,token,0,0,0,GX_GetDrawSync());
      DiagnosticPhase("gx-timeout-fallback");
      ERROR_LOG(G3D,"GX timeout in %s; aborting frame and falling back to CPU",stage);
      faulted=true;enabled=ready=false;GX_AbortFrame();commandsPending=false;return false;
    }
    usleep(100);
  }
  commandsPending=false;
  DiagnosticPhase("gx-complete");return true;
}
static bool EnsureTexture(unsigned bytes) {
  // An allocation/readback failure may have returned with draws outstanding.
  // Never let the CPU overwrite or free bytes still referenced by GX.
  if(commandsPending && !WaitGX("texture reuse"))return false;
  DiagnosticPhase("gx-texture-allocation");
  bytes=(bytes+31)&~31u;
  if(!bytes||bytes>bufferBytes)return false;
  if(texture && textureBytes>=bytes)return true;
  // Every preceding GX operation has reached WaitGX before buffer reuse.
  free(texture);texture=nullptr;textureBytes=0;flushedTextureBytes=0;presentTextureValid=false;
  texture=static_cast<uint8_t *>(memalign(32,bytes));
  if(!texture)return false;
  textureBytes=bytes;return true;
}
static bool EnsureReadback(unsigned bytes) {
  DiagnosticPhase("gx-readback-allocation");
  bytes=(bytes+31)&~31u;
  if(bytes>bufferBytes)return false;
  if(readback && readbackBytes>=bytes)return true;
  // EFB readback is synchronous: the previous scratch buffer is no longer in use.
  free(readback);readback=nullptr;readbackBytes=0;
  readback=static_cast<uint8_t *>(memalign(32,bytes));
  if(!readback)return false;
  readbackBytes=bytes;
  memset(readback,0,bytes);DCFlushRange(readback,bytes);
  return true;
}
unsigned GXReleaseReadbackScratch() {
  if(!readback||commandsPending)return 0;
  const unsigned bytes=readbackBytes;
  free(readback);readback=nullptr;readbackBytes=0;return bytes;
}
static bool ReadColor(unsigned width,unsigned height) {
  if(!EnsureReadback(width*height*4))return false;
  GX_SetTexCopySrc(0,0,width,height);GX_SetTexCopyDst(width,height,GX_TF_RGBA8,GX_FALSE);
  GX_SetCopyFilter(GX_FALSE,nullptr,GX_FALSE,nullptr);
  GX_CopyTex(readback,GX_FALSE);GX_PixModeSync();if(!WaitGX("texture readback"))return false;
  DCInvalidateRange(readback,width*height*4);
  GetGraphicsWork().gxReadbackBytes+=uint64_t(width)*height*4;return true;
}
static bool Init() {
  DiagnosticPhase("gx-init");
  if(!enabled||faulted)return false;
  if(attempted)return ready;
  attempted=true;
  const unsigned commandBytes=GetSettings().lowMemory?64*1024:fifoBytes;
  fifo=memalign(32,commandBytes);
  if(!fifo) {
    free(fifo);free(texture);fifo=nullptr;texture=nullptr;
    WARN_LOG(G3D,"GX allocation failed; using software display");return false;
  }
  memset(fifo,0,commandBytes);
  GX_Init(fifo,commandBytes);
  GX_SetCopyClear(GXColor{0,0,0,255},0xffffff);
  // Verify real EFB drawing, copy tiling, and color precision before using GX.
  Setup(512,272,false);Quad(0,0,4,4,0x1256a3,false);if(!ReadColor(4,4))return false;
  ready=UnpackGXPixel(readback,4,0,0)==0x1256a3 && UnpackGXPixel(readback,4,3,3)==0x1256a3;
  INFO_LOG(G3D,"GX EFB color/copy self-test: %s",ready?"PASS":"FAIL; software fallback");
  GXReleaseReadbackScratch();
  return ready;
}
static bool TestRGB565() {
  if(rgb565Tested)return rgb565Ready;
  rgb565Tested=true;presentTextureValid=false;
  if(!Init())return false;
  if(!EnsureTexture(32))return false;
  const uint32_t test=0x1256a3;
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)PackGX565Pixel(texture,4,x,y,test);
  DCFlushRange(texture,4*4*2);GX_InvalidateTexAll();Setup(4,4,true);
  GXTexObj tex;GX_InitTexObj(&tex,texture,4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  Quad(0,0,4,4,0xffffff,true);
  if(ReadColor(4,4)) {
    const uint32_t expected=ExpandGX565(test);
    rgb565Ready=UnpackGXPixel(readback,4,0,0)==expected && UnpackGXPixel(readback,4,3,3)==expected;
  }
  INFO_LOG(G3D,"GX RGB565 upload self-test: %s",rgb565Ready?"PASS":"FAIL; RGBA8 fallback");
  GXReleaseReadbackScratch();
  return rgb565Ready;
}
static bool TestRGB5A3() {
  if(rgb5a3Tested)return rgb5a3Ready;
  rgb5a3Tested=true;presentTextureValid=false;
  if(!Init()||!EnsureTexture(32))return false;
  const uint16_t pixels[]={0xfc00,0x83e0,0x801f,0x7fff};
  const uint32_t expected[]={0xff0000,0x00ff00,0x0000ff,0xffffff};
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
    unsigned i=GX565TileOffset(x,y,4);texture[i]=pixels[x]>>8;texture[i+1]=pixels[x];
  }
  DCFlushRange(texture,32);GX_InvalidateTexAll();Setup(4,4,true);
  GXTexObj tex;GX_InitTexObj(&tex,texture,4,4,GX_TF_RGB5A3,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  Quad(0,0,4,4,0xffffff,true);
  if(ReadColor(4,4)) {
    rgb5a3Ready=true;
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
      if(UnpackGXPixel(readback,4,x,y)!=expected[x])rgb5a3Ready=false;
  }
  INFO_LOG(G3D,"GX RGB5A3 upload self-test: %s",rgb5a3Ready?"PASS":"FAIL; RGBA8 fallback");
  GXReleaseReadbackScratch();return rgb5a3Ready;
}
static void TexturedRectangle(unsigned tw,unsigned th,unsigned w,unsigned h,float u0,float v0,float u1,float v1,unsigned format=GX_TF_RGBA8,const GXSpriteState *state=nullptr,bool flush=true,uint8_t *pixels=nullptr) {
  presentTextureValid=false;
  if(!pixels)pixels=texture;
  if(flush) {
    const unsigned bytes=tw*th*(format==GX_TF_RGBA8?4:2);
    DCFlushRange(pixels,bytes);
    if(pixels==texture)flushedTextureBytes=std::max(flushedTextureBytes,bytes);
  }
  GX_InvalidateTexAll();
  Setup(512,272,true);
  GXTexObj tex;GX_InitTexObj(&tex,pixels,tw,th,format,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  if(state) {
    if(state->add) {
      // Exact saturating RGB addition; no divide/interpolation approximation.
      // GX TEV computes D + (A - B) * C. Use ONE for C to add the
      // texture to the raster color; ZERO would silently output raster only.
      GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_TEXC,GX_CC_ZERO,GX_CC_ONE,GX_CC_RASC);
      GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    if(state->doubleColor) {
      if(!state->add)GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC);
      GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_2,GX_TRUE,GX_TEVPREV);
    }
    if(state->coverage)GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
    if(state->blend)GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
    if(state->logic) {
      const unsigned operations[]={GX_LO_CLEAR,GX_LO_AND,GX_LO_REVAND,GX_LO_COPY,GX_LO_INVAND,GX_LO_NOOP,GX_LO_XOR,GX_LO_OR,
        GX_LO_NOR,GX_LO_EQUIV,GX_LO_INV,GX_LO_REVOR,GX_LO_INVCOPY,GX_LO_INVOR,GX_LO_NAND,GX_LO_SET};
      GX_SetBlendMode(GX_BM_LOGIC,GX_BL_ONE,GX_BL_ZERO,operations[state->logicOp]);
    }
  }
  GX_Begin(GX_QUADS,GX_VTXFMT0,4);
  const uint32_t color=state&&state->add?state->primary:0xffffff;
  auto vertex=[&](float x,float y,float u,float v) {GX_Position3f32(x,y,0);GX_Color4u8(color>>16,color>>8,color,255);GX_TexCoord2f32(u,v);};
  vertex(0,0,u0/tw,v0/th);vertex(w,0,u1/tw,v0/th);
  vertex(w,h,u1/tw,v1/th);vertex(0,h,u0/tw,v1/th);GX_End();
}
// Exercise every input value in every channel with independent constants,
// including saturation boundaries, before trusting the hardware ADD path.
static bool addDoubleTested=false,addDoubleReady=false;
static bool TestAddCombiner(bool doubled=false) {
  bool &tested=doubled?addDoubleTested:addTested;
  bool &passed=doubled?addDoubleReady:addReady;
  if(tested)return passed;
  tested=true;
  if(!EnsureTexture(16*16*4))return false;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
    const unsigned v=y*16+x;
    PackGXPixel(texture,16,x,y,v*0x010101);
  }
  GXSpriteState state;state.add=true;state.doubleColor=doubled;state.primary=0x017fff;
  TexturedRectangle(16,16,16,16,0,0,16,16,GX_TF_RGBA8,&state);
  if(!ReadColor(16,16))return false;
  passed=true;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
    const unsigned v=y*16+x;
    const unsigned scale=doubled?2:1;
    const uint32_t expected=(std::min(255u,(v+1)*scale)<<16)|(std::min(255u,(v+127)*scale)<<8)|255;
    if(UnpackGXPixel(readback,16,x,y)!=expected)passed=false;
  }
  INFO_LOG(G3D,"GX saturating texture ADD%s self-test: %s",doubled?" + doubling":"",passed?"PASS":"FAIL; CPU fallback");
  return passed;
}
// Check every 8-bit channel value and saturation boundary on the real EFB.
static bool doubleTested=false,doubleReady=false;
static bool TestDoubleCombiner() {
  if(doubleTested)return doubleReady;
  doubleTested=true;
  if(!EnsureTexture(16*16*4))return false;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
    const unsigned v=y*16+x;
    PackGXPixel(texture,16,x,y,(v<<16)|((255-v)<<8)|((v^85)));
  }
  GXSpriteState state;state.doubleColor=true;
  TexturedRectangle(16,16,16,16,0,0,16,16,GX_TF_RGBA8,&state);
  if(!ReadColor(16,16))return false;
  doubleReady=true;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
    const unsigned v=y*16+x;
    const uint32_t expected=(std::min(255u,v*2)<<16)|(std::min(255u,(255-v)*2)<<8)|std::min(255u,(v^85)*2);
    if(UnpackGXPixel(readback,16,x,y)!=expected)doubleReady=false;
  }
  INFO_LOG(G3D,"GX saturating texture doubling self-test: %s",doubleReady?"PASS":"FAIL; CPU fallback");
  return doubleReady;
}
// Validate advanced operations against real EFB results, independently of the
// guest framebuffer. Cache each tested state; never repeat this per sprite.
static uint32_t advancedTested=0,advancedPassed=0;
static uint32_t LogicReference(unsigned op,uint32_t source,uint32_t dest) {
  switch(op) {
    case 0:return 0;case 1:return source&dest;case 2:return source&~dest;case 3:return source;
    case 4:return ~source&dest;case 5:return dest;case 6:return source^dest;case 7:return source|dest;
    case 8:return ~(source|dest);case 9:return ~(source^dest);case 10:return ~dest;case 11:return source|~dest;
    case 12:return ~source;case 13:return ~source|dest;case 14:return ~(source&dest);default:return 0xffffff;
  }
}
static bool TestSpriteState(const GXSpriteState &requested) {
  const unsigned test=requested.logic?requested.logicOp:requested.blend?16:17;
  const uint32_t bit=1u<<test;
  if(advancedTested&bit)return (advancedPassed&bit)!=0;
  advancedTested|=bit;
  if(!EnsureTexture(64)||!EnsureReadback(64))return false;
  // Exercise the same scratch-as-preload ordering as gameplay, including
  // copying back into that scratch after GX has sampled it. Fail closed on
  // hardware that does not reproduce the expected destination/state results.
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
    PackGXPixel(readback,4,x,y,0x1289ab);
  TexturedRectangle(4,4,4,4,0,0,4,4,GX_TF_RGBA8,nullptr,true,readback);
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
    PackGXPixel(texture,4,x,y,0x345678,((x+y)&1)?(requested.blend?128:255):0);
  GXSpriteState state=requested;state.coverage=true;state.keepMask=0;
  TexturedRectangle(4,4,4,4,0,0,4,4,GX_TF_RGBA8,&state);
  if(!ReadColor(4,4))return false;
  bool passed=true;unsigned maxError=0;
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
    uint32_t expected=0x1289ab;
    if((x+y)&1) {
      expected=requested.logic?LogicReference(requested.logicOp,0x345678,0x1289ab)&0xffffff:0x345678;
      if(requested.blend) {
        expected=0;
        for(unsigned shift=0;shift<24;shift+=8)
          expected|=(((((0x345678u>>shift)&255)*128+((0x1289abu>>shift)&255)*127)/255)<<shift);
      }
    }
    const uint32_t actual=UnpackGXPixel(readback,4,x,y);
    for(unsigned shift=0;shift<24;shift+=8) {
      const unsigned error=unsigned(std::abs(int((actual>>shift)&255)-int((expected>>shift)&255)));
      maxError=std::max(maxError,error);
      if(error>(requested.blend?1u:0u))passed=false;
    }
  }
  if(passed)advancedPassed|=bit;
  INFO_LOG(G3D,"GX sprite-state self-test: state=%u result=%s max-channel-error=%u%s",test,passed?"PASS":"FAIL; CPU fallback",maxError,requested.blend?" (experimental blend precision)":"");
  return passed;
}
bool GXSprite(uint32_t frame,unsigned stride,int x,int y,unsigned w,unsigned h,
  uint32_t source,unsigned sourceStride,unsigned sourceFormat,unsigned frameFormat,unsigned tw,unsigned th,float u0,float v0,float u1,float v1,GXTexelReader reader,const void *context,const GXSpriteState *state) {
  // Readback is amortized over a large sprite, never used for tiny glyphs/tiles.
  if(!enabled||!GetSettings().gxSprites||sourceFormat>7||(!reader&&sourceFormat>3)||frameFormat>3||x<0||y<0||!w||!h||w>512||h>272||w*h<32768||
    tw<4||th<4||tw>512||th>512||(tw&3)||(th&3)||sourceStride<tw||uint64_t(x)+w>stride||
    !std::isfinite(u0)||!std::isfinite(v0)||!std::isfinite(u1)||!std::isfinite(v1)||
    u0<0||u0>tw||u1<0||u1>tw||v0<0||v0>th||v1<0||v1>th)return false;
  if(state) {
    // The hardware probe formerly allowed a one-channel rounding error.
    // Blending must use the reference pixel pipeline until exact equivalence
    // is established, including PSP factors, rounding and stencil semantics.
    if(state->blend)return false;
    if(state->add||state->doubleColor) {
      if(state->coverage||state->blend||state->logic||state->keepMask||state->primary>0xffffff)return false;
    } else if(!GetSettings().gxAdvancedSprites||!reader||state->logicOp>15||(state->blend&&state->logic))return false;
  }
  // Destination preload + synchronous readback only make sense for enlarged
  // sprites. Baking once per input texel must replace substantial pixel work.

  const unsigned sourceBpp=sourceFormat==3?4:2,frameBpp=frameFormat==3?4:2;
  const unsigned sourceBits[]={16,16,16,32,4,8,16,32};
  const uint64_t size=(uint64_t(sourceStride)*th*sourceBits[sourceFormat]+7)/8;
  if(size>0xffffffffULL || !addressSpace.IsRangeValid(source,size) ||
    !ValidSurface(frame,stride,x,y,w,h,frameBpp))return false;
  const unsigned sourceBytes=unsigned(size);
  const uint8_t *src=addressSpace.Contiguous(source,sourceBytes);
  if(!src)return false;
  for(unsigned row=0;row<h;++row) {
    uint32_t addr=frame+((y+row)*stride+x)*frameBpp;
    if(!Memory::IsValidRange(addr,w*frameBpp))return false;
    const uint8_t *dst=addressSpace.Contiguous(addr,w*frameBpp);
    if(!dst)return false;
    // Feedback textures need sequential software semantics, not a GPU snapshot.
    uintptr_t d=reinterpret_cast<uintptr_t>(dst),t=reinterpret_cast<uintptr_t>(src);
    if(d<t+sourceBytes&&t<d+w*frameBpp)return false;
  }
  const unsigned cropX=unsigned(std::floor(std::min(u0,u1))),cropY=unsigned(std::floor(std::min(v0,v1)));
  const unsigned cropW=unsigned(std::ceil(std::max(u0,u1)))-cropX,cropH=unsigned(std::ceil(std::max(v0,v1)))-cropY;
  if(!cropW||!cropH)return false;
  // No depth/stencil side effects are accepted by the GX sprite caller.
  // A fully protected RGB target needs neither upload, preload nor readback.
  if(state&&(GXColorWriteIsNoop(frameFormat,state->keepMask)||
             (state->logic&&state->logicOp==5)))return true;
  tw=(cropW+3)&~3u;th=(cropH+3)&~3u;
  if(tw*th>w*h||(state&&!state->add&&!state->doubleColor&&uint64_t(tw)*th*4>uint64_t(w)*h))return false;
  u0-=cropX;u1-=cropX;v0-=cropY;v1-=cropY;
  presentTextureValid=false;
  if(!Init())return false;
  if(!EnsureTexture(64))return false;
  if(!spriteTested) {
    spriteTested=true;
    for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)PackGXPixel(texture,4,i,j,0x102030+i*0x310507+j*0x09132b);
    TexturedRectangle(4,4,16,16,0,0,4,4);if(!ReadColor(16,16))return false;spriteReady=true;
    for(unsigned j=0;j<16;++j)for(unsigned i=0;i<16;++i)
      if(UnpackGXPixel(readback,16,i,j)!=(0x102030+(i/4)*0x310507+(j/4)*0x09132b))spriteReady=false;
    INFO_LOG(G3D,"GX nearest sprite self-test: %s",spriteReady?"PASS":"FAIL; software fallback");
  }
  if(!spriteReady)return false;
  if(state && !(state->add?TestAddCombiner(state->doubleColor):state->doubleColor?TestDoubleCombiner():TestSpriteState(*state)))return false;
  // The display path already validates native texture expansion. Reuse it for
  // opaque sprites: GX expands the packed channels, halving upload traffic.
  // Alpha/stencil still comes from the destination, exactly as in RGBA8 mode.
  const bool native16=(!state||state->add||state->doubleColor)&&!reader&&sourceFormat<3 && (sourceFormat==0?TestRGB565():TestRGB5A3());
  const unsigned uploadFormat=native16?(sourceFormat==0?GX_TF_RGB565:GX_TF_RGB5A3):GX_TF_RGBA8;
  // CLEAR, COPY, INVCOPY and SET have no destination input. Coverage still
  // requires a preload: discarded texels must retain the original framebuffer.
  const bool logicReadsDestination=state&&state->logic&&
    state->logicOp!=0&&state->logicOp!=3&&state->logicOp!=12&&state->logicOp!=15;
  const bool preload=state&&(state->coverage||state->blend||logicReadsDestination);
  // Reserve the source before submitting any preload commands. The readback
  // scratch doubles as the preload texture: GX consumes it before the ordered
  // EFB copy writes it. Source conversion can now overlap the preload draw,
  // without overwriting a live texture or adding a second completion fence.
  const unsigned rw=(w+3)&~3u,rh=(h+3)&~3u;
  if(!EnsureTexture(tw*th*(native16?2:4)))return false;
  if(!EnsureReadback(rw*rh*4))return false;
  if(preload) {
    const unsigned dw=(w+3)&~3u,dh=(h+3)&~3u;
    if(!EnsureReadback(dw*dh*4))return false;
    for(unsigned j=0;j<dh;++j) {
      const uint8_t *row=addressSpace.Contiguous(frame+((y+std::min(j,h-1))*stride+x)*frameBpp,w*frameBpp);
      for(unsigned i=0;i<dw;++i)
        PackGXPixel(readback,dw,i,j,DecodePixel(row+std::min(i,w-1)*frameBpp,frameFormat));
    }
    TexturedRectangle(dw,dh,w,h,0,0,w,h,GX_TF_RGBA8,nullptr,true,readback);
  }
  const unsigned sourceUploadBytes=tw*th*(native16?2:4);
  // A newly allocated buffer can contain equal CPU bytes without having been
  // flushed for GX. Force its first complete upload instead of trusting them.
  const bool compare=GetSettings().gxTextureCache&&flushedTextureBytes>=sourceUploadBytes;
  bool flushed=false;
  if(!native16) {
    const auto read=[&](unsigned i,unsigned j)->uint32_t {
      const unsigned sx=cropX+std::min(i,cropW-1),sy=cropY+std::min(j,cropH-1);
      const uint32_t color=reader?reader(sx,sy,context):DecodePixel(src+(sy*sourceStride+sx)*sourceBpp,sourceFormat);
      return (color&0xffffff)|(state?(color&0xff000000):0xff000000);
    };
    const unsigned changed=UploadSpritePatch(texture,tw,th,read,compare,
      [](uint8_t *p,unsigned n){DCFlushRange(p,n);});
    auto &work=GetGraphicsWork();work.gxUploadBytes+=changed;work.gxUploadSaved+=tw*th*4-changed;
    flushed=true;
  } else {
    const auto read=[&](unsigned i,unsigned j)->uint16_t {
      const auto *p=src+((cropY+std::min(j,cropH-1))*sourceStride+cropX+std::min(i,cropW-1))*2;
      return uint16_t(p[0])|uint16_t(p[1])<<8;
    };
    const unsigned changed=UploadNative16Patch(texture,tw,th,read,sourceFormat,compare,
      [](uint8_t *p,unsigned n){DCFlushRange(p,n);});
    auto &work=GetGraphicsWork();work.gxUploadBytes+=changed;work.gxUploadSaved+=tw*th*2-changed;
    ++work.gxNative16Sprites;flushed=true;
  }
  flushedTextureBytes=std::max(flushedTextureBytes,sourceUploadBytes);
  TexturedRectangle(tw,th,w,h,u0,v0,u1,v1,uploadFormat,state,!flushed);
  if(!ReadColor(rw,rh))return false;
  // Synchronize before returning to the CPU; PSP alpha is stencil, not GX alpha.
  for(unsigned j=0;j<h;++j) {
    uint8_t *dst=Memory::GetPointerUnchecked(frame+((y+j)*stride+x)*frameBpp);
    CopyGXColorRow(dst,readback,rw,j,w,frameFormat,state?state->keepMask:0);
  }
  if(state&&state->add)++GetGraphicsWork().gxTevAdd;
  if(state&&state->doubleColor)++GetGraphicsWork().gxTevDouble;
  static unsigned reports=0;
  if(reports++<4)INFO_LOG(G3D,"GX sprite rendered: %ux%u from %ux%u; upload=%s bytes=%u",w,h,tw,th,native16?"native16":"RGBA8",tw*th*(native16?2:4));
  return true;
}
bool GXPresent(uint32_t frame,unsigned stride,unsigned format,const void *videoMode,void *xfb) {
  if(!enabled||format>3||stride<480)return false;
  const unsigned bpp=format==3?4:2;
  if(!videoMode || !xfb || !ValidSurface(frame,stride,0,0,480,272,bpp))return false;
  if(!Init())return false;
  alignas(32) uint8_t scanlines[4][480*4];
  const bool native16=format<3 && (format==0?TestRGB565():TestRGB5A3());
  const unsigned uploadBpp=native16?2:4;
  if(!EnsureTexture(480*272*uploadBpp))return false;
  if(GetSettings().lowMemory)GXReleaseReadbackScratch();
  unsigned changedBytes=0;
  const unsigned memoBefore=displaySource.Format();
  const bool memoReady=displaySource.Configure(format,GetSettings().lowMemory);
  // A different source format invalidates the shadow even if it happens to
  // require the same number of bytes. Never inspect uninitialized snapshots.
  const bool reusable=presentTextureValid&&memoBefore==format&&memoReady;
  bool reuseTexture=presentTextureValid;
  presentTextureValid=false; // partial/failing uploads cannot certify a texture
  GetGraphicsWork().gxSourceMemoBytes=displaySource.Bytes();
#if defined(WII_GX_FULL_UPLOAD)
  reuseTexture=false;
#endif
  for(unsigned y=0;y<272;y+=4) {
    const uint8_t *rows[4];
    for(unsigned row=0;row<4;++row) {
      uint32_t address;
      if(!SurfaceRow(frame,stride,0,y+row,480,bpp,address))return false;
      rows[row]=addressSpace.Contiguous(address,480*bpp);
      if(!rows[row]) {
        if(!CopyFromGuest(scanlines[row],address,480*bpp))return false;
        rows[row]=scanlines[row];
      }
    }
    if(reuseTexture&&reusable&&displaySource.Matches(y,rows)) {
      GetGraphicsWork().gxSourceSkippedPixels+=480*4;
      continue;
    }
    const bool hadTileMemo=reuseTexture&&reusable&&displaySource.HasBand(y);
    bool tileMemo=hadTileMemo;
    unsigned skipped=0,dirtyTiles=0;
    const unsigned changed=UploadDirtyBandReuse(texture,480,y,rows,format,native16,
      !reuseTexture,[](uint8_t *p,unsigned bytes) { DCFlushRange(p,bytes); },
      [&](unsigned x) {
        if(!tileMemo)return false;
        if(displaySource.MatchesTile(y,x,rows)) {skipped+=16;return true;}
        // Retire a broadly animated band instead of maintaining a full raw
        // shadow indefinitely. Resume admission only after it stabilizes.
        if(++dirtyTiles>30) {displaySource.Invalidate(y);tileMemo=false;return false;}
        displaySource.StoreTile(y,x,rows);
        return false;
      });
    GetGraphicsWork().gxDisplayConvertedPixels+=480*4-skipped;
    GetGraphicsWork().gxSourceSkippedPixels+=skipped;
    changedBytes+=changed;
    if(memoReady&&!hadTileMemo) {
      // Admit only stable bands; sparse edits can then update individual tiles.
      if(!changed)displaySource.Store(y,rows);
      else displaySource.Invalidate(y);
    }
  }
  presentTextureValid=true;
  flushedTextureBytes=std::max(flushedTextureBytes,480*272*uploadBpp);
  // A static framebuffer already matches the GX texture. Invalidate only
  // after a CPU upload changed its contents.
  if(changedBytes)GX_InvalidateTexAll();
  const GXRModeObj *mode=static_cast<const GXRModeObj *>(videoMode);
  const auto rect=DisplayRect(mode->fbWidth,mode->efbHeight,GetSettings().aspect!=0);
  const bool bars=rect.left||rect.top||rect.width!=mode->fbWidth||rect.height!=mode->efbHeight;
  Setup(mode->fbWidth,mode->efbHeight,!bars);
  if(bars) {
    Quad(0,0,mode->fbWidth,mode->efbHeight,0,false);
    VertexPipeline(true);
  }
  GXTexObj tex;GX_InitTexObj(&tex,texture,480,272,native16?(format==0?GX_TF_RGB565:GX_TF_RGB5A3):GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  if(GetSettings().gxLinear) {
    GX_InitTexObjLOD(&tex,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    GX_LoadTexObj(&tex,GX_TEXMAP0);
  }
  Quad(rect.left,rect.top,rect.width,rect.height,0xffffff,true);
  GX_SetDispCopyFrame2Field(mode->copy_interlaced);
  GX_SetCopyClamp(GX_CLAMP_TOP|GX_CLAMP_BOTTOM);
  GX_SetDispCopyGamma(GX_GM_1_0);
  GX_SetDispCopySrc(0,0,mode->fbWidth,mode->efbHeight);
  GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);
  const unsigned copiedHeight=GX_SetDispCopyYScale(GX_GetYScaleFactor(mode->efbHeight,mode->xfbHeight));
  if(!copiedHeight||copiedHeight>unsigned(mode->xfbHeight)+2)return false;
  GX_SetCopyFilter(mode->aa,mode->sample_pattern,GX_TRUE,mode->vfilter);
  GX_SetFieldMode(mode->field_rendering,mode->viHeight==2*mode->efbHeight);
  GX_CopyDisp(xfb,GX_TRUE);if(!WaitGX("display copy"))return false;
  static bool reported=false;if(!reported) { INFO_LOG(G3D,"GX framebuffer scanout active (exact dirty tiles, first upload %u bytes)",changedBytes);reported=true; }
  return true;
}
void GXShutdown() {
  displaySource.Reset();GetGraphicsWork().gxSourceMemoBytes=0;
  advancedTested=advancedPassed=0;addTested=addReady=false;addDoubleTested=addDoubleReady=false;
  if(commandsPending || ready)WaitGX("shutdown");
  free(fifo);free(texture);free(readback);fifo=nullptr;texture=readback=nullptr;readbackBytes=textureBytes=0;flushedTextureBytes=0;presentTextureValid=false;ready=attempted=false;spriteReady=spriteTested=false;doubleTested=doubleReady=false;rgb565Ready=rgb565Tested=false;rgb5a3Ready=rgb5a3Tested=false;
}
}
