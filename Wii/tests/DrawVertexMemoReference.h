// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace PreviousDrawMemo {
// Per-draw, four-way transform memo. Reset for every primitive submission:
// GE matrices, lighting and decoder state cannot leak between submissions.
template<class Vertex, unsigned Slots = 128> class DrawVertexMemo {
  static_assert(Slots >= 4 && !(Slots & (Slots - 1)), "power-of-two slot count");
  struct Entry { Vertex vertex; uint32_t index; bool outside; } entries_[Slots];
  uint32_t valid_[(Slots+31)/32]={};
  unsigned active_=Slots;
  unsigned next_[Slots/4]={};
  unsigned Set(uint32_t index) const {return ((index ^ (index>>5) ^ (index>>13)) & (active_/4-1))*4;}

public:
  void Reset(bool lowMemory=false) {
    active_=lowMemory&&Slots>32?32:Slots;
    for(auto &word:valid_)word=0;
    for(auto &way:next_)way=0;
  }
  bool Load(uint32_t index, Vertex &vertex, bool &outside) const {
    const unsigned set=Set(index);
    for(unsigned slot=set;slot<set+4;++slot) {
      if (!(valid_[slot/32] & (1u << (slot&31))) || entries_[slot].index != index) continue;
      vertex = entries_[slot].vertex;
      outside = outside || entries_[slot].outside;
      return true;
    }
    return false;
  }
  void Store(uint32_t index, const Vertex &vertex, bool outside) {
    const unsigned set=Set(index);
    unsigned slot=set+(next_[set/4]++&3);
    for(unsigned i=set;i<set+4;++i)if(!(valid_[i/32]&(1u<<(i&31)))||entries_[i].index==index) {slot=i;break;}
    entries_[slot].vertex = vertex; entries_[slot].index = index; entries_[slot].outside = outside;
    valid_[slot/32] |= 1u << (slot&31);
  }
};
}
