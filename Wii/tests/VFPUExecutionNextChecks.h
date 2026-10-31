// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/tests/CPUExecutionChecks.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#define V(i) (currentMIPS->v[voffset[i]])
#include "Wii/tests/VFPULaneReference.h"
#undef V
static bool previousVFPULanes;
extern void RealReadVector(float*,VectorSize,int) asm("__real__Z10ReadVectorPf10VectorSizei");
extern void RealWriteVector(const float*,VectorSize,int) asm("__real__Z11WriteVectorPKf10VectorSizei");
void WrappedReadVector(float*,VectorSize,int) asm("__wrap__Z10ReadVectorPf10VectorSizei");
void WrappedWriteVector(const float*,VectorSize,int) asm("__wrap__Z11WriteVectorPKf10VectorSizei");
void WrappedReadVector(float *v,VectorSize size,int reg){if(previousVFPULanes)PreviousReadVector(v,size,reg);else RealReadVector(v,size,reg);}
void WrappedWriteVector(const float *v,VectorSize size,int reg){if(previousVFPULanes)PreviousWriteVector(v,size,reg);else RealWriteVector(v,size,reg);}
static void VFPUExecutionNextChecks(){
 CPUObservedState saved;saved.Read();u32 vectors[128],ctrl[16];memcpy(vectors,currentMIPS->v,sizeof(vectors));memcpy(ctrl,currentMIPS->vfpuCtrl,sizeof(ctrl));
 const bool logging=g_Config.bEnableLogging;g_Config.bEnableLogging=false;
 const unsigned ops[]={0x60000000,0x60800000,0x63800000,0x64000000,0x64800000,0x66000000,0x6d000000,0xd0000000,0xd0010000,0xd0020000,0xd0040000,0xd0050000,0xd0060000,0xd0070000};
 u32 seed=0x187329;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(unsigned trial=0;trial<60000;++trial){
  const unsigned n=1+trial%4;
  // Saturation, constants, swizzles, abs/negation and all destination masks.
  const u32 prefixS=(random()&0xfffff),prefixT=(random()&0xfffff),prefixD=random()&0xfff;
  u32 initial[128];for(auto &v:initial){float f=(int(random()%1024)-512)*0.125f;memcpy(&v,&f,4);}
  const u32 sizeBits=(n==2||n==4?0x80u:0)|(n>=3?0x8000u:0);
  const u32 word=ops[trial%(sizeof(ops)/sizeof(ops[0]))]|sizeBits|(random()%128)|((random()%128)<<8)|(ops[trial%(sizeof(ops)/sizeof(ops[0]))]>>26<52?((random()%128)<<16):0);
  CPUObservedState expected;u32 expectedV[128],expectedCtrl[16];
  for(unsigned which=0;which<2;++which){
   saved.Write();currentMIPS->pc=0x08820000;memcpy(currentMIPS->v,initial,sizeof(initial));memcpy(currentMIPS->vfpuCtrl,ctrl,sizeof(ctrl));
   currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX]=prefixS;currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX]=prefixT;currentMIPS->vfpuCtrl[VFPU_CTRL_DPREFIX]=prefixD;
   previousVFPULanes=!which;MIPSInterpret(MIPSOpcode(word));CPUObservedState result;result.Read();
   if(!which){expected=result;memcpy(expectedV,currentMIPS->v,sizeof(expectedV));memcpy(expectedCtrl,currentMIPS->vfpuCtrl,sizeof(expectedCtrl));}
   else assert(expected.Same(result)&&!memcmp(expectedV,currentMIPS->v,sizeof(expectedV))&&!memcmp(expectedCtrl,currentMIPS->vfpuCtrl,sizeof(expectedCtrl)));
  }
 }

 if(getenv("WII_TEST_VFPU_BENCHMARK")) {
  const u32 arithmetic[]={0x60020180,0x60820180,0x64020180,0x64820180,0xd0000180,0xd0010180,0xd0020180,0xd0040180};
  for(unsigned masked=0;masked<2;++masked)for(unsigned round=0;round<9;++round)for(unsigned turn=0;turn<2;++turn){
   const unsigned updated=(turn+round)%2;previousVFPULanes=!updated;
   for(auto &v:currentMIPS->v)v=0.125f;
   const auto start=std::chrono::steady_clock::now();
   for(unsigned i=0;i<4000000;++i){
    currentMIPS->pc=0x08820000;
    currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX]=currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX]=0xe4;
    currentMIPS->vfpuCtrl[VFPU_CTRL_DPREFIX]=masked?0x500:0;
    MIPSInterpret(MIPSOpcode(arithmetic[i&7]));
   }
   printf("vfpu_dispatch,%u,%u,%s,%.6f\n",masked,round,updated?"updated":"previous",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
 }
 previousVFPULanes=false;saved.Write();memcpy(currentMIPS->v,vectors,sizeof(vectors));memcpy(currentMIPS->vfpuCtrl,ctrl,sizeof(ctrl));g_Config.bEnableLogging=logging;
 puts("pass: 60000 complete VFPU dispatches vs preceding lane helpers; scalar/vector sizes, aliasing, masks, prefixes and full CPU/vector/control state");
}
