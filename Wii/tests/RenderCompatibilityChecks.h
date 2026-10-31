// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/Settings.h"
#include "GPU/ge_constants.h"
#include "GPU/Software/SoftGpu.h"

static void RenderCompatibilityChecks(bool required) {
 const bool savedSkip=Wii::GetSettings().skipBufferEffects;
 const u32 target=0x04100000,vertices=0x089a0000;
 auto cmd=[](unsigned command,u32 data) {
  const u32 op=(command<<24)|(data&0xffffff),old=gstate.cmdmem[command];
  gstate.cmdmem[command]=op;gpu->ExecuteOp(op,old^op);
 };
 cmd(GE_CMD_FRAMEBUFPTR,target&0xffffff);cmd(GE_CMD_FRAMEBUFWIDTH,512);
 cmd(GE_CMD_FRAMEBUFPIXFORMAT,GE_FORMAT_8888);
 cmd(GE_CMD_REGION1,0);cmd(GE_CMD_REGION2,31|(31<<10));
 cmd(GE_CMD_SCISSOR1,0);cmd(GE_CMD_SCISSOR2,31|(31<<10));
 cmd(GE_CMD_OFFSETX,0);cmd(GE_CMD_OFFSETY,0);cmd(GE_CMD_CLEARMODE,0);
 cmd(GE_CMD_VERTEXTYPE,GE_VTYPE_THROUGH|GE_VTYPE_COL_8888|GE_VTYPE_POS_16BIT);
 for(unsigned command:{GE_CMD_LIGHTINGENABLE,GE_CMD_CULLFACEENABLE,GE_CMD_TEXTUREMAPENABLE,
   GE_CMD_ALPHABLENDENABLE,GE_CMD_ALPHATESTENABLE,GE_CMD_ZTESTENABLE,
   GE_CMD_STENCILTESTENABLE,GE_CMD_COLORTESTENABLE,GE_CMD_FOGENABLE,
   GE_CMD_LOGICOPENABLE,GE_CMD_PATCHCULLENABLE})cmd(command,0);
 cmd(GE_CMD_ZWRITEDISABLE,1);cmd(GE_CMD_MASKRGB,0);cmd(GE_CMD_MASKALPHA,0);
 cmd(GE_CMD_PATCHDIVISION,4|(4<<8));cmd(GE_CMD_PATCHPRIMITIVE,0);cmd(GE_CMD_PATCHFACING,0);
 auto vertex=[&](unsigned index,unsigned x,unsigned y) {
  const u32 at=vertices+index*12;
  Memory::Write_U32(0xff4488cc,at);Memory::Write_U16(x,at+4);
  Memory::Write_U16(y,at+6);Memory::Write_U16(0,at+8);Memory::Write_U16(0,at+10);
 };
 const size_t bytes=512*32*4;
 for(unsigned command:{GE_CMD_PRIM,GE_CMD_BEZIER,GE_CMD_SPLINE}) {
  if(command==GE_CMD_PRIM) {vertex(0,2,2);vertex(1,26,2);vertex(2,2,26);}
  else for(unsigned v=0;v<4;++v)for(unsigned u=0;u<4;++u)vertex(v*4+u,2+u*8,2+v*8);
  auto draw=[&](unsigned reason,bool skip) {
   memset(Memory::GetPointer(target),0,bytes);
   gstate_c.skipDrawReason=reason;Wii::GetSettings().skipBufferEffects=skip;
   cmd(GE_CMD_BASE,(vertices>>8)&0xff0000);cmd(GE_CMD_VADDR,vertices&0xffffff);
   cmd(command,command==GE_CMD_PRIM?(GE_PRIM_TRIANGLES<<16)|3:4|(4<<8));
   return std::vector<u8>(Memory::GetPointer(target),Memory::GetPointer(target)+bytes);
  };
  const auto reference=draw(0,false);
  assert(std::any_of(reference.begin(),reference.end(),[](u8 b){return b!=0;}));
  assert(draw(SKIPDRAW_NON_DISPLAYED_FB,false)==reference);
  const auto requested=draw(SKIPDRAW_NON_DISPLAYED_FB,true);
  if(required)assert(requested==reference);
  else assert(std::all_of(requested.begin(),requested.end(),[](u8 b){return b==0;}));
  const auto skipped=draw(SKIPDRAW_SKIPFRAME|SKIPDRAW_NON_DISPLAYED_FB,true);
  assert(std::all_of(skipped.begin(),skipped.end(),[](u8 b){return b==0;}));
  assert(Wii::GetSettings().skipBufferEffects); // never rewrite the preference
 }
 Wii::GetSettings().skipBufferEffects=savedSkip;gstate_c.skipDrawReason=0;
 puts("pass: actual triangle/bezier/spline offscreen pixels obey required buffering, preserve optional skipping and frame skipping");
}
