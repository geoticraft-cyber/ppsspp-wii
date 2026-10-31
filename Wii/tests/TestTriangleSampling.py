#!/usr/bin/env python3
"""Fixed draw-time LOD/filter selection versus the original per-quad calculation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text()
helpers=s[s.index('// Produces a signed 1.23.8 value.'):s.index('static inline void ApplyTexturing(Sampler::Funcs sampler, Vec4<int> *prim_color')]
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "Core/Config.h"
#include "GPU/GPUState.h"
Config::Config(){}Config::~Config(){}Config g_Config;
GPUgstate gstate{};GPUStateCache gstate_c{};
'''+helpers+r'''
static uint32_t F24(float value){uint32_t bits;memcpy(&bits,&value,4);return bits>>8;}
int main(){
 uint32_t seed=0x726439;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned fixed=0,dynamic=0;
 for(unsigned trial=0;trial<200000;++trial){
   gstate={};gstate.texsize[0]=random()%10|((random()%10)<<8);
   gstate.texlevel=(trial%4)|((random()%256)<<16);
   gstate.texfilter=random()&0x107;
   gstate.texlodslope=F24((int(random()%4001)-2000)*0.01f);
   g_Config.iTexFiltering=random()%4;
   const unsigned maxLevel=random()%8;
   const auto prepared=PrepareWiiTriangleSampling(maxLevel);
   for(unsigned i=0;i<8;++i){
     const float ds=(int(random()%16001)-8000)*0.0001f,dt=(int(random()%16001)-8000)*0.0001f;
     int level,fraction;bool bilinear;
     CalculateSamplingParams(ds,dt,maxLevel,level,fraction,bilinear);
     if(prepared.fixed){assert(level==prepared.level&&fraction==prepared.fraction&&bilinear==prepared.bilinear);++fixed;}
     else ++dynamic;
   }
 }
 printf("pass: %u fixed and %u dynamic sampling decisions, AUTO/CONST/SLOPE/unknown mode, mip limits, bias, min/mag/mip filters and overrides\n",fixed,dynamic);
}
'''
with tempfile.TemporaryDirectory(prefix='triangle-sampling-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
