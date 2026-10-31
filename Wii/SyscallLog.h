// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
// Bounded, non-evicting set: collisions must not turn a one-time description
// into disk logging on every audio/thread syscall. Full means stop describing.
class SyscallLogSet {
  uint32_t entries[256]={};
  unsigned count=0;
public:
  void Clear() {count=0;for(auto &entry:entries)entry=0;}
  bool First(uint32_t opcode) {
    if(!opcode||count==256)return false;
    const unsigned start=((opcode>>6)*2654435761u)>>24;
    for(unsigned probe=0;probe<256;++probe) {
      auto &entry=entries[(start+probe)&255];
      if(entry==opcode)return false;
      if(!entry) {entry=opcode;++count;return true;}
    }
    return false;
  }
};
}
