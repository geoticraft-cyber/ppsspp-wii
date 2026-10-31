// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/CoreTiming.h"
#include "Core/Core.h"
#include "Core/MIPS/MIPSTables.h"
#include "Wii/Diagnostics.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static void InterpreterBoundaryChecks() {
  const u32 base=0x08820000;
  const u32 savedF0=currentMIPS->fi[0];
  u32 regs[32];memcpy(regs,currentMIPS->r,sizeof(regs));
  const u32 pc=currentMIPS->pc,next=currentMIPS->nextPC;
  const bool delay=currentMIPS->inDelaySlot;
  const int down=currentMIPS->downcount,slice=CoreTiming::slicelength;
  const s64 timer=CoreTiming::globalTimer;
  const auto savedCore=coreState;const unsigned diagnostic=Wii::DiagnosticLevel();
  u8 backup[64];Memory::Memcpy(backup,base,sizeof(backup));
  for(unsigned traced=0;traced<2;++traced)for(unsigned ticks=0;ticks<2;++ticks) {
    Wii::SetDiagnosticLevel(traced?2:0);
    const u64 start=CoreTiming::GetTicks();
    CoreTiming::globalTimer=start;CoreTiming::slicelength=10000;currentMIPS->downcount=10000;
    currentMIPS->pc=base;currentMIPS->inDelaySlot=false;currentMIPS->r[8]=0;
    Memory::Write_U32(0x3c081234,base);Memory::Write_U32(0x3508abcd,base+4);
    Memory::Write_U32(0,base+8);coreState=CORE_RUNNING;
    MIPSInterpret_RunUntil(start+ticks);
    assert(currentMIPS->pc==base+4*(ticks+1));
    assert(currentMIPS->r[8]==(ticks?0x1234abcdu:0x12340000u));
    assert(CoreTiming::GetTicks()==start+ticks+1);
  }
  Wii::SetDiagnosticLevel(0);
  // A constant load in a taken branch's delay slot must not consume the
  // instruction after that slot. Its second half is deliberately skipped.
  const u64 start=CoreTiming::GetTicks();
  CoreTiming::globalTimer=start;CoreTiming::slicelength=10000;currentMIPS->downcount=10000;
  currentMIPS->pc=base;currentMIPS->inDelaySlot=false;currentMIPS->r[8]=0;
  Memory::Write_U32(0x10000003,base);Memory::Write_U32(0x3c081234,base+4);
  Memory::Write_U32(0x3508abcd,base+8);Memory::Write_U32(0,base+16);
  coreState=CORE_RUNNING;MIPSInterpret_RunUntil(start+1);
  assert(currentMIPS->pc==base+16&&currentMIPS->r[8]==0x12340000&&!currentMIPS->inDelaySlot);
  // NOP and raw floating transfers retain every tick/trace boundary.
  for(unsigned traced=0;traced<2;++traced)for(unsigned limit=0;limit<3;++limit) {
    Wii::SetDiagnosticLevel(traced?2:0);
    const u64 transferStart=CoreTiming::GetTicks();
    CoreTiming::globalTimer=transferStart;CoreTiming::slicelength=10000;currentMIPS->downcount=10000;
    currentMIPS->pc=base;currentMIPS->inDelaySlot=false;currentMIPS->r[1]=base+32;currentMIPS->fi[0]=0x11111111;
    Memory::Write_U32(0,base);Memory::Write_U32(0xc4200000,base+4);Memory::Write_U32(0xe4200004,base+8);
    Memory::Write_U32(0x7fc12345,base+32);Memory::Write_U32(0xaabbccdd,base+36);
    coreState=CORE_RUNNING;MIPSInterpret_RunUntil(transferStart+limit);
    assert(currentMIPS->pc==base+(limit+1)*4&&CoreTiming::GetTicks()==transferStart+limit+1);
    assert(currentMIPS->fi[0]==(limit?0x7fc12345u:0x11111111u));
    assert(Memory::Read_U32(base+36)==(limit==2?0x7fc12345u:0xaabbccddu));
  }
  // Completing a taken branch's NOP slot must not execute the skipped load.
  Wii::SetDiagnosticLevel(0);
  const u64 nopStart=CoreTiming::GetTicks();CoreTiming::globalTimer=nopStart;
  CoreTiming::slicelength=10000;currentMIPS->downcount=10000;currentMIPS->pc=base;
  currentMIPS->inDelaySlot=false;currentMIPS->r[1]=base+32;currentMIPS->fi[0]=0x11111111;
  Memory::Write_U32(0x10000003,base);Memory::Write_U32(0,base+4);Memory::Write_U32(0xc4200000,base+8);
  Memory::Write_U32(0,base+16);coreState=CORE_RUNNING;MIPSInterpret_RunUntil(nopStart+1);
  assert(currentMIPS->pc==base+16&&!currentMIPS->inDelaySlot&&currentMIPS->fi[0]==0x11111111&&CoreTiming::GetTicks()==nopStart+2);
  currentMIPS->fi[0]=savedF0;
  Memory::Memcpy(base,backup,sizeof(backup));
  memcpy(currentMIPS->r,regs,sizeof(regs));currentMIPS->pc=pc;currentMIPS->nextPC=next;
  currentMIPS->inDelaySlot=delay;currentMIPS->downcount=down;
  CoreTiming::globalTimer=timer;CoreTiming::slicelength=slice;coreState=savedCore;
  Wii::SetDiagnosticLevel(diagnostic);
  puts("pass: actual interpreter loop preserves constant-pair, NOP and COP1 memory tick/trace boundaries and taken-branch delay slots");
}
