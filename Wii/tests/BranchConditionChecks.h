// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/tests/InterpreterDispatchChecks.h"
#include "Wii/tests/CPUExecutionChecks.h"
static void BranchConditionChecks() {
  CPUObservedState saved;saved.Read();
  const u32 values[]={0,1,2,0x7fffffff,0x80000000,0x80000001,0xfffffffe,0xffffffff};
  const u32 kinds[]={4,5,6,7,20,21,22,23};
  const u32 pcs[]={0,0x08820000,0xfffffff8,0xfffffffc};
  const u32 immediates[]={0,1,0x7fff,0x8000,0xffff};
  unsigned cases=0;
  for(u32 kind:kinds)for(u32 lhs:values)for(u32 rhs:values)
  for(u32 pc:pcs)for(u32 immediate:immediates)for(unsigned delay=0;delay<2;++delay)
  for(int down:{0,1,100}) {
    CPUObservedState expected;
    const MIPSOpcode op(kind<<26|3u<<21|4u<<16|immediate);
    for(unsigned which=0;which<2;++which) {
      saved.Write();currentMIPS->r[0]=0;currentMIPS->r[3]=lhs;currentMIPS->r[4]=rhs;
      currentMIPS->pc=pc;currentMIPS->nextPC=0x89abcdef;currentMIPS->inDelaySlot=delay;currentMIPS->downcount=down;
      (which?MIPSInterpret:PreviousDispatch)(op);
      CPUObservedState result;result.Read();
      if(!which)expected=result;else assert(expected.Same(result));
    }
    ++cases;
  }
  saved.Write();printf("pass: %u branch edge-state comparisons, signed extremes, equality, likely annul, delay slots, tick boundaries and address wrap\n",cases);
}
