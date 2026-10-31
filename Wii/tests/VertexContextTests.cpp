// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/VertexDecodeContext.h"
#include <cassert>
#include <cstdio>
GPUgstate gstate{};
GPUStateCache gstate_c{};
bool g_DoubleTextureCoordinates=false;
int main() {
  u8 original[512],changed[512];
  gstate_c.uv={1.0f,1.0f,0.0f,0.0f};
  const size_t bytes=Wii::CaptureVertexDecodeContext(original);assert(bytes<=512);
  auto different=[&]() {assert(Wii::CaptureVertexDecodeContext(changed)==bytes);assert(memcmp(original,changed,bytes));};
  auto same=[&]() {assert(Wii::CaptureVertexDecodeContext(changed)==bytes);assert(!memcmp(original,changed,bytes));};
  same();
  gstate.texmapmode=1;different();gstate.texmapmode=0;same();
  gstate_c.uv.uScale=2.0f;different();gstate_c.uv.uScale=1.0f;same();
  gstate_c.uv.vOff=-0.0f;different();gstate_c.uv.vOff=0.0f;same();
  for(unsigned i=0;i<8;++i) {gstate_c.morphWeights[i]=0.5f;different();gstate_c.morphWeights[i]=0.0f;same();}
  for(unsigned i=0;i<96;++i) {gstate.boneMatrix[i]=1.0f;different();gstate.boneMatrix[i]=0.0f;same();}
  gstate_c.vertBounds.minU=3;same();gstate_c.vertBounds.minU=0;same();
  gstate_c.vertBounds.maxV=9;same();gstate_c.vertBounds.maxV=0;same();
  gstate_c.vertexFullAlpha=true;same();gstate_c.vertexFullAlpha=false;same();
  g_DoubleTextureCoordinates=true;different();g_DoubleTextureCoordinates=false;same();
  gstate_c.vertBounds={10,20,30,40};gstate_c.vertexFullAlpha=true;
  Wii::MergeVertexDecodeEffects(KnownVertexBounds{12,8,28,90},false);
  assert(gstate_c.vertBounds.minU==10&&gstate_c.vertBounds.minV==8&&gstate_c.vertBounds.maxU==30&&gstate_c.vertBounds.maxV==90&&!gstate_c.vertexFullAlpha);
  Wii::MergeVertexDecodeEffects(KnownVertexBounds{65535,65535,0,0},true);
  assert(gstate_c.vertBounds.minV==8&&gstate_c.vertBounds.maxV==90&&!gstate_c.vertexFullAlpha);
  puts("PASS: actual decoder context invalidates for UV mode/scale/offset, signed zero, all morph weights/bones, remaster state, and cumulative bounds/alpha replay");
}
