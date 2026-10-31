#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the actual clipper with its fast route enabled and disabled."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Clipper.cpp').read_text()
reference=s.replace('namespace Clipper {','namespace ReferenceClipper {').replace('#if PPSSPP_PLATFORM(WII)','#if 0')
t=(root/'GPU/Software/TransformUnit.cpp').read_text();a=t.index('static inline ScreenCoords ClipToScreenInternal');b=t.index('DrawingCoords TransformUnit::ScreenToDrawing',a)
project=t[a:b]
code=r'''
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Rasterizer.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
GPUgstate gstate{};GPUStateCache gstate_c{};
static std::vector<VertexData> output;
namespace ReferenceClipper {void ProcessTriangle(VertexData&,VertexData&,VertexData&);}
namespace Rasterizer {
void DrawTriangle(const VertexData&a,const VertexData&b,const VertexData&c){output.push_back(a);output.push_back(b);output.push_back(c);}
void DrawPoint(const VertexData&){}void DrawLine(const VertexData&,const VertexData&){}void ClearRectangle(const VertexData&,const VertexData&){}
bool TryDrawSprite(const VertexData&,const VertexData&,const VertexData&,const VertexData&){return false;}
}
'''+project+s+reference+r'''
static uint32_t F24(float value){uint32_t bits;memcpy(&bits,&value,4);return bits>>8;}
int main(){
 uint32_t seed=0x134ab;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 gstate.viewportxscale=gstate.viewportyscale=F24(120.f);gstate.viewportzscale=F24(30000.f);
 gstate.viewportxcenter=gstate.viewportycenter=F24(150.f);gstate.viewportzcenter=F24(32000.f);
 for(unsigned trial=0;trial<24000;++trial){
  gstate.clipEnable=trial&1;gstate.vertType=trial%7==0?GE_VTYPE_THROUGH:0;
  VertexData vertices[3]{};
  for(auto &v:vertices){
   v.clippos=ClipCoords((int(random()%65)-32)*0.0625f,(int(random()%65)-32)*0.0625f,(int(random()%65)-32)*0.0625f,1.f);
   v.screenpos=ScreenCoords(11,29,101);v.color0=Vec4<int>(13,29,101,255);v.fogdepth=1.f;
  }
  output.clear();ReferenceClipper::ProcessTriangle(vertices[0],vertices[1],vertices[2]);auto expected=output;
  output.clear();Clipper::ProcessTriangle(vertices[0],vertices[1],vertices[2]);assert(output.size()==expected.size());
  for(unsigned i=0;i<output.size();++i){
   const auto &a=output[i],&b=expected[i];
   assert(a.screenpos.x==b.screenpos.x&&a.screenpos.y==b.screenpos.y&&a.screenpos.z==b.screenpos.z);
   assert(!memcmp(&a.clippos,&b.clippos,sizeof(a.clippos))&&!memcmp(&a.texturecoords,&b.texturecoords,sizeof(a.texturecoords)));
   assert(!memcmp(&a.color0,&b.color0,sizeof(a.color0))&&!memcmp(&a.color1,&b.color1,sizeof(a.color1))&&a.fogdepth==b.fogdepth);
  }
 }
 puts("pass: 24000 actual fast/reference clipper cases, clipped and unclipped, both clipping settings and through mode");
}
'''
with tempfile.TemporaryDirectory(prefix='clipper-fast-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),str(root/'GPU/Math3D.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
