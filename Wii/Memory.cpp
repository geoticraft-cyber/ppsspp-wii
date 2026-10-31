// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#if !defined(PPSSPP_WII_PORTABLE_TEST)
#include <gccore.h>
#include <ogc/system.h>
#endif
#include "Wii/AddressSpace.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Common/Log.h"

namespace Wii { AddressSpace addressSpace; }

// Arena reservations outlive game sessions. Keep the one reservation reachable
// after shutdown and retry; never grow MEM2 by another 32 MiB per launch.
static u8 *reservedPSPRAM;
static u8 *reservedVRAM;
namespace Memory {
extern u8 *m_pPhysicalScratchPad, *m_pUncachedScratchPad;
extern u8 *m_pKernelRAM, *m_pPhysicalRAM2, *m_pUncachedRAM2, *m_pKernelRAM2;
extern u8 *m_pPhysicalRAM3, *m_pUncachedRAM3, *m_pKernelRAM3;
extern u8 *m_pPhysicalVRAM1, *m_pPhysicalVRAM2, *m_pPhysicalVRAM3, *m_pPhysicalVRAM4;
extern u8 *m_pUncachedVRAM1, *m_pUncachedVRAM2, *m_pUncachedVRAM3, *m_pUncachedVRAM4;

bool MemoryMap_Setup(u32) {
  // RECOVERED_1_2: target fn 0x8016f688 only accepts the PSP-1000 32 MiB map.
  if (g_MemorySize != RAM_NORMAL_SIZE) {
    ERROR_LOG(BOOT,"Unsupported PSP RAM request: %u bytes; Wii guest map supports only %u bytes",g_MemorySize,RAM_NORMAL_SIZE);
    return false;
  }
  auto &space = Wii::addressSpace;
  if (space.ram)
    return space.ramSize == RAM_NORMAL_SIZE;

  // Binary-proven 1.2 behavior: guest RAM is taken from MEM2's low arena rather
  // than malloc/memalign, keeping the ordinary heap available to PPSSPP/GX.
  if (!reservedPSPRAM) {
#if defined(PPSSPP_WII_PORTABLE_TEST)
    reservedPSPRAM = static_cast<u8 *>(memalign(32, RAM_NORMAL_SIZE));
#else
    reservedPSPRAM = static_cast<u8 *>(SYS_AllocArenaMem2Lo(RAM_NORMAL_SIZE, 32));
#endif
  }
  space.ram = reservedPSPRAM;
  if (!space.ram) {
    WARN_LOG(BOOT, "Cannot reserve contiguous 32 MiB PSP RAM");
    return false;
  }
  INFO_LOG(BOOT, "PSP RAM: reserved %u bytes at %p in MEM2", RAM_NORMAL_SIZE, space.ram);

  // Use otherwise unclaimed MEM2 for the long-lived 2 MiB VRAM allocation.
  // Reserve once, like PSP RAM, and preserve a 4 MiB arena margin. This removes
  // VRAM from the fragmented malloc heap without changing guest addresses.
#if !defined(PPSSPP_WII_PORTABLE_TEST)
  if (!reservedVRAM && SYS_GetArena2Size() >= VRAM_SIZE + 4u * 1024 * 1024 + 32)
    reservedVRAM = static_cast<u8 *>(SYS_AllocArenaMem2Lo(VRAM_SIZE, 32));
#endif
  space.vram = reservedVRAM ? reservedVRAM : static_cast<u8 *>(memalign(32, VRAM_SIZE));
  INFO_LOG(BOOT,"PSP VRAM: %u bytes in %s",VRAM_SIZE,reservedVRAM?"MEM2 arena (reusable)":"malloc heap (fallback)");
  space.scratch = static_cast<u8 *>(memalign(32, SCRATCHPAD_SIZE));
  const bool needNull=PSP_CoreParameter().compat.flags().NullPageValid;
  space.nullPage=needNull?static_cast<u8 *>(memalign(32,0x10000)):nullptr;
  if (!space.vram || !space.scratch || (needNull&&!space.nullPage)) {
    if (space.vram != reservedVRAM) free(space.vram);
    free(space.scratch);free(space.nullPage);space.nullPage=nullptr;
    // Arena2 low allocations cannot be individually freed.  Leave RAM reserved;
    // a failed setup is fatal to this emulation session, matching the target's
    // one-shot memory lifetime.
    space.vram = nullptr;
    space.scratch = nullptr;
    space.ram = nullptr;
    space.ramSize = 0;
    return false;
  }

  space.ramSize = RAM_NORMAL_SIZE;
  space.RebuildPages();
  memset(space.ram, 0, RAM_NORMAL_SIZE);
  memset(space.vram, 0, VRAM_SIZE);
  memset(space.scratch, 0, SCRATCHPAD_SIZE);
  if(space.nullPage)memset(space.nullPage,0,0x10000);
  base = space.ram;  // lifecycle marker only, never a guest address base on Wii.
  m_pPhysicalRAM = m_pUncachedRAM = m_pKernelRAM = space.ram;
  m_pPhysicalRAM2 = m_pUncachedRAM2 = m_pKernelRAM2 = space.ram + 31 * 1024 * 1024;
  m_pPhysicalRAM3 = m_pUncachedRAM3 = m_pKernelRAM3 = nullptr;
  m_pPhysicalScratchPad = m_pUncachedScratchPad = space.scratch;
  m_pPhysicalVRAM1 = m_pPhysicalVRAM2 = m_pPhysicalVRAM3 = m_pPhysicalVRAM4 = space.vram;
  m_pUncachedVRAM1 = m_pUncachedVRAM2 = m_pUncachedVRAM3 = m_pUncachedVRAM4 = space.vram;
  return true;
}

void MemoryMap_Shutdown(u32) {
  auto &space = Wii::addressSpace;
  // Arena reservations are reused, while heap fallbacks and scratch are freed.
  if (space.vram != reservedVRAM) free(space.vram);
  free(space.scratch);free(space.nullPage);
  space = {};
  base = nullptr;
  m_pPhysicalRAM = m_pUncachedRAM = m_pKernelRAM = nullptr;
  m_pPhysicalRAM2 = m_pUncachedRAM2 = m_pKernelRAM2 = nullptr;
  m_pPhysicalRAM3 = m_pUncachedRAM3 = m_pKernelRAM3 = nullptr;
  m_pPhysicalScratchPad = m_pUncachedScratchPad = nullptr;
  m_pPhysicalVRAM1 = m_pPhysicalVRAM2 = m_pPhysicalVRAM3 = m_pPhysicalVRAM4 = nullptr;
  m_pUncachedVRAM1 = m_pUncachedVRAM2 = m_pUncachedVRAM3 = m_pUncachedVRAM4 = nullptr;
}
}
