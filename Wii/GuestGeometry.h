// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/AddressSpace.h"
namespace Wii {
// GE decoders consume raw host pointers. Validate the entire index array and
// vertex extent before handing one out; a valid first byte is insufficient.
inline bool ValidGeometry(uint32_t vertices,uint32_t indices,unsigned count,unsigned stride,unsigned indexType) {
  if(!count||count>65536||!stride||indexType>3)return false;
  uint64_t extent=count;
  if(indexType) {
    const unsigned bytes=1u<<(indexType-1);
    const auto *p=addressSpace.Contiguous(indices,size_t(count)*bytes);
    if(!p)return false;
    unsigned maximum=0;
    for(unsigned i=0;i<count;++i) {
      uint32_t value=0;for(unsigned b=0;b<bytes;++b)value|=uint32_t(p[i*bytes+b])<<(b*8);
      // This legacy decoder represents min/max bounds with u16.
      if(value>65535)return false;
      if(value>maximum)maximum=value;
    }
    extent=uint64_t(maximum)+1;
  }
  const uint64_t size=extent*stride;
  return size<=0xffffffffULL&&addressSpace.Contiguous(vertices,size)!=nullptr;
}
}
