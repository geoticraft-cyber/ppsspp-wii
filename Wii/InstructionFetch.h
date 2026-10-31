#pragma once
#include "Core/MemMap.h"
namespace Wii {
// Cache address translation only. Every fetch still reads current guest bytes,
// so self-modifying code does not need a translated-code invalidation scheme.
class InstructionFetch {
  u32 page_=0xffffffff;
  const u8 *pointer_=nullptr;
  void Resolve(u32 pc) {
    const u32 page=pc&~4095u;
    if(page!=page_) {
      page_=page;pointer_=nullptr;
      if(Memory::IsValidRange(page,4096)) {
        const u8 *p=Memory::GetPointerUnchecked(page);
        if(Memory::GetPointerUnchecked(page+4095)==p+4095)pointer_=p;
      }
    }
  }
public:
  // GE commands share the same live-word/page-translation optimization, but
  // must report invalid list addresses instead of interpreting a zero word.
  bool ReadCommand(u32 pc,u32 &word) {
    Resolve(pc);
    if(pointer_&&!(pc&3)) {
      word=*reinterpret_cast<const u32_le *>(pointer_+(pc&4095));return true;
    }
    if(!Memory::IsValidRange(pc,4))return false;
    word=Memory::ReadUnchecked_U32(pc);return true;
  }
  u32 Read(u32 pc) {
    Resolve(pc);
    if(pointer_&&!(pc&3))return *reinterpret_cast<const u32_le *>(pointer_+(pc&4095));
    return Memory::Read_U32(pc);
  }
};
}
