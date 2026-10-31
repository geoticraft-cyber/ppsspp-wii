// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <malloc.h>
#include "Common/MemoryUtil.h"

void *AllocateAlignedMemory(size_t size, size_t alignment) { return memalign(alignment, size); }
void FreeAlignedMemory(void *ptr) { free(ptr); }
void *AllocateMemoryPages(size_t size, uint32_t) { return memalign(32, size); }
void FreeMemoryPages(void *ptr, size_t) { free(ptr); }
