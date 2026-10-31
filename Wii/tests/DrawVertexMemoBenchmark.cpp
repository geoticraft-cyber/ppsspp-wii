// SPDX-License-Identifier: GPL-2.0-or-later
#include "GPU/Software/TransformUnit.h"
#include "Wii/tests/DrawVertexMemoReference.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <cassert>
// Use the actual vertex payload. Both versions run in one process through the
// same timed loop; helper calls are not inlined into a synthetic workload.
template<class Memo> struct Access {
  static Memo memo;
  __attribute__((noinline)) static void Reset(bool low) {memo.Reset(low);}
  __attribute__((noinline)) static bool Load(unsigned i,VertexData &v,bool &out) {return memo.Load(i,v,out);}
  __attribute__((noinline)) static void Store(unsigned i,const VertexData &v,bool out) {memo.Store(i,v,out);}
};
template<class Memo> Memo Access<Memo>::memo;
using Previous=Access<PreviousDrawMemo::DrawVertexMemo<VertexData>>;
using Updated=Access<Wii::DrawVertexMemo<VertexData>>;
using Reset=void(*)(bool);
using Load=bool(*)(unsigned,VertexData&,bool&);
using Store=void(*)(unsigned,const VertexData&,bool);
struct Result {double ms;unsigned checksum,hits;};
__attribute__((noinline,noclone)) static Result Run(unsigned mode,const std::vector<unsigned> &collision,Reset reset,Load load,Store store) {
  VertexData v{};bool outside=false;unsigned checksum=0,hits=0;
  const auto start=std::chrono::steady_clock::now();
  for(unsigned draw=0;draw<100000;++draw) {
    reset(mode==3);
    for(unsigned n=0;n<128;++n) {
      const unsigned index=mode==1?n:mode==2?collision[n%collision.size()]:n%32;
      v.color0.x=int(index);
      if(load(index,v,outside))++hits;else store(index,v,false);
      checksum+=unsigned(v.color0.x);
    }
  }
  return {std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),checksum,hits};
}
int main() {
  std::vector<unsigned> collision;
  for(unsigned i=0;collision.size()<12;++i)if(!((i^(i>>5)^(i>>13))&31))collision.push_back(i);
  const char *modes[]={"reused","unique","collisions","low-memory"};
  puts("mode,round,version,milliseconds,checksum,memo_bytes,hits");
  for(unsigned mode=0;mode<4;++mode) {
    Result reference{};
    for(unsigned trial=0;trial<7;++trial)for(unsigned slot=0;slot<2;++slot) {
      const bool updated=(trial+slot)&1;
      const auto r=Run(mode,collision,updated?Updated::Reset:Previous::Reset,updated?Updated::Load:Previous::Load,updated?Updated::Store:Previous::Store);
      if(!trial&&!slot)reference=r;
      assert(reference.checksum==r.checksum&&reference.hits==r.hits);
      printf("%s,%u,%s,%.6f,%u,%zu,%u\n",modes[mode],trial,updated?"updated":"previous",r.ms,r.checksum,updated?sizeof(Updated::memo):sizeof(Previous::memo),r.hits);
      fflush(stdout);
    }
  }
}
