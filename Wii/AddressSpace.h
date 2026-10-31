// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace Wii {
// Separate backing stores avoid a 256 MB virtual reservation and duplicate VRAM.
struct AddressSpace {
  uint8_t *ram = nullptr;
  uint8_t *vram = nullptr;
  uint8_t *scratch = nullptr;
  uint8_t *nullPage = nullptr;
  size_t ramSize = 0;

  static constexpr size_t MainRamBytes=0x02000000;
  size_t RamBytes() const {return ramSize<MainRamBytes?ramSize:MainRamBytes;}

  // 1 MiB software pages: 4 KiB on Wii, shared by cached/kernel aliases.
  // Scratchpad and partial RAM pages retain the bounded reference path.
  uint8_t *pages[1024] = {};
  bool pagesReady = false;
  void RebuildPages() {
    for (auto &page : pages) page = nullptr;
    if (ram) for (size_t i = 0; i < RamBytes() / 0x100000 && i < 32; ++i)
      pages[0x80 + i] = ram + i * 0x100000;
    if (vram) for (unsigned i = 0; i < 8; ++i)
      pages[0x40 + i] = vram + (i & 1) * 0x100000;
    pagesReady = true;
  }
  // Guest-contiguous length can cross VRAM mirrors; host Span cannot.
  size_t ValidSpan(uint32_t address) const {
    address &= 0x3fffffff;
    if (nullPage && address < 0x10000) return 0x10000-address;
    if (ram && address >= 0x08000000 && address - 0x08000000 < RamBytes())
      return RamBytes() - (address - 0x08000000);
    if (vram && address >= 0x04000000 && address < 0x04800000)
      return 0x04800000 - address;
    if (scratch && address >= 0x10000 && address < 0x14000)
      return 0x14000 - address;
    return 0;
  }
  bool IsRangeValid(uint32_t address, size_t bytes) const {
    return ValidSpan(address) && bytes <= ValidSpan(address) &&
      uint64_t(address) + bytes <= 0x100000000ULL;
  }
  uint8_t *Contiguous(uint32_t address, size_t bytes) const {
    return bytes <= Span(address) ? Translate(address) : nullptr;
  }
  size_t Span(uint32_t address) const {
    address&=0x3fffffff;
    if(nullPage && address<0x10000)return 0x10000-address;
    if(address>=0x08000000&&address-0x08000000<RamBytes())return ram?RamBytes()-(address-0x08000000):0;
    if(address>=0x04000000&&address<0x04800000)return vram?0x200000-((address-0x04000000)&0x1fffff):0;
    if(address>=0x00010000&&address<0x00014000)return scratch?0x14000-address:0;
    return 0;
  }
  uint8_t *Translate(uint32_t address) const {
    address &= 0x3fffffff;
#if !defined(WII_REFERENCE_ADDRESS_TRANSLATION)
    if (pagesReady) {
      uint8_t *page = pages[address >> 20];
      if (page) return page + (address & 0xfffff);
    }
#endif
    if (nullPage && address < 0x10000) return nullPage + address;
    if (address >= 0x08000000 && address - 0x08000000 < RamBytes())
      return ram ? ram + (address - 0x08000000) : nullptr;
    if (address >= 0x04000000 && address < 0x04800000)
      return vram ? vram + ((address - 0x04000000) & 0x001fffff) : nullptr;
    if (address >= 0x00010000 && address < 0x00014000)
      return scratch ? scratch + (address - 0x00010000) : nullptr;
    return nullptr;
  }
};
extern AddressSpace addressSpace;
}
