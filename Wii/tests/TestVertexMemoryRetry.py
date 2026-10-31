#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual decode-buffer growth/reclaim code under a fixed heap budget."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/TransformUnit.cpp').read_text()
def block(start):
 a=s.index(start);b=s.index('{',a);depth=1;e=b+1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
release=block('unsigned ReleaseSoftwareVertexCaches()')
grow=block('if (needed>bufBytes ||')
code=r'''
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include "Wii/VertexCacheStorage.h"
#include "Wii/CacheAdmission.h"
#include "Wii/GraphicsWork.h"
#define PPSSPP_PLATFORM(x) 1
#define ERROR_LOG(...) ((void)0)
using u8=uint8_t;
struct {Wii::VertexCacheStorage storage;void *base=nullptr;Wii::CacheAdmission admission;}wiiVertexCaches[4];
unsigned SoftwareVertexCacheBytes(){unsigned n=0;for(auto &c:wiiVertexCaches)n+=c.storage.ResidentBytes();return n;}
namespace Wii{struct Settings{bool lowMemory=false;}settings;Settings&GetSettings(){return settings;}}
''' +release+r'''
constexpr int MEM_PROT_READ=1,MEM_PROT_WRITE=2;
unsigned attempts=0,live=0,limit=131072;bool alwaysFail=false;
void *AllocateMemoryPages(unsigned n,int){++attempts;assert(live==0);if(alwaysFail||n+SoftwareVertexCacheBytes()>limit)return nullptr;live=n;return malloc(n);}
void FreeMemoryPages(void*p,unsigned n){if(p){assert(live==n);live=0;free(p);}}
struct Transform {u8*buf=nullptr;unsigned bufBytes=0;void Grow(unsigned needed){
''' +grow+r'''
}};
int main(){
 const u8 source[16]={};u8 decoded[16368]={};
 for(auto &cache:wiiVertexCaches){cache.storage.Configure(16384);assert(cache.storage.Store(source,16,decoded,sizeof(decoded)));}
 assert(SoftwareVertexCacheBytes()==65536);
 Transform transform;transform.buf=static_cast<u8*>(malloc(65536));transform.bufBytes=live=65536;
 transform.Grow(70000);assert(transform.buf&&transform.bufBytes==131072&&attempts==2);
 assert(!SoftwareVertexCacheBytes()&&Wii::GetGraphicsWork().decodeRetries==1);
 assert(Wii::GetGraphicsWork().decodeScratchPeak==131072);
 FreeMemoryPages(transform.buf,transform.bufBytes);transform.buf=nullptr;transform.bufBytes=0;
 alwaysFail=true;attempts=0;transform.Grow(70000);assert(!transform.buf&&!transform.bufBytes&&attempts==1);
 assert(Wii::GetGraphicsWork().decodeRetries==1);
 puts("pass: actual vertex growth/reclaim code, old scratch freed before growth, bounded caches released, single successful OOM retry, peak accounting and unrecoverable failure");
}
'''
with tempfile.TemporaryDirectory(prefix='vertex-retry-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
