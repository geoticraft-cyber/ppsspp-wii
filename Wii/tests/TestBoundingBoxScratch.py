#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual bounded Wii bbox path with deterministic decoder/planes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Common/DrawEngineCommon.cpp').read_text()
a=s.index('bool DrawEngineCommon::TestBoundingBox(');b=s.index('// TODO: This probably is not the best interface.',a)
code=r'''
#include <cassert>
#include <algorithm>
#include <memory>
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define PPSSPP_PLATFORM(x) 1
using u8=uint8_t;using u32=uint32_t;using s8=int8_t;using s16=int16_t;using s16_le=int16_t;using float_le=float;
constexpr u32 GE_VTYPE_POS_FLOAT=1,GE_VTYPE_POS_8BIT=2,GE_VTYPE_POS_16BIT=3;
struct Vec {float x,y,z;};struct SimpleVertex {float uv[2];uint32_t color;Vec normal,pos;};
static_assert(sizeof(SimpleVertex)==36,"matches the core packed SimpleVertex layout");
struct {float worldMatrix[12],viewMatrix[12],projMatrix[16];}gstate;
struct Plane {int axis;float direction;float Test(const float *p){return 0.25f+direction*p[axis];}};
void ConvertMatrix4x3To4x4(float *,const float *){}void Matrix4ByMatrix4(float *,const float *,const float *){}
void PlanesFromMatrix(const float *,Plane*p){for(int i=0;i<6;++i)p[i]={i/2,(i&1)?-1.f:1.f};}
struct Format {unsigned stride=64;};
struct VertexDecoder {Format format;const Format&GetDecVtxFmt(){return format;}int VertexSize(){return 12;}}decoder;
unsigned scratchRequests=0,normalizeCalls=0,expectedCount=0,maxAllocated=0,fullScratchCalls=0;bool failScratch=false;
void *__real__ZnamRKSt9nothrow_t(size_t,const std::nothrow_t &) asm("__real__ZnamRKSt9nothrow_t");
extern "C" void *__wrap__ZnamRKSt9nothrow_t(size_t size,const std::nothrow_t &tag) {
 ++scratchRequests;maxAllocated=std::max<unsigned>(maxAllocated,size);
 assert(size<=64*(256+sizeof(SimpleVertex))+16);
 if(failScratch)return nullptr;return __real__ZnamRKSt9nothrow_t(size,tag);
}
struct DrawEngineCommon {
 bool EnsureScratchBuffers(bool){++fullScratchCalls;return false;}
 VertexDecoder *GetVertexDecoder(u32){return &decoder;}
 void NormalizeVertices(u8 *out,u8 *temp,u8 *in,int low,int upper,u32,int *size) {
   ++normalizeCalls;assert(low==0&&upper==int(expectedCount)-1);
   *size=12;auto *corners=reinterpret_cast<SimpleVertex *>(out);
   auto *positions=reinterpret_cast<float *>(in);
   for(int i=0;i<=upper;++i)corners[i].pos={positions[i*3],positions[i*3+1],positions[i*3+2]};
   memset(temp,0,(upper+1)*decoder.format.stride);
 }
 bool TestBoundingBox(void*,int,u32,int*);
};
''' +s[a:b]+r'''
int main(){
 DrawEngineCommon draw;uint32_t seed=0x9834235;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(unsigned trial=0;trial<5000;++trial)for(unsigned kind=1;kind<=4;++kind) {
   expectedCount=trial%3==0?1:trial%3==1?8:64;
   std::vector<float> positions(expectedCount*3);std::vector<int8_t> bytes(positions.size());std::vector<int16_t> halves(positions.size());
   for(unsigned i=0;i<positions.size();++i){
    bytes[i]=int8_t(random());halves[i]=int16_t(random());
    positions[i]=kind==2?bytes[i]/128.f:kind==3?halves[i]/32768.f:(int(random()%4097)-2048)/1024.f;
   }
   bool expected=true;
   for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}){
    bool any=false;for(unsigned i=0;i<expectedCount;++i)any=any||(0.25f+sign*positions[i*3+axis]>=0);
    if(!any)expected=false;
   }
   int read=-1;void *source=kind==2?static_cast<void*>(bytes.data()):kind==3?static_cast<void*>(halves.data()):static_cast<void*>(positions.data());
   const unsigned before=scratchRequests;
   assert(draw.TestBoundingBox(source,expectedCount,kind,&read)==expected);
   assert(read==int(expectedCount*(kind==2?3:kind==3?6:12)));
   assert(scratchRequests-before==(kind==4?1u:0u)&&!fullScratchCalls);
 }
 float positions[24]={};int read=-1;expectedCount=8;
 const auto before=normalizeCalls;failScratch=true;
 assert(draw.TestBoundingBox(positions,8,4,&read)&&read==96&&normalizeCalls==before);
 failScratch=false;decoder.format.stride=257;
 assert(draw.TestBoundingBox(positions,8,4,&read)&&read==0);
 assert(draw.TestBoundingBox(positions,65,1,&read)&&read==0);
 assert(draw.TestBoundingBox(positions,0,1,&read)&&read==0);
 assert(maxAllocated==6400&&!fullScratchCalls);
 puts("pass: actual bbox path, 20000 culling/format comparisons, no full scratch allocation, compact 6400-byte stride-64 complex cases, inclusive decode bounds and conservative allocation-failure handling");
}
'''
with tempfile.TemporaryDirectory(prefix='bbox-scratch-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-Wl,--wrap=_ZnamRKSt9nothrow_t','-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
