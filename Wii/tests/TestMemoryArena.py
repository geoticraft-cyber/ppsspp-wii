#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run actual Wii/Memory.cpp with mocked libogc arena calls (not hardware)."""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'Wii/Memory.cpp').read_text()
pointers=sorted(set(re.findall(r'\bm_p\w+',source)))
with tempfile.TemporaryDirectory(prefix='arena-regression-',dir=Path.cwd()) as folder:
    folder=Path(folder)
    (folder/'ogc').mkdir()
    (folder/'gccore.h').write_text('#pragma once\n')
    (folder/'ogc/system.h').write_text('#pragma once\n#include <cstddef>\nunsigned SYS_GetArena2Size();\nvoid *SYS_AllocArenaMem2Lo(unsigned,unsigned);\n')
    cpp=folder/'arena.cpp'
    cpp.write_text('''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "Common/Log.h"
#include "Core/MemMap.h"
#include "Core/System.h"
CoreParameter &PSP_CoreParameter(){static CoreParameter param;return param;}
#include "Wii/AddressSpace.h"
static unsigned remaining,used,calls;
static bool failNull;
extern "C" void *__real_memalign(size_t,size_t);
extern "C" void *__wrap_memalign(size_t alignment,size_t size) {
 return failNull&&size==0x10000?nullptr:__real_memalign(alignment,size);
}
static unsigned char arena[64*1024*1024] __attribute__((aligned(32)));
unsigned SYS_GetArena2Size() {return remaining;}
void *SYS_AllocArenaMem2Lo(unsigned size,unsigned alignment) {
  assert(alignment==32);assert(size<=remaining);
  void *p=arena+used;used+=size;remaining-=size;++calls;return p;
}
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char*,...) {}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return false;}
namespace Memory {
 u32 g_MemorySize=RAM_NORMAL_SIZE;u8 *base=nullptr;
 bool MemoryMap_Setup(u32);void MemoryMap_Shutdown(u32);
'''+'\n'.join(' u8 *'+name+'=nullptr;' for name in pointers)+'''
}
int main(int argc,char **argv) {
  const bool high=argc>1 && !strcmp(argv[1],"high");
  remaining=Memory::RAM_NORMAL_SIZE+(high?16:4)*1024*1024;
  unsigned char *vram=nullptr;
  for(int i=0;i<4;++i) {
    const bool nullValid=(i&1)!=0;
    const_cast<CompatFlags&>(PSP_CoreParameter().compat.flags()).NullPageValid=nullValid;
    assert(Memory::MemoryMap_Setup(0));
    assert(Wii::addressSpace.ram==arena);
    assert(bool(Wii::addressSpace.nullPage)==nullValid);
    if(nullValid){assert(Wii::addressSpace.nullPage[0]==0&&Wii::addressSpace.nullPage[0xffff]==0);Wii::addressSpace.nullPage[0xffff]=123;}
    assert(Wii::addressSpace.ramSize==Memory::RAM_NORMAL_SIZE);
    if(high) {
      assert(Wii::addressSpace.vram==arena+Memory::RAM_NORMAL_SIZE);
      if(i)assert(vram==Wii::addressSpace.vram);
      assert(calls==2);
    } else {
      assert(Wii::addressSpace.vram!=arena+Memory::RAM_NORMAL_SIZE);assert(calls==1);
    }
    assert(Wii::addressSpace.vram[0]==0);
    assert(Wii::addressSpace.vram[Memory::VRAM_SIZE-1]==0);
    vram=Wii::addressSpace.vram;vram[0]=123;vram[Memory::VRAM_SIZE-1]=42;
    assert(Memory::MemoryMap_Setup(0)); // idempotent live setup
    Memory::MemoryMap_Shutdown(0);
    assert(!Wii::addressSpace.ram&&!Wii::addressSpace.vram&&!Wii::addressSpace.nullPage&&!Memory::base);
  }
  const_cast<CompatFlags&>(PSP_CoreParameter().compat.flags()).NullPageValid=true;
  failNull=true;assert(!Memory::MemoryMap_Setup(0));assert(!Wii::addressSpace.ram&&!Wii::addressSpace.nullPage);
  failNull=false;assert(Memory::MemoryMap_Setup(0));assert(Wii::addressSpace.nullPage[0xffff]==0);Memory::MemoryMap_Shutdown(0);
  Memory::g_MemorySize=64*1024*1024;assert(!Memory::MemoryMap_Setup(0));
  printf("PASS: actual Memory.cpp %s, VRAM zeroing, aliases cleared, alternating null-page titles, allocation failure/retry, no arena growth, unsupported map rejected\\n",high?"MEM2 reservation":"heap fallback");
}
''')
    exe=folder/'arena'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(folder),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),'-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,--wrap=memalign',str(root/'Core/Compatibility.cpp'),str(root/'Wii/Memory.cpp'),str(cpp),'-o',str(exe)],check=True)
    for mode in ['high','low']:subprocess.run([str(exe.resolve()),mode],check=True)
