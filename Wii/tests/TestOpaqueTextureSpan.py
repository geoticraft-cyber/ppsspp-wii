#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual CPU texture function and opaque sprite span against bytes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text()
a=s.index('static inline Vec4<int> GetTextureFunctionOutput');b=s.index('static inline bool ColorTestPassed',a)
texture=s[a:b]
a=s.index('  if(simple) {',s.index('bool TryDrawSprite'));b=s.index('  ++Wii::GetGraphicsWork().pixelStateFallbacks;',a)
span=s[a:b]
code=r'''
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdio>
#include "GPU/Math3D.h"
#include "GPU/ge_constants.h"
#include "Wii/OpaqueSpan.h"
#include "Wii/GraphicsWork.h"
using namespace Math3D;
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
namespace Wii {AddressSpace addressSpace;bool EmulationStopRequested(){return false;}}
struct State {
 unsigned function=0,format=0;bool alpha=false;
 unsigned getTextureFunction()const{return function;}
 bool isTextureAlphaUsed()const{return alpha;}
 int getTextureEnvColR()const{return 37;}int getTextureEnvColG()const{return 241;}int getTextureEnvColB()const{return 103;}
 unsigned getFrameBufAddress()const{return 0x441ffff0;}
 unsigned FrameBufStride()const{return 64;}unsigned FrameBufFormat()const{return format;}
}gstate;
''' +texture+r'''
static unsigned lookups=0;
static uint32_t Texel(int x,int y){return uint32_t((x*27+y*13)&255)|(((x*19+y*51)&255)<<8)|(((x*11+y*23)&255)<<16)|(((x*31+y*43)&255)<<24);}
static bool Run(unsigned function,unsigned format,bool alpha,bool flipped,std::vector<uint8_t>&vram) {
 gstate.function=function;gstate.format=format;gstate.alpha=alpha;
 bool simple=true,textured=true;
 struct {int x=0,y=0;}top;
 struct {Vec4<int> color0;struct{float x,y;}texturecoords;}br,tl;
 br.color0=Vec4<int>(83,179,231,137);tl.texturecoords.x=flipped?8:0;tl.texturecoords.y=flipped?8:0;
 const float du=flipped?-0.25f:0.25f,dv=du,invW=1.0f/8,invH=invW;
 const int x0=3,x1=30,y0=2,y1=29;
 const auto sample=[&](Vec4<int>&color,float u,float v) {
   ++lookups;
   color=GetTextureFunctionOutput(br.color0,Vec4<int>::FromRGBA(Texel(int(std::floor(u*8))&7,int(std::floor(v*8))&7)));
 };
 std::fill(vram.begin(),vram.end(),0xa7);
 auto expected=vram;
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
   const int u=int(std::floor(tl.texturecoords.x+(x+0.5f)*du))&7;
   const int v=int(std::floor(tl.texturecoords.y+(y+0.5f)*dv))&7;
   const auto c=GetTextureFunctionOutput(br.color0,Vec4<int>::FromRGBA(Texel(u,v)));
   const uint32_t rgb=(unsigned(c.x)<<16)|(unsigned(c.y)<<8)|unsigned(c.z);
   const unsigned bpp=format==3?4:2,offset=(0x1ffff0+(y*64+x)*bpp)&0x1fffff;
   const uint32_t keep=format==3?0xa7000000:format==1?0x8000:format==2?0xa000:0;
   const uint32_t pixel=Wii::RGBToPSPPixel(rgb,0,format)|keep;
   for(unsigned i=0;i<bpp;++i)expected[offset+i]=uint8_t(pixel>>(i*8));
 }
 const auto draw=[&]()->bool {
''' +span+r'''
 return false;
 };
 const unsigned before=lookups;
 assert(draw()&&vram==expected&&lookups-before==unsigned((x1-x0)*(y1-y0)));
 return true;
}
int main() {
 std::vector<uint8_t> vram(0x200000);Wii::addressSpace.vram=vram.data();
 for(unsigned fn=0;fn<5;++fn)for(unsigned fmt=0;fmt<4;++fmt)for(bool alpha:{false,true})for(bool flip:{false,true})Run(fn,fmt,alpha,flip,vram);
 puts("pass: 80 CPU opaque texture spans, all five texture functions, four framebuffer formats, alpha modes, flipped/clipped UVs, VRAM wrap and stencil preservation");
}
'''
with tempfile.TemporaryDirectory(prefix='opaque-texture-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
