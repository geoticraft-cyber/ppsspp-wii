// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/VertexCacheStorage.h"
#include "Wii/DrawVertexMemo.h"
#include "Wii/WarningThrottle.h"
#include "Wii/CacheAdmission.h"
#include <cassert>
#include <vector>
#include <cstdio>
struct Vertex {unsigned value;};
int main() {
  std::vector<uint8_t> source(32*1024,17),decoded(64*1024,23),output(decoded.size());
  Wii::VertexCacheStorage cache;cache.Configure(96*1024);
  assert(cache.Store(source.data(),source.size(),decoded.data(),decoded.size()));
  assert(cache.ResidentBytes()==96*1024);
  assert(cache.Load(source.data(),source.size(),output.data(),output.size())&&output==decoded);
  const auto *borrowed=cache.Borrow(source.data(),source.size(),decoded.size());
  assert(borrowed&&memcmp(borrowed,decoded.data(),decoded.size())==0);
  assert(borrowed==cache.Borrow(source.data(),source.size(),decoded.size()));
  assert(!cache.Borrow(source.data(),source.size(),decoded.size()-1));
  source.back()^=1;assert(!cache.Borrow(source.data(),source.size(),decoded.size()));assert(!cache.Load(source.data(),source.size(),output.data(),output.size()));source.back()^=1;
  cache.Configure(64*1024);assert(cache.ResidentBytes()==0);
  assert(!cache.Store(source.data(),source.size(),decoded.data(),decoded.size()));
  assert(!cache.Load(source.data(),source.size(),output.data(),output.size()));
  assert(!cache.Store(nullptr,source.size(),decoded.data(),decoded.size()));
  cache.Configure(96*1024);assert(cache.Store(source.data(),source.size(),decoded.data(),decoded.size()));
  cache.Reset();assert(!cache.ResidentBytes());
  {
    const uint8_t oddSource[9]={};const float values[4]={1,2,3,4};
    cache.Configure(31);assert(!cache.Store(oddSource,9,values,sizeof(values)));
    cache.Configure(32);assert(cache.Store(oddSource,9,values,sizeof(values)));
    const auto *aligned=cache.Borrow(oddSource,9,sizeof(values));
    assert(aligned&&!(reinterpret_cast<uintptr_t>(aligned)&3)&&!memcmp(aligned,values,sizeof(values)));
    assert(cache.ResidentBytes()==32);
    cache.Configure(size_t(-1));assert(!cache.Store(oddSource,size_t(-1)-1,values,1));
    cache.Reset();
  }
  Wii::CacheAdmission admission;
  assert(!admission.Observe(source.data(),source.size()));
  assert(admission.Observe(source.data(),source.size()));
  for(unsigned i=0;i<100;++i){source[0]^=1;assert(!admission.Observe(source.data(),source.size()));}
  assert(admission.Observe(source.data(),source.size()));
  admission.Reset();assert(!admission.Observe(source.data(),source.size()));
  unsigned warnings=0,reported=0;
  for(unsigned i=0;i<165;++i)if(Wii::CountRepeatedWarning(warnings))++reported;
  assert(warnings==165&&reported==9);
  Wii::DrawVertexMemo<Vertex,32> memo;memo.Reset();Vertex out{};bool outside=false;
  assert(!memo.Load(7,out,outside));memo.Store(7,Vertex{42},true);
  assert(memo.Load(7,out,outside)&&out.value==42&&outside);
  outside=true;memo.Store(8,Vertex{13},false);assert(memo.Load(8,out,outside)&&outside);
  memo.Store(39,Vertex{5},false);assert(memo.Load(7,out,outside));assert(memo.Load(39,out,outside)&&out.value==5);
  memo.Reset();assert(!memo.Load(39,out,outside));
  // Four distinct indices sharing the same mixed set remain resident.
  const unsigned collision[]={0,256,512,768};
  for(auto index:collision)memo.Store(index,Vertex{index+1},false);
  for(auto index:collision)assert(memo.Load(index,out,outside)&&out.value==int(index+1));
  memo.Store(1024,Vertex{99},false);
  unsigned retained=0;for(auto index:collision)retained+=memo.Load(index,out,outside);
  assert(retained==3);assert(memo.Load(1024,out,outside)&&out.value==99);
  memo.Reset();for(auto index:collision)assert(!memo.Load(index,out,outside));
  // A 64-vertex indexed working set fits the expanded memo. The same
  // traversal churned the 32-slot memo; geometry/outside flags remain exact.
  Wii::DrawVertexMemo<Vertex> expanded;expanded.Reset();unsigned hits=0;
  for(unsigned pass=0;pass<30;++pass)for(unsigned index=0;index<64;++index) {
    bool outside=false;
    if(expanded.Load(index,out,outside)) {++hits;assert(out.value==index+10&&outside==(index==13));}
    else expanded.Store(index,Vertex{index+10},index==13);
  }
  assert(hits==64*29);
  expanded.Reset(true);for(unsigned index=0;index<64;++index)assert(!expanded.Load(index,out,outside));
  puts("pass: bounded ram cache, exact source validation, hard capacity, invalidation, transform memo culling/reset/collisions");
}
