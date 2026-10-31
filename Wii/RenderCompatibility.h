// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/Compatibility.h"
#include "GPU/GPU.h"

namespace Wii {
// Keep the saved preference intact. Required offscreen targets may become
// textures later, even if they have never been selected for display.
inline bool SkipSoftwareDraw(const CompatFlags &flags, unsigned reasons, bool skipBufferEffects) {
  return (reasons & SKIPDRAW_SKIPFRAME) ||
    (skipBufferEffects && !flags.RequireBufferedRendering &&
      (reasons & SKIPDRAW_NON_DISPLAYED_FB));
}
}
