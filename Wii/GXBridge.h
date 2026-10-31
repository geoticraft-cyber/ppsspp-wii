#pragma once
#include <cstdint>
namespace Wii {
void SetGXEnabled(bool enabled);
bool GXEnabled();
const char *GXStatus();
bool GXPresent(uint32_t frame,unsigned stride,unsigned format,const void *videoMode,void *xfb);
unsigned GXReleaseReadbackScratch();

void GXShutdown();
}
namespace Wii {
using GXTexelReader=uint32_t (*)(unsigned,unsigned,const void *);
struct GXSpriteState {
  bool coverage=false,blend=false,logic=false;
  bool doubleColor=false; // exact saturated RGB * 2, optionally after ADD; alpha unchanged
  bool add=false; // exact RGB texture + primary; excludes destination/test state
  uint32_t primary=0; // RGB constant, not guest ABGR
  unsigned logicOp=3;
  uint32_t keepMask=0; // PSP ABGR channel mask; stencil always preserved
};
// With state, reader returns 0xAARRGGBB; alpha also carries baked test coverage.
// Destination-dependent operations preload guest pixels into the EFB.
// Guest alpha/stencil is always preserved.
bool GXSprite(uint32_t frame,unsigned stride,int x,int y,unsigned w,unsigned h,
  uint32_t source,unsigned sourceStride,unsigned sourceFormat,unsigned frameFormat,unsigned tw,unsigned th,float u0,float v0,float u1,float v1,GXTexelReader reader=nullptr,const void *context=nullptr,const GXSpriteState *state=nullptr);
}
