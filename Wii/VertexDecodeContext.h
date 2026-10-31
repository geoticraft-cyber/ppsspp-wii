// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstring>
#include <algorithm>
#include "GPU/GPUState.h"
#include "Core/HDRemaster.h"
namespace Wii {
// Bytewise keys preserve NaNs and signed zero; no hashes can collide. The
// cumulative bounds/alpha are replayed separately so prior draws do not
// prevent reuse of otherwise identical decoded vertices.
inline size_t CaptureVertexDecodeContext(u8 (&context)[512]) {
  static_assert(sizeof(gstate_c.uv)+sizeof(gstate_c.morphWeights)+sizeof(gstate.boneMatrix)+
    sizeof(u32)+sizeof(bool)<=512,
    "decoder context fits");
  u8 *out=context;
  const u32 uvMode=gstate.getUVGenMode();
  memcpy(out,&uvMode,sizeof(uvMode));out+=sizeof(uvMode);
  memcpy(out,&gstate_c.uv,sizeof(gstate_c.uv));out+=sizeof(gstate_c.uv);
  memcpy(out,gstate_c.morphWeights,sizeof(gstate_c.morphWeights));out+=sizeof(gstate_c.morphWeights);
  memcpy(out,gstate.boneMatrix,sizeof(gstate.boneMatrix));out+=sizeof(gstate.boneMatrix);
  memcpy(out,&g_DoubleTextureCoordinates,sizeof(g_DoubleTextureCoordinates));out+=sizeof(g_DoubleTextureCoordinates);
  return size_t(out-context);
}
inline void MergeVertexDecodeEffects(const KnownVertexBounds &bounds,bool fullAlpha) {
  gstate_c.vertBounds.minU=std::min(gstate_c.vertBounds.minU,bounds.minU);
  gstate_c.vertBounds.minV=std::min(gstate_c.vertBounds.minV,bounds.minV);
  gstate_c.vertBounds.maxU=std::max(gstate_c.vertBounds.maxU,bounds.maxU);
  gstate_c.vertBounds.maxV=std::max(gstate_c.vertBounds.maxV,bounds.maxV);
  gstate_c.vertexFullAlpha=gstate_c.vertexFullAlpha&&fullAlpha;
}
}
