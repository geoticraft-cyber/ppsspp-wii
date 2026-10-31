// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
struct GraphicsWork {
  uint64_t spriteCalls=0,gxAttempts=0,gxAccepted=0,paletteAttempts=0;
  uint64_t gxUploadBytes=0,gxUploadSaved=0,cpuTexelHits=0,cpuTexelMisses=0;
  uint64_t gxTevAdd=0,gxTevDouble=0,gxNative16Sprites=0;
  uint64_t gxBaked=0,gxBlend=0,gxLogic=0,gxCoverage=0,gxMasked=0;
  uint64_t verticesDecoded=0,vertexRequests=0,verticesTransformed=0,vertexCopyAvoided=0;
  uint64_t gxReadbackBytes=0,gxSourceSkippedPixels=0,gxDisplayConvertedPixels=0;
  unsigned gxSourceMemoBytes=0;
  uint64_t preparedLightingDraws=0,triangleSlices=0;
  uint64_t preparedPixelSlices=0,referencePixelSlices=0,alphaPixelSlices=0,stencilPixelSlices=0,fogPixelSlices=0,packedPixelSlices=0;
  unsigned decodeScratchPeak=0,vertexCachePeak=0,decodeRetries=0;
  uint64_t flatPixels=0,texturePixels=0,triangles=0,pixelStateFallbacks=0;
};
// Updated by primitive submission on the guest/GPU thread, read between runs.
inline GraphicsWork &GetGraphicsWork() {static GraphicsWork stats;return stats;}
}
