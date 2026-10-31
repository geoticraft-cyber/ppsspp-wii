#!/usr/bin/env python3
"""Compile and execute the actual per-sprite memo extracted from Rasterizer.cpp."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text();a=s.index('  struct TexelEntry',s.index('bool TryDrawSprite'));b=s.index('  // The sampler already applies',a);body=s[a:b]
with tempfile.TemporaryDirectory(prefix='sprite-memo-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp'
 cpp.write_text('''#include "Wii/AddressSpace.h"
#include "Wii/Settings.h"
#include "Wii/GraphicsWork.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstdint>
namespace Wii {AddressSpace addressSpace;Settings settings;Settings &GetSettings(){return settings;}}
template<class T> struct Vec4 {
 T x=0,y=0,z=0,w=0;
 static Vec4 FromRGBA(uint32_t c){Vec4 v;v.x=c&255;v.y=(c>>8)&255;v.z=(c>>16)&255;v.w=c>>24;return v;}
};
static Vec4<int> GetTextureFunctionOutput(const Vec4<int>&p,const Vec4<int>&t) {
 Vec4<int> o;o.x=p.x*t.x/255;o.y=p.y*t.y/255;o.z=p.z*t.z/255;o.w=p.w*t.w/255;return o;
}
static void GetTexelCoordinates(int,float s,float t,int &u,int &v){u=int(s*16)&15;v=int(t*16)&15;}
static unsigned reads;
static uint32_t Read(int u,int v,const uint8_t*p,int stride,int){++reads;return 0xff000000|p[v*stride+u]*0x010101;}
const unsigned GE_FORMAT_8888=3;
struct State {unsigned frame=0x04000000;unsigned getTextureFormat(){return 4;}unsigned getTextureHeight(int){return 16;}unsigned FrameBufFormat(){return 3;}unsigned getFrameBufAddress(){return frame;}unsigned FrameBufStride(){return 64;}}gstate;
static void Run(bool feedback,bool enabled) {
 bool textured=true;int texbufw[8]={16};
 uint8_t *texptr[8]={Wii::addressSpace.ram};
 if(feedback)texptr[0]=Wii::addressSpace.vram;
 struct {uint32_t(*nearest)(int,int,const uint8_t*,int,int);}sampler{Read};
 struct {Vec4<int>color0;}br; br.color0=Vec4<int>::FromRGBA(0x9aef7723);
 int x0=0,x1=64,y0=0,y1=64;Wii::GetSettings().cpuSpriteCache=enabled;
''' +body+'''
 const unsigned before=reads;
 for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
  if(feedback)texptr[0][(y/4)*16+x/4]^=7;
  const float s=(x+0.5f)/64,t=(y+0.5f)/64;
  int u,v;GetTexelCoordinates(0,s,t,u,v);
  auto expected=GetTextureFunctionOutput(br.color0,Vec4<int>::FromRGBA(0xff000000|texptr[0][v*16+u]*0x010101));
  Vec4<int> actual;sample(actual,s,t);
  assert(actual.x==expected.x&&actual.y==expected.y&&actual.z==expected.z&&actual.w==expected.w);
 }
 assert(reads-before==(enabled&&!feedback?256u:4096u));
}
int main() {
 std::vector<uint8_t>ram(1024),vram(0x200000);for(unsigned i=0;i<ram.size();++i)ram[i]=i;
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();Wii::addressSpace.RebuildPages();
 Run(false,true);Run(false,false);Run(true,true);ram[13]^=99;Run(false,true);
 puts("pass: actual CPU sprite memo, 16x fewer sampler calls at 4x enlargement, exact colors, disabled path, source edits and sequential feedback");
}
''')
 exe=p/'test';subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-I'+str(root),str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe.resolve())],check=True)
