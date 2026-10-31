// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
// Only explicit empty cases in SoftGPU::ExecuteOp. cmdmem is still written;
// renderers read these registers at draw time. Never classify by diff==0:
// repeated PRIM, matrix, CLUT, address and control commands have side effects.
inline bool GECommandStoresStateOnly(unsigned cmd) {
  static const uint32_t mask[8]={
    0xffe50000u,
    0x0040001fu,
    0xf9f830fcu,
    0xffffffffu,
    0x0fff807fu,
    0xff3fffffu,
    0xc87ce240u,
    0x0000583fu,
  };
  return cmd<256 && ((mask[cmd>>5]>>(cmd&31))&1)!=0;
}
}
