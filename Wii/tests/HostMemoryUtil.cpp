// SPDX-License-Identifier: GPL-2.0-or-later
// Allocator for the native test harness only. The product uses Wii/MemoryUtil.cpp.
#include <cstdlib>
#include <malloc.h>
#include "Common/MemoryUtil.h"
void *AllocateAlignedMemory(size_t n, size_t a) { return memalign(a, n); }
void FreeAlignedMemory(void *p) { free(p); }
void *AllocateMemoryPages(size_t n, uint32_t) { return memalign(32, n); }
void FreeMemoryPages(void *p, size_t) { free(p); }
