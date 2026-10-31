#pragma once
#include <cassert>
#include <cstring>
#include <chrono>
#include "Wii/ScalarInterpreter.h"
#include "Wii/MemoryInterpreter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
static void ScalarChecks() {
  uint32_t saved[32],pc=mipsr4k.pc;memcpy(saved,mipsr4k.r,sizeof(saved));
  uint32_t rng=0x78312f01;
  auto random=[&] {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
  const unsigned functions[]={0,2,3,4,6,7,10,11,22,23,33,35,36,37,38,39,42,43};
  for(int i=0;i<50000;++i) {
    uint32_t initial[32];for(auto &v:initial)v=random();initial[0]=0;
    unsigned kind=random()%(7+sizeof(functions)/sizeof(functions[0])),rs=random()%32,rt=random()%32,rd=random()%32;
    uint32_t op;
    if(kind<7)op=((9+kind)<<26)|(rs<<21)|(rt<<16)|(random()&65535);
    else {
      unsigned fn=functions[kind-7],sa=0;
      if(fn<=3) {rs=(fn==2)?random()%2:0;sa=random()%32;}
      if(fn==6)sa=random()%2;
      if(fn==22||fn==23) {rt=0;if(i%4==0)initial[rs]=i%8?0:0xffffffffu;initial[0]=0;}
      op=(rs<<21)|(rt<<16)|(rd<<11)|(sa<<6)|fn;
    }
    if((op>>26)==15)op&=~(31u<<21);
    memcpy(mipsr4k.r,initial,sizeof(initial));mipsr4k.pc=0x08804000;
    auto instruction=MIPSGetInterpretFunc(MIPSOpcode(op));assert(instruction);
    instruction(MIPSOpcode(op));
    uint32_t fast[32],fastPC=0x08804000;memcpy(fast,initial,sizeof(fast));
    assert(Wii::InterpretScalar(op,fast,fastPC));
    assert(!memcmp(fast,mipsr4k.r,sizeof(fast))&&fastPC==mipsr4k.pc);
  }
  for(unsigned i=0;i<10000;++i) {
    uint32_t initial[32];for(auto &v:initial)v=random();initial[0]=0;
    unsigned rt=random()%32,rd=random()%32,sa=(i&1)?16:24;
    const uint32_t op=(31u<<26)|(rt<<16)|(rd<<11)|(sa<<6)|32;
    memcpy(mipsr4k.r,initial,sizeof(initial));mipsr4k.pc=0x08804000;
    MIPSGetInterpretFunc(MIPSOpcode(op))(MIPSOpcode(op));
    uint32_t fast[32],fastPC=0x08804000;memcpy(fast,initial,sizeof(fast));
    assert(Wii::InterpretScalar(op,fast,fastPC));
    assert(!memcmp(fast,mipsr4k.r,sizeof(fast))&&fastPC==mipsr4k.pc);
  }
  printf("PASS: 10000 SEB/SEH interpreter differential cases\n");
  const u32 savedLo=mipsr4k.lo,savedHi=mipsr4k.hi;
  for(unsigned i=0;i<20000;++i) {
    uint32_t initial[32];for(auto &v:initial)v=random();initial[0]=0;
    const unsigned fnList[]={16,17,18,19,24,25};const unsigned fn=fnList[i%6];
    unsigned rs=random()%32,rt=random()%32,rd=random()%32;
    if(fn==16||fn==18)rs=rt=0;
    if(fn==17||fn==19)rt=rd=0;
    if(fn==24||fn==25)rd=0;
    uint32_t lo=random(),hi=random(),fastPC=0x08804000,fast[32];
    const u32 op=rs<<21|rt<<16|rd<<11|fn;
    memcpy(mipsr4k.r,initial,sizeof(initial));mipsr4k.lo=lo;mipsr4k.hi=hi;mipsr4k.pc=fastPC;
    MIPSGetInterpretFunc(MIPSOpcode(op))(MIPSOpcode(op));memcpy(fast,initial,sizeof(fast));
    assert(Wii::InterpretHiLo(op,fast,lo,hi,fastPC));
    assert(!memcmp(fast,mipsr4k.r,sizeof(fast))&&fastPC==mipsr4k.pc&&lo==mipsr4k.lo&&hi==mipsr4k.hi);
  }
  mipsr4k.lo=savedLo;mipsr4k.hi=savedHi;
  printf("PASS: 20000 HI/LO/multiply interpreter differential cases\n");
  const unsigned memoryKinds[]={32,33,34,35,36,37,38,40,41,42,43,46};
  for(unsigned i=0;i<10000;++i) {
    uint32_t initial[32];for(auto &v:initial)v=random();initial[0]=0;
    unsigned rs=1+random()%31,rt=random()%32,kind=memoryKinds[i%12];
    int16_t offset=int16_t(random());
    const uint32_t bases[]={0x089e0000,0x04000400,0x041ffffc,0x00010400};
    uint32_t address=bases[(i/12)%4]+(random()&252);
    if(kind==34||kind==38||kind==42||kind==46)address|=random()&3;
    initial[rs]=address-uint32_t(int32_t(offset));if(i&1)initial[rs]|=0x80000000;
    uint32_t op=(kind<<26)|(rs<<21)|(rt<<16)|uint16_t(offset);
    uint32_t old=Memory::ReadUnchecked_U32(address&~3u),referenceMemory;
    memcpy(mipsr4k.r,initial,sizeof(initial));mipsr4k.pc=0x08804000;
    MIPSGetInterpretFunc(MIPSOpcode(op))(MIPSOpcode(op));
    referenceMemory=Memory::ReadUnchecked_U32(address&~3u);
    Memory::WriteUnchecked_U32(old,address&~3u);
    uint32_t fast[32],fastPC=0x08804000;memcpy(fast,initial,sizeof(fast));
    assert(Wii::InterpretMemory(op,fast,fastPC));
    assert(!memcmp(fast,mipsr4k.r,sizeof(fast))&&fastPC==mipsr4k.pc);
    assert(referenceMemory==Memory::ReadUnchecked_U32(address&~3u));
    Memory::WriteUnchecked_U32(old,address&~3u);
  }
  printf("PASS: 10000 RAM/VRAM/scratch/merge interpreter differential cases\n");
  uint32_t registers[32]{};uint32_t fakePC=0;
  assert(!Wii::InterpretScalar(0x00484802,registers,fakePC)); // reserved SRL encoding
  assert(!Wii::InterpretScalar(0x8c880000,registers,fakePC)); // loads retain normal memory semantics
  using Clock=std::chrono::steady_clock;
  const uint32_t op=0x25080001;
  auto instruction=MIPSGetInterpretFunc(MIPSOpcode(op));
  volatile uint32_t codes[256];for(unsigned i=0;i<256;++i)codes[i]=0x25080000|(i+1);
  auto start=Clock::now();mipsr4k.r[8]=0;mipsr4k.pc=0;
  for(unsigned i=0;i<2000000;++i)instruction(MIPSOpcode(codes[i&255]));
  auto middle=Clock::now();registers[8]=0;fakePC=0;
  for(unsigned i=0;i<2000000;++i)Wii::InterpretScalar(codes[i&255],registers,fakePC);
  auto end=Clock::now();assert(registers[8]==mipsr4k.r[8]&&fakePC==mipsr4k.pc);
  printf("PASS: 50000 scalar differential cases; host ALU loop reference %.2f ms, fast %.2f ms (not game FPS)\n",std::chrono::duration<double,std::milli>(middle-start).count(),std::chrono::duration<double,std::milli>(end-middle).count());
  memcpy(mipsr4k.r,saved,sizeof(saved));mipsr4k.pc=pc;
}
