// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/HLE/HLE.h"
#include <vector>
#include <cassert>
#include <chrono>
#include <cstdio>
static void ImportIndexChecks() {
  // Storage survives until the process exits, like production export tables.
  static std::vector<HLEFunction> exports(2049);
  for(unsigned i=0;i<2048;++i)exports[i]={0x10000000u+i*97u,nullptr,"test export",'i',"",0,0};
  exports[2048]=exports[71];
  RegisterModule("perf2 index fixture",int(exports.size()),exports.data());
  const int index=GetModuleIndex("perf2 index fixture");assert(index>=0);
  for(unsigned i=0;i<2048;++i)assert(GetFuncIndex(index,exports[i].ID)==int(i));
  assert(GetFuncIndex(index,0xffffffff)==-1&&GetFuncIndex(-1,0)==-1);
  assert(GetFuncIndex(100000,0)==-1&&GetModuleIndex("absent perf2 module")==-1);
  RegisterModule("perf2 index fixture",0,nullptr);
  assert(GetModuleIndex("perf2 index fixture")==index);
  assert(!GetSyscallFuncPointer(MIPSOpcode((unsigned(index+2)<<18)|(0xfffu<<6)|12u)));
  volatile unsigned sum=0;
  const auto run=[&](bool indexed) {
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<200000;++i) {
      const unsigned wanted=(i*7919)%2048;int found=-1;
      if(indexed)found=GetFuncIndex(index,exports[wanted].ID);
      else for(unsigned j=0;j<exports.size();++j)if(exports[j].ID==exports[wanted].ID){found=int(j);break;}
      sum+=unsigned(found);
    }
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  };
  const double previous=run(false),current=run(true);
  printf("pass: import indexes preserve module/function numbers, duplicate-first behavior, missing exports and invalid module bounds\n");
  printf("host import lookup: linear %.3f ms indexed %.3f ms checksum %u (synthetic 2049-export module)\n",previous,current,unsigned(sum));
}
