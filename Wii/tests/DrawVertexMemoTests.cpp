// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/DrawVertexMemo.h"
#include "Wii/tests/DrawVertexMemoReference.h"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Vertex {uint32_t data[32];};
extern "C" int __lsan_is_turned_off() {return 1;}
static uint32_t seed=0x91b732e5;
static uint32_t Random() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
template<unsigned Slots> static void Check() {
  Wii::DrawVertexMemo<Vertex,Slots> updated;
  PreviousDrawMemo::DrawVertexMemo<Vertex,Slots> previous;
  auto load=[&](uint32_t index) {
    Vertex a,b;for(auto &v:a.data)v=Random();b=a;
    bool outsideA=(Random()&1)!=0,outsideB=outsideA;
    const bool hitA=updated.Load(index,a,outsideA),hitB=previous.Load(index,b,outsideB);
    assert(hitA==hitB&&outsideA==outsideB&&!memcmp(&a,&b,sizeof(a)));
  };
  for(unsigned phase=0;phase<4;++phase) {
    const bool low=phase&1;updated.Reset(low);previous.Reset(low);
    // More than 256 stores exercise byte counter wrap and full-set eviction.
    for(unsigned n=0;n<2048;++n) {
      Vertex v;for(auto &word:v.data)word=Random();
      const unsigned index=(n%7)*8192;
      updated.Store(index,v,n&1);previous.Store(index,v,n&1);
      for(unsigned i=0;i<7;++i)load(i*8192);
    }
    for(unsigned n=0;n<100000;++n) {
      const unsigned choice=Random(),index=Random()%1024;
      if((choice&1023)==0) {updated.Reset(low);previous.Reset(low);}
      else if(choice&1) {
        Vertex v;for(auto &word:v.data)word=Random();
        const bool outside=Random()&1;
        updated.Store(index,v,outside);previous.Store(index,v,outside);
      } else load(index);
    }
    for(unsigned index=0;index<1024;++index)load(index);
  }
}
int main() {
  static_assert(sizeof(PreviousDrawMemo::DrawVertexMemo<Vertex>)-sizeof(Wii::DrawVertexMemo<Vertex>)==592,"compact default memo storage");
  Check<4>();Check<8>();Check<32>();Check<128>();Check<256>();
  puts("pass: vertex memo matches previous hits, eviction, payloads, misses, culling flags, reset, counter wrap and low-memory mode across five capacities");
}
