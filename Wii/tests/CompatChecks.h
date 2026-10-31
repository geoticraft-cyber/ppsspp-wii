// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/ELF/ParamSFO.h"
#include "Wii/Settings.h"
#include "Wii/RenderCompatibility.h"
#include "Wii/tests/RenderCompatibilityChecks.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/scePower.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/GPUInterface.h"
#include "Common/ChunkFile.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>

static void CompatChecks() {
 const std::string id=g_paramSFO.GetDiscID();
 const bool gos=id=="UCUS98737"||id=="UCES01401";
 const bool coo=id=="UCUS98653"||id=="UCES00842";
 const bool lbp=id=="UCUS98744"||id=="UCES01264"||id=="UCJS10107"||id=="NPJG00073"||id=="UCAS40262";
 const bool buffered=coo||id=="ULUS10383"||id=="ULUS10160";
 assert(gos||coo||lbp||buffered||id=="HBWI00001");
 const auto &flags=PSP_CoreParameter().compat.flags();
 assert(flags.RequireDefaultCPUClock==gos&&flags.NullPageValid==gos);
 assert(flags.GoWFramerateHack60==(gos||coo));
 assert(flags.DisallowFramebufferAtOffset==lbp);
 assert(flags.RequireBufferedRendering==buffered);
 if(buffered)assert(g_Config.iRenderingMode==1);
 assert(g_Config.iLockedCPUSpeed==333&&GetLockedCPUSpeedMhz()==(gos?0:333));
 assert(CoreTiming::GetClockFrequencyMHz()==(gos?222:333));
 __PowerApplyWiiClock(135);
 assert(g_Config.iLockedCPUSpeed==135&&CoreTiming::GetClockFrequencyMHz()==(gos?222:135));
 currentMIPS->r[4]=222;currentMIPS->r[5]=111;currentMIPS->r[6]=111;
 GetFunc("scePower",0x737486f2)->func();assert(currentMIPS->r[2]==0);
 assert(CoreTiming::GetClockFrequencyMHz()==(gos?111:135));
 currentMIPS->r[4]=222;GetFunc("scePower",0x843fbf43)->func();assert(currentMIPS->r[2]==0);
 assert(CoreTiming::GetClockFrequencyMHz()==(gos?222:135));
 printf("pass: %s title flags and effective clock; game clock requests remain permitted\n",id.c_str());
 assert(bool(Wii::addressSpace.nullPage)==gos);
 if(gos) {
  for(u32 alias:{0u,0x40000000u,0x80000000u,0xc0000000u}) {
   assert(Memory::IsValidRange(alias,0x10000)&&!Memory::IsValidRange(alias|0xfffc,8));
   Memory::Write_U32(0x12345678,alias|0xfffc);
   assert(Memory::Read_U32(0xfffc)==0x12345678);
  }
  assert(!Memory::IsValidAddress(0x14000));
  const u8 *raw=Memory::GetPointer(0xfffc);assert(raw[0]==0x78&&raw[3]==0x12);
 } else {assert(!Memory::IsValidAddress(0)&&!Memory::IsValidAddress(0xffff));}
 // Include the conditional page in the real memory serializer, not a mock.
 std::vector<u8> state(Memory::g_MemorySize+0x200000+0x14000+1024);
 u8 *cursor=state.data();{PointerWrap p(&cursor,PointerWrap::MODE_WRITE);Memory::DoState(p);assert(!p.error);}
 Memory::Clear();if(gos)assert(Memory::Read_U32(0xfffc)==0);
 cursor=state.data();{PointerWrap p(&cursor,PointerWrap::MODE_READ);Memory::DoState(p);assert(!p.error);}
 if(gos)assert(Memory::Read_U32(0xfffc)==0x12345678);
 printf("pass: %s conditional null page, bounds, aliases, little-endian bytes, clear and memory savestate round trip\n",id.c_str());
 auto command=[](unsigned cmd,u32 data){const u32 op=(cmd<<24)|(data&0xffffff),old=gstate.cmdmem[cmd];gstate.cmdmem[cmd]=op;gpu->ExecuteOp(op,old^op);};
 command(0x9d,512);command(0xd2,3);
 gstate_c.SetCurRTOffsetX(37);command(0x9c,0);
 assert(fb.data==Memory::GetPointer(0x04000000));fb.Set32(0,0,512,0x11223344);
 gstate_c.SetCurRTOffsetX(19);command(0x9c,0x100);
 assert(fb.data==Memory::GetPointer(0x04000100));
 assert(gstate_c.curRTOffsetX==(lbp?0u:19u));
 fb.Set32(0,0,512,0x55667788);
 assert(Memory::Read_U32(0x04000000)==0x11223344&&Memory::Read_U32(0x04000100)==0x55667788);
 gstate_c.SetCurRTOffsetX(23);command(0x9d,512);
 assert(fb.data==Memory::GetPointer(0x04000100)&&gstate_c.curRTOffsetX==(lbp?0u:23u));
 // A restored command image must rebind the exact guest base and normalize
 // stale derived target offsets through the normal replay path too.
 gstate_c.SetCurRTOffsetX(31);gpu->ReapplyGfxState();
 assert(fb.data==Memory::GetPointer(0x04000100)&&gstate_c.curRTOffsetX==(lbp?0u:31u));
 puts("pass: framebuffer pointer/stride commands and state replay preserve exact guest bases and disallow derived offset reuse");
 RenderCompatibilityChecks(buffered);

 // Exercise the upstream recognized-hash routing and entry/exit locations.
 auto fps=GetReplacementFuncIndexes(0x795d940ad0a605f8ULL,40);
 auto vortex=GetReplacementFuncIndexes(0x4c75043b7b0c643bULL,512);
 assert(fps.size()==1&&vortex.size()==1);
 assert(GetReplacementFuncIndexes(0x795d940ad0a605f8ULL,44).empty());
 assert(GetReplacementFunc(vortex[0])->hookOffset==0x60);
 for(u32 s1:{0u,1u})for(u32 a0:{0xc0u,0xbfu})for(u32 t4:{0x3f800000u,0x3f000000u}) {
  currentMIPS->r[17]=s1;currentMIPS->r[4]=a0;currentMIPS->r[12]=t4;currentMIPS->downcount=100;
  GetReplacementFunc(vortex[0])->replaceFunc();
  bool change=(gos||coo)&&s1==0&&a0==0xc0&&t4!=0x3f800000;
  assert(currentMIPS->r[17]==(change?1:s1)&&currentMIPS->downcount==(change?0:100));
 }
 const u32 fn=0x089b0000,code=0x089c0000,marker=0x089d0000;
 for(unsigned i=0;i<10;++i)Memory::Write_U32(0,fn+i*4);
 Memory::Write_U32(0x03e00008,fn+32);Memory::Write_U32(0x26520007,fn+36);
 WriteReplaceInstructions(fn,0x795d940ad0a605f8ULL,40);
 u32 original=0;assert(GetReplacedOpAt(fn+32,&original)&&original==0x03e00008);
 std::vector<u32> words;
 auto load=[&](unsigned reg,u32 n){words.push_back(0x3c000000|(reg<<16)|(n>>16));words.push_back(0x34000000|(reg<<21)|(reg<<16)|(n&65535));};
 load(2,0x12345678);load(18,0);words.push_back(0x0c000000|(fn>>2));words.push_back(0);
 load(8,marker);load(9,0xfeed11aa);words.push_back(0xad120004);words.push_back(0xad020008);words.push_back(0xad090000);
 const u32 stop=code+4*words.size();words.push_back(0x08000000|(stop>>2));words.push_back(0);
 for(unsigned i=0;i<words.size();++i)Memory::Write_U32(words[i],code+i*4);
 currentMIPS->InvalidateICache(code,words.size()*4);currentMIPS->InvalidateICache(fn,40);
 currentMIPS->pc=code;currentMIPS->inDelaySlot=false;
 const int beforeVCount=__DisplayGetVCount();
 for(unsigned i=0;i<10000&&Memory::Read_U32(marker)!=0xfeed11aa;++i) {
  coreState=CORE_RUNNING;PSP_BeginHostFrame();PSP_RunLoopFor(10000);PSP_EndHostFrame();
 }
 assert(Memory::Read_U32(marker)==0xfeed11aa&&Memory::Read_U32(marker+4)==7&&Memory::Read_U32(marker+8)==((gos||coo)?0u:0x12345678u));
 if(gos||coo)assert(__DisplayGetVCount()>beforeVCount);
 assert(!currentMIPS->inDelaySlot);
 RestoreReplacedInstructions(fn,fn+40);
 puts("pass: recognized gow hooks, vortex predicates, real guest return/delay slot, upstream wait return value, and vblank wakeup");
}
