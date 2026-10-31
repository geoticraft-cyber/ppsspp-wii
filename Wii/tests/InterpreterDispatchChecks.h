// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/MIPS/MIPSTables.h"
#include "Wii/ScalarInterpreter.h"
#include "Wii/MemoryInterpreter.h"
#include "Wii/Diagnostics.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cassert>

// Previous interpreter dispatch, retained only as a regression/benchmark oracle.
static void PreviousDispatch(MIPSOpcode op) {
  Wii::TraceInstruction(currentMIPS->pc,op.encoding,currentMIPS->r[29],currentMIPS->r[31]);
  const unsigned kind=op.encoding>>26;
  if(kind>=32) {
    if(Wii::InterpretMemory(op.encoding,currentMIPS->r,currentMIPS->pc))return;
  } else if(kind==0) {
    if(Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc))return;
    if(Wii::InterpretHiLo(op.encoding,currentMIPS->r,currentMIPS->lo,currentMIPS->hi,currentMIPS->pc))return;
  } else if((kind>=9&&kind<=15)||kind==31) {
    if(Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc))return;
  }
  auto interpret=MIPSGetInterpretFunc(op);assert(interpret);interpret(op);
}
static void InterpreterDispatchChecks() {
  uint32_t seed=0x942fd22;
  const auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  const unsigned regimm[]={0,1,2,3,16,17,18,19};
  uint32_t before[32],expected[32];
  for(unsigned trial=0;trial<160000;++trial) {
    for(auto &reg:before)reg=random();before[0]=0;
    const unsigned rs=random()%32,rt=random()%32,rd=random()%32;
    const unsigned kinds[]={1,2,3,4,5,6,7,20,21,22,23,0};
    const unsigned kind=kinds[trial%12];
    uint32_t op=kind<<26|rs<<21|rt<<16|(random()&65535);
    if(kind==0)op=rs<<21|rd<<11|(random()&1?8:9);
    if(kind==1)op=(op&~(31u<<16))|regimm[random()%8]<<16;
    const uint32_t pc=random()&~3u,next=random();
    const int down=int(random()%256);
    const bool delay=((kind>=4&&kind<=7)||(kind>=20&&kind<=23))&&(random()&1);
    memcpy(currentMIPS->r,before,sizeof(before));currentMIPS->pc=pc;
    currentMIPS->nextPC=next;currentMIPS->inDelaySlot=delay;currentMIPS->downcount=down;
    PreviousDispatch(MIPSOpcode(op));
    memcpy(expected,currentMIPS->r,sizeof(expected));
    const uint32_t refPC=currentMIPS->pc,refNext=currentMIPS->nextPC;
    const int refDown=currentMIPS->downcount;const bool refDelay=currentMIPS->inDelaySlot;
    memcpy(currentMIPS->r,before,sizeof(before));currentMIPS->pc=pc;
    currentMIPS->nextPC=next;currentMIPS->inDelaySlot=delay;currentMIPS->downcount=down;
    MIPSInterpret(MIPSOpcode(op));
    assert(!memcmp(expected,currentMIPS->r,sizeof(expected))&&currentMIPS->pc==refPC&&
      currentMIPS->nextPC==refNext&&currentMIPS->downcount==refDown&&currentMIPS->inDelaySlot==refDelay);
  }
  puts("PASS: 160000 branch/jump dispatch comparisons: taken/untaken, likely annul, link, register alias and address wrap");
  // Fixed instruction mix; no memory or rendered frames. Host timing only.
  const uint32_t ops[]={0x1422fffc,0x10430010,0x5422fffc,0x08010000,0x0c010000,0x03e00008,0x0320f809,0x04310008,0x24840001,0x308500ff};
  const auto run=[&](void(*dispatch)(MIPSOpcode)) {
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<2000000;++i) {
      currentMIPS->pc=0x08804000;currentMIPS->inDelaySlot=false;currentMIPS->downcount=100;
      dispatch(MIPSOpcode(ops[i%10]));
    }
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  };
  const double beforeMs=run(PreviousDispatch),afterMs=run(MIPSInterpret);
  printf("HOST dispatch mix: previous %.3f ms, current %.3f ms (not Wii FPS)\n",beforeMs,afterMs);
  currentMIPS->inDelaySlot=false;
}
