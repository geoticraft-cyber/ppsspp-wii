#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual skin matrix method bodies, including memo hits and bone changes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
def extract(s):
 a=s.index('void VertexDecoder::ComputeSkinMatrix(');b=s.index('{',a);e=b+1;depth=1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
s=(root/'GPU/Common/VertexDecoderCommon.cpp').read_text()
start=s.index('void VertexDecoder::DecodeVerts(')
assert 'skinMemo.Reset();' in s[start:start+350], 'memo must reset before decoding any new batch'
code=r'''
#include "Wii/SkinMatrixMemo.h"
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#define PPSSPP_PLATFORM(x) 1
using std::memset;
struct State {float boneMatrix[96];}gstate;
static float skinMatrix[12];static Wii::SkinMatrixMemo skinMemo;
struct VertexDecoder {int nweights;void ComputeSkinMatrix(const float weights[8])const;};
struct PreviousDecoder {int nweights;void ComputeSkinMatrix(const float weights[8])const;};
'''+(root/'Wii/tests/SkinMatrixReference.h').read_text()+extract(s)+r'''
int main(){
 uint32_t seed=0x891234;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned cases=0;
 for(unsigned batch=0;batch<400;++batch){
  skinMemo.Reset();unsigned count=1+batch%8;
  VertexDecoder current{int(count)};PreviousDecoder previous{int(count)};
  for(auto &v:gstate.boneMatrix)v=(int(random()%33)-16)*0.125f;
  float weights[8]={};
  for(unsigned vertex=0;vertex<100;++vertex){
   if(vertex%5==0)for(unsigned j=0;j<count;++j)weights[j]=float(random()%17)*0.125f;
   previous.ComputeSkinMatrix(weights);float expected[12];memcpy(expected,skinMatrix,sizeof(expected));
   memset(skinMatrix,0x67,sizeof(skinMatrix));current.ComputeSkinMatrix(weights);
   assert(!memcmp(expected,skinMatrix,sizeof(expected)));++cases;
  }
 }
 printf("pass: %u actual preceding/current skin matrices, all bone counts, repeated/changing weights, changed bones and fresh batches\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='skin-matrix-',dir=Path.cwd()) as d:
 cpp=Path(d)/'test.cpp';cpp.write_text(code);exe=Path(d)/'test'
 subprocess.run(['g++','-std=c++11','-O3','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
