// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace Wii {
// No heap queries or normal logging from an allocation failure. These are the
// last periodic observations, not a claim about the heap at the instant of OOM.
void AllocationSnapshot(uint32_t pc, unsigned heapUsed, unsigned heapFree,
  unsigned arena1, unsigned arena2);
void DiagnosticPhase(const char *literal);
bool OpenAllocationLog(const char *path);
void CloseAllocationLog();
void SetAllocationLogging(bool enabled);
void AllocationFailure(const char *kind, size_t requested, size_t detail,
  size_t alignment, uintptr_t caller, int error, uintptr_t address=0,
  unsigned guestFree=0, unsigned guestLargest=0);
unsigned AllocationFailureCount();
unsigned AllocationUnwrittenCount();
}
