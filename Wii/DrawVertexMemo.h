// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
// Per-draw, four-way transform memo. Reset for every primitive submission:
// GE matrices, lighting and decoder state cannot leak between submissions.
template<class Vertex, unsigned Slots = 128> class DrawVertexMemo {
  static_assert(Slots >= 4 && !(Slots & (Slots - 1)), "power-of-two slot count");
  // Keep probe keys contiguous instead of striding over the vertex payload.
  Vertex vertices_[Slots];
  uint32_t indices_[Slots];
  uint32_t outside_[(Slots+31)/32]={};
  uint32_t valid_[(Slots+31)/32]={};
  unsigned setMask_=Slots/4-1;
  // Only the low two bits select a way; byte wrap preserves round-robin order.
  uint8_t next_[Slots/4]={};
  unsigned Set(uint32_t index) const {return ((index ^ (index>>5) ^ (index>>13)) & setMask_)*4;}

public:
  void Reset(bool lowMemory=false) {
    setMask_=lowMemory&&Slots>32?7:Slots/4-1;
    for(auto &word:valid_)word=0;
    for(auto &way:next_)way=0;
  }
  bool Load(uint32_t index, Vertex &vertex, bool &outside) const {
    const unsigned set=Set(index);
    for(unsigned slot=set;slot<set+4;++slot) {
      if (!(valid_[slot/32] & (1u << (slot&31))) || indices_[slot] != index) continue;
      vertex = vertices_[slot];
      outside = outside || (outside_[slot/32] & (1u << (slot&31)));
      return true;
    }
    return false;
  }
  void Store(uint32_t index, const Vertex &vertex, bool outside) {
    const unsigned set=Set(index);
    unsigned slot=set+(next_[set/4]++&3);
    for(unsigned i=set;i<set+4;++i)if(!(valid_[i/32]&(1u<<(i&31)))||indices_[i]==index) {slot=i;break;}
    vertices_[slot] = vertex; indices_[slot] = index;
    const uint32_t bit=1u << (slot&31);
    // Invalid entries are never read, so Reset need not clear outside_.
    outside_[slot/32] = (outside_[slot/32] & ~bit) | (outside ? bit : 0);
    valid_[slot/32] |= 1u << (slot&31);
  }
};
}
