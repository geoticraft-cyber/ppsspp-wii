#!/usr/bin/env python3
"""Differential checks of actual specialized pixel routines and reference code."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text()
helpers=s[s.index('static inline u32 GetPixelColor('):s.index('static inline void ApplyTexturing(')]
code=r'''
#if defined(__SANITIZE_ADDRESS__)
// LeakSanitizer cannot enumerate threads in the restricted host environment.
extern "C" int __lsan_is_turned_off() {return 1;}
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Common/ColorConv.h"
#include "Wii/PreparedBuffer.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/SoftGpu.h"
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
using namespace Math3D;
GPUgstate gstate{};GPUStateCache gstate_c{};FormatBuffer fb,depthbuf;
namespace Wii {AddressSpace addressSpace;}
'''+helpers+r'''
int main() {
 std::vector<uint8_t> ram(0x2000000),vram(0x200000);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();
 uint32_t seed=0x12564578;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned comparisons=0;
 for(unsigned trial=0;trial<400000;++trial) {
  gstate={};
  gstate.framebufpixformat=trial%4;
  gstate.fbwidth=64;gstate.zbwidth=(trial%7)?64:129;gstate.pmska=random()&255;
  gstate.pmskc=trial%5?0:random()&0xffffff;
  gstate.maxz=65535;
  gstate.alphaTestEnable=(trial>>8)&1;gstate.alphatest=(random()&0xffff00)|(random()%8);
  gstate.textureMapEnable=(trial>>9)&1;gstate.texfunc=(trial&1024)?0x10000:0;
  gstate.vertType=(trial&2)?GE_VTYPE_THROUGH:0;
  if(trial%3==0){gstate.minz=10000;gstate.maxz=50000;}
  gstate.fogEnable=(trial>>11)&1;gstate.fogcolor=random()&0xffffff;
  gstate.colorTestEnable=(trial>>12)&1;gstate.colortest=random()%4;
  gstate.colorref=random()&0xffffff;gstate.colortestmask=random()&0xffffff;
  gstate.zTestEnable=(trial>>2)&1;gstate.ztestfunc=(trial>>3)&7;
  gstate.zmsk=(trial>>6)&1;gstate.alphaBlendEnable=(trial>>7)&1;
  gstate.blend=(trial%6)<<8|(random()%16)<<4|(random()%16);
  gstate.blendfixa=random()&0xffffff;gstate.blendfixb=random()&0xffffff;
  if(trial%11==0){gstate.stencilTestEnable=1;gstate.stenciltest=GE_COMP_ALWAYS;gstate.stencilop=0;}
  // Both backing regions, mirrored VRAM and end-of-RAM rejection use the
  // same bounded buffer access semantics as the reference pixel path.
  fb.data=(trial%3==0)?ram.data()+ram.size()-32:vram.data()+0x1ffff0;
  depthbuf.data=trial%5==0?fb.data:ram.data()+4096;
  const DrawingCoords p(random()%8,random()%4,0);const u16 z=random();
  const Vec4<int> color(int(random()%1024),int(random()%1024),int(random()%1024),int(random()%256));
  const u32 old=random();const u16 oldZ=random();
  if(gstate.FrameBufFormat()==GE_FORMAT_8888)fb.Set32(p.x,p.y,64,old);else fb.Set16(p.x,p.y,64,old);
  depthbuf.Set16(p.x,p.y,gstate.DepthBufStride(),oldZ);
  const u32 saved=gstate.FrameBufFormat()==GE_FORMAT_8888?fb.Get32(p.x,p.y,64):fb.Get16(p.x,p.y,64);
  const u16 savedZ=depthbuf.Get16(p.x,p.y,gstate.DepthBufStride());
  auto pipeline=PrepareWiiPixelPipeline();assert(pipeline.draw);
  const u8 fog=random();
  DrawSinglePixel<false>(p,z,fog,color);
  const u32 expected=gstate.FrameBufFormat()==GE_FORMAT_8888?fb.Get32(p.x,p.y,64):fb.Get16(p.x,p.y,64);
  const u16 expectedZ=depthbuf.Get16(p.x,p.y,gstate.DepthBufStride());
  if(gstate.FrameBufFormat()==GE_FORMAT_8888)fb.Set32(p.x,p.y,64,saved);else fb.Set16(p.x,p.y,64,saved);
  depthbuf.Set16(p.x,p.y,gstate.DepthBufStride(),savedZ);
  pipeline.draw(pipeline,p,z,color,fog);
  const u32 actual=gstate.FrameBufFormat()==GE_FORMAT_8888?fb.Get32(p.x,p.y,64):fb.Get16(p.x,p.y,64);
  assert(actual==expected&&depthbuf.Get16(p.x,p.y,gstate.DepthBufStride())==expectedZ);++comparisons;
 }
 // Only states with remaining reference-only behavior reject preparation.
 for(unsigned kind=0;kind<3;++kind) {
  gstate={};gstate.framebufpixformat=GE_FORMAT_8888;
  if(kind==0)gstate.stencilTestEnable=1;
  if(kind==1)gstate.logicOpEnable=1;
  if(kind==2)gstate.clearmode=1;
  assert(!PrepareWiiPixelPipeline().draw);
 }
 printf("pass: %u exact specialized/reference pixels; formats, all depth predicates, depth writes, blend equations/factors, clamp, stencil preservation, VRAM wrap, bounds and fallback gates\n",comparisons);
}
'''
with tempfile.TemporaryDirectory(prefix='performance-pixels-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
