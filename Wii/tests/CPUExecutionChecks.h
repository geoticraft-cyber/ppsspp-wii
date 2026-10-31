// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/tests/CPUPreviousDispatch.h"
#include "Core/HLE/HLE.h"
#include "Wii/InstructionFetch.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cassert>
static unsigned cpuProbeCalls;
static bool cpuProbeCycles=false;
static void CPUProbeSyscall(){++cpuProbeCalls;currentMIPS->r[2]=currentMIPS->r[4]^currentMIPS->r[5];if(cpuProbeCycles)hleEatCycles(13);}
struct CPUObservedState {
  u32 r[32],fi[32],pc,next,fcr31,condition,lo,hi;int down;bool delay;
  void Read(){memcpy(r,currentMIPS->r,sizeof(r));memcpy(fi,currentMIPS->fi,sizeof(fi));pc=currentMIPS->pc;next=currentMIPS->nextPC;fcr31=currentMIPS->fcr31;condition=currentMIPS->fpcond;lo=currentMIPS->lo;hi=currentMIPS->hi;down=currentMIPS->downcount;delay=currentMIPS->inDelaySlot;}
  void Write()const{memcpy(currentMIPS->r,r,sizeof(r));memcpy(currentMIPS->fi,fi,sizeof(fi));currentMIPS->pc=pc;currentMIPS->nextPC=next;currentMIPS->fcr31=fcr31;currentMIPS->fpcond=condition;currentMIPS->lo=lo;currentMIPS->hi=hi;currentMIPS->downcount=down;currentMIPS->inDelaySlot=delay;}
  bool Same(const CPUObservedState &b)const{return !memcmp(r,b.r,sizeof(r))&&!memcmp(fi,b.fi,sizeof(fi))&&pc==b.pc&&next==b.next&&fcr31==b.fcr31&&condition==b.condition&&lo==b.lo&&hi==b.hi&&down==b.down&&delay==b.delay;}
};
static void CPUExecutionChecks(){
  const bool savedLogging=g_Config.bEnableLogging;g_Config.bEnableLogging=false;
  CPUObservedState saved;saved.Read();const unsigned savedDiagnostic=Wii::DiagnosticLevel();Wii::SetDiagnosticLevel(0);
  static const HLEFunction probes[]={{0x76543210,&CPUProbeSyscall,"cpu test probe",'x',"",0,0}};
  RegisterModule("cpu dispatch probe",1,probes);
  const u32 syscall=GetSyscallOp("cpu dispatch probe",0x76543210);
  // Consume boot's pending HLE completion flags before comparing calls. The
  // two paths must start with identical HLE state, as well as CPU registers.
  cpuProbeCycles=false;PreviousCPUInterpret(MIPSOpcode(syscall));saved.Write();
  const u32 memoryBase=0x08840000;u8 backup[128];Memory::Memcpy(backup,memoryBase,sizeof(backup));
  const u32 instructions[]={0,0x8c220000,0xac220004,0x80220008,0x9422000a,0xa422000c,
    0xc4200010,0xe4200014,0x44823000,0x44023000,0x46020800,0x46020801,0x46020802,
    0x46020803,0x46000806,0x46000807,0x46020832,0x45000001,0x45010001,
    0x45020001,0x45030001,0x4442f800,0x44420000,0x44c2f800,0x46800820,0x46000824,0x4600080c,0x4600080d,0x4600080e,0x4600080f,0x24840001,0x308500ff};
  uint32_t seed=0x893265;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned trial=0;trial<120000;++trial){
    CPUObservedState before{};
    for(auto &v:before.r)v=random();before.r[0]=0;before.r[1]=memoryBase;
    for(auto &v:before.fi){const float f=(int(random()%4095)-2047)*0.125f;memcpy(&v,&f,4);}
    before.fi[2]=0x3f800000;before.pc=0x08820000;before.next=0x08820100;
    before.fcr31=random()&0x0181ffff;before.condition=random()&1;before.lo=random();before.hi=random();before.down=200;before.delay=trial&1;
    const u32 op=trial%13?instructions[trial%(sizeof(instructions)/sizeof(instructions[0]))]:syscall;
    cpuProbeCycles=(trial&2)!=0;
    u8 initial[128];for(auto &v:initial)v=random();Memory::Memcpy(memoryBase,initial,sizeof(initial));
    before.Write();const unsigned probeBefore=cpuProbeCalls;
    PreviousCPUInterpret(MIPSOpcode(op));CPUObservedState expected;expected.Read();u8 expectedMemory[128];Memory::Memcpy(expectedMemory,memoryBase,sizeof(expectedMemory));
    assert(cpuProbeCalls-probeBefore==(op==syscall?1u:0u));
    Memory::Memcpy(memoryBase,initial,sizeof(initial));before.Write();const unsigned secondProbe=cpuProbeCalls;
    MIPSInterpret(MIPSOpcode(op));CPUObservedState actual;actual.Read();u8 actualMemory[128];Memory::Memcpy(actualMemory,memoryBase,sizeof(actualMemory));
    if(!expected.Same(actual)||memcmp(expectedMemory,actualMemory,sizeof(actualMemory))) {
      fprintf(stderr,"cpu mismatch trial=%u opcode=%08x pc=%08x/%08x next=%08x/%08x down=%d/%d fcr=%08x/%08x cond=%u/%u delay=%d/%d mem=%d\n",trial,op,expected.pc,actual.pc,expected.next,actual.next,expected.down,actual.down,expected.fcr31,actual.fcr31,expected.condition,actual.condition,expected.delay,actual.delay,int(memcmp(expectedMemory,actualMemory,sizeof(actualMemory))));
      for(unsigned reg=0;reg<32;++reg)if(expected.r[reg]!=actual.r[reg]||expected.fi[reg]!=actual.fi[reg])fprintf(stderr,"reg %u r=%08x/%08x fi=%08x/%08x\n",reg,expected.r[reg],actual.r[reg],expected.fi[reg],actual.fi[reg]);
      assert(false);
    }
    assert(cpuProbeCalls-secondProbe==(op==syscall?1u:0u));
  }
  // Every fetch and transfer sees live edits, including rewritten COP1 opcodes.
  Wii::InstructionFetch fetch;const u32 oldCode=Memory::Read_U32(0x08821000);
  for(u32 opcode:{0xc4200000u,0xc4210000u,0xe4200004u,0xe4210004u}){
    currentMIPS->r[1]=0x08821000;currentMIPS->fi[0]=opcode;
    MIPSInterpret(MIPSOpcode(0xe4200000)); // SWC1 writes the next live opcode.
    assert(fetch.Read(0x08821000)==opcode);
  }
  Memory::Write_U32(oldCode,0x08821000);
  puts("pass: 120000 old/new complete CPU dispatches, memory, COP1 math/transfers/branches, FCR state, syscall delay slots, cycle charges and register clobbers; live opcode edits");
  cpuProbeCycles=false;
  const u32 mixes[6][8]={
    {0x24840001,0x308500ff,0x8c220000,0xac220004,0x80220008,0x9422000a,0x10430001,0x14230001},
    {0x46020800,0x46020801,0x46020802,0x46020803,0x46000806,0x46000807,0x44823000,0x44023000},
    {0xc4200010,0xe4200014,0xc4210018,0xe421001c,0x8c220000,0xac220004,0x24840001,0x308500ff},
    {syscall,syscall,syscall,syscall,0x24840001,0x308500ff,0x10430001,0x14230001},
    {0,0,0x24840001,0,0x308500ff,0,0x10430001,0},
    {0x00432021,0x00432023,0x00432024,0x00432025,0x00432026,0x0043202a,0x00022100,0x00622006}};
  for(unsigned mix=0;mix<6;++mix)for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn){
    const bool updated=(turn^(round&1))!=0;auto fn=updated?MIPSInterpret:PreviousCPUInterpret;
    saved.Write();currentMIPS->r[1]=memoryBase;currentMIPS->fi[1]=0x3f800000;currentMIPS->fi[2]=0x40000000;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<8000000;++i){currentMIPS->r[1]=memoryBase;currentMIPS->pc=0x08820000;currentMIPS->inDelaySlot=false;currentMIPS->downcount=100;fn(MIPSOpcode(mixes[mix][i&7]));}
    printf("host cpu mix %u round %u %s %.3f ms\n",mix,round,updated?"new":"old",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  Memory::Memcpy(memoryBase,backup,sizeof(backup));saved.Write();Wii::SetDiagnosticLevel(savedDiagnostic);g_Config.bEnableLogging=savedLogging;
}
