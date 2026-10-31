// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/AddressSpace.h"
namespace Wii {
// Reject overflow before reducing to a 32-bit guest address. Validation is per
// visible row: PSP stride padding need not be mapped, and VRAM may wrap.
inline bool SurfaceRow(uint32_t base, unsigned stride, unsigned x, unsigned y,
    unsigned width, unsigned bpp, uint32_t &address) {
  if (!width || (bpp != 2 && bpp != 4) || uint64_t(x) + width > stride) return false;
  const uint64_t offset = (uint64_t(y) * stride + x) * bpp;
  if (offset + base > 0xffffffffULL) return false;
  address = uint32_t(base + offset);
  return addressSpace.IsRangeValid(address,size_t(width) * bpp);
}
inline bool ValidSurface(uint32_t base, unsigned stride, unsigned x, unsigned y,
    unsigned width, unsigned height, unsigned bpp) {
  if (!height || uint64_t(y) + height > 0xffffffffULL) return false;
  for (unsigned row = 0; row < height; ++row) {
    uint32_t address;
    if (!SurfaceRow(base,stride,x,y+row,width,bpp,address)) return false;
  }
  return true;
}
}
