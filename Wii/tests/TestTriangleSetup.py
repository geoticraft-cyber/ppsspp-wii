#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual shared triangle setup to its per-fragment reference path."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text()
def function(start):
 a=s.index(start);b=s.index('{',a);d=1;e=b+1
 while d:d+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
a=s.index('struct TriangleTextureState');b=s.index('// NOTE: These likely',a)
helpers=s[a:b]
edge=s[s.index('struct TriangleEdge'):s.index('static inline Vec4<int> MakeMask')]
code=r'''
#include "GPU/Software/TransformUnit.h"
#include "GPU/ge_constants.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
GPUgstate gstate{};GPUStateCache gstate_c{};
'''+function('static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<float>')+'\n'+function('static inline Vec4<float> Interpolate(const float &c0, const float &c1, const float &c2, const Vec4<int>')+'\n'+helpers+'\n'+edge+'\n'+function('static inline Vec4<float> EdgeRecip(')+r'''
int main() {
 uint32_t seed=0x234ef;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned comparisons=0;
 for(unsigned trial=0;trial<12000;++trial) {
  VertexData v[3]{};
  for(auto &vertex:v) {
   vertex.screenpos=ScreenCoords(random()%1024,random()%1024,0);
   vertex.clippos.w=0.5f+(random()%32)*0.125f;
   vertex.normal=Vec3<float>(0.25f+(random()%8),0.5f,1.0f);
   vertex.modelpos=Vec3<float>(0.25f+(random()%8),0.5f,1.0f);
   vertex.texturecoords=Vec2<float>((random()%64)*0.125f,(random()%64)*0.125f);
  }
  TriangleEdge edges[3];ScreenCoords origin(0,0,0);
  Vec4<int> base[3]={edges[0].Start(v[1].screenpos,v[2].screenpos,origin),edges[1].Start(v[2].screenpos,v[0].screenpos,origin),edges[2].Start(v[0].screenpos,v[1].screenpos,origin)};
  const auto reciprocal=EdgeRecip(base[0],base[1],base[2]);
  gstate.texmapmode=trial%4|(((trial/4)%4)<<8);
  const float matrix[]={1,0,0,0,1,0,0,0,1,0,0,1};memcpy(gstate.tgenMatrix,matrix,sizeof(matrix));
  TriangleTextureState prepared;PrepareTriangleTexture(v[0],v[1],v[2],prepared);
  for(unsigned y=0;y<8;++y) {
   Vec4<int> w[3]={base[0],base[1],base[2]};
   for(unsigned x=0;x<8;++x) {
    const auto reference=EdgeRecip(w[0],w[1],w[2]);
    assert(!memcmp(&reciprocal,&reference,sizeof(reference)));
    if(w[0].x+w[1].x+w[2].x) {
     Vec4<float> s0,t0,s1,t1;
     GetTextureCoordinates(v[0],v[1],v[2],w[0],w[1],w[2],reference,s0,t0);
     GetTextureCoordinates(v[0],v[1],v[2],w[0],w[1],w[2],reciprocal,s1,t1,&prepared);
     assert(!memcmp(&s0,&s1,sizeof(s0))&&!memcmp(&t0,&t1,sizeof(t0)));
    }
    for(unsigned i=0;i<3;++i)w[i]=edges[i].StepX(w[i]);++comparisons;
   }
   for(unsigned i=0;i<3;++i)base[i]=edges[i].StepY(base[i]);
  }
 }
 printf("pass: %u exact shared/per-fragment barycentric and texture-coordinate comparisons, all UV projection modes\n",comparisons);
}
'''
with tempfile.TemporaryDirectory(prefix='triangle-setup-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),str(root/'GPU/Math3D.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
