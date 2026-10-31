#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual ClearRectangle to the previous implementation; optional host timing."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text();a=s.index('void ClearRectangle(');b=s.index('\nvoid DrawLine',a)
actual=s[a:b].replace('void ClearRectangle(', '__attribute__((noinline)) static void UpdatedClearRectangle(')
t=(root/'GPU/Software/TransformUnit.cpp').read_text();a=t.index('DrawingCoords TransformUnit::ScreenToDrawing');b=t.index('\nVertexData TransformUnit::ReadVertex',a)
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>
#include "Common/ColorConv.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "Wii/BufferFill.h"
using namespace Math3D;
GPUgstate gstate{};GPUStateCache gstate_c{};FormatBuffer fb,depthbuf;
namespace Wii {AddressSpace addressSpace;}
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
#include "Wii/tests/GEClearReference.h"
'''+t[a:b]+actual+r'''
static uint32_t seed=0x124213;
static uint32_t Random(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static volatile uint32_t checksum;
int main() {
 std::vector<uint8_t> vram(0x200000),ram(4096),scratch(16384);
 Wii::addressSpace.vram=vram.data();Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.scratch=scratch.data();
 for(unsigned trial=0;trial<1600;++trial) {
  for(auto *buffer:{&vram,&ram,&scratch}) {
   for(unsigned i=0;i<512;++i)(*buffer)[i]=uint8_t(Random());
   for(unsigned i=buffer->size()-512;i<buffer->size();++i)(*buffer)[i]=uint8_t(Random());
  }
  gstate={};gstate.framebufpixformat=trial%4;gstate.fbwidth=64;gstate.zbwidth=trial%3?64:0;
  gstate.clearmode=1|((trial/4)%8)<<8;
  gstate.offsetx=Random()%32;gstate.offsety=Random()%32;
  gstate.scissor1=(Random()%8)|((Random()%8)<<10);gstate.scissor2=31|(31<<10);
  switch((trial/32)%5) {
   case 0:fb.data=vram.data()+0x1ffff0;depthbuf.data=vram.data()+128;break;
   case 1:fb.data=ram.data()+4080;depthbuf.data=scratch.data()+16368;break;
   case 2:fb.data=scratch.data()+16368;depthbuf.data=fb.data;break;
   case 3:fb.data=vram.data();depthbuf.data=fb.data;break;
   default:fb.data=vram.data()+1;depthbuf.data=nullptr;break;
  }
  VertexData v0{},v1{};
  v0.screenpos.x=Random()%128;v0.screenpos.y=Random()%128;
  v1.screenpos.x=Random()%560;v1.screenpos.y=Random()%560;v1.screenpos.z=Random();
  v1.color0=Vec4<int>(Random()%256,Random()%256,Random()%256,Random()%256);
  if(trial%9==0) {v1.color0=Vec4<int>(0,0,0,0);v1.screenpos.z=65535;}
  const auto savedV=vram,savedR=ram,savedS=scratch;
  PreviousClearRectangle(v0,v1);
  const auto expectedV=vram,expectedR=ram,expectedS=scratch;
  std::copy(savedV.begin(),savedV.end(),vram.begin());std::copy(savedR.begin(),savedR.end(),ram.begin());std::copy(savedS.begin(),savedS.end(),scratch.begin());
  UpdatedClearRectangle(v0,v1);
  assert(vram==expectedV&&ram==expectedR&&scratch==expectedS);
 }
 puts("pass: 1600 actual/reference GE clears, all formats/masks, depth, clipping/offsets, aliasing, invalid ranges and VRAM wrap");
#ifdef BENCHMARK
 using Clear=void(*)(const VertexData&,const VertexData&);
 const Clear funcs[]={PreviousClearRectangle,UpdatedClearRectangle};
 const char *names[]={"rgb-mask","stencil-only","depth-pattern","uniform"};
 for(unsigned mode=0;mode<4;++mode) {
  gstate={};gstate.framebufpixformat=3;gstate.fbwidth=512;gstate.zbwidth=512;
  gstate.scissor2=479|(271<<10);gstate.clearmode=1|(mode==0?0x100:mode==1?0x200:mode==2?0x400:0x300);
  fb.data=vram.data();depthbuf.data=vram.data()+0x100000;
  VertexData v0{},v1{};v1.screenpos.x=480*16;v1.screenpos.y=272*16;v1.screenpos.z=mode==3?65535:0x37a2;
  v1.color0=mode==3?Vec4<int>(0,0,0,0):Vec4<int>(31,127,243,171);
  std::vector<double> times[2];
  for(unsigned trial=0;trial<7;++trial)for(unsigned slot=0;slot<2;++slot) {
   unsigned which=(slot+trial)%2;
   auto begin=std::chrono::steady_clock::now();
   for(unsigned repeat=0;repeat<200;++repeat)funcs[which](v0,v1);
   double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
   times[which].push_back(ms);checksum+=vram[13]+vram[0x100000];
   printf("sample,%s,%u,%s,%.3f\n",names[mode],trial,which?"updated":"previous",ms);
  }
  for(auto &v:times)std::sort(v.begin(),v.end());
  printf("HOST GE %s: previous %.3f ms updated %.3f ms (median7, 200 clears, not Wii FPS)\n",names[mode],times[0][3],times[1][3]);
 }
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='ge-clear-',dir=root) as tmp:
 p=Path(tmp);cpp=p/'clear.cpp';cpp.write_text(code);exe=p/'clear'
 flags=['-O3','-DBENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize=undefined','-fno-sanitize-recover=all']
 subprocess.run(['g++','-std=c++11']+flags+['-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
