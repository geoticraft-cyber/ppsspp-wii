// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Wii/AddressSpace.h"
#include <cstring>
#include <algorithm>
namespace Wii {
inline bool CopyGuest(uint32_t dst,uint32_t src,size_t bytes) {
  if (!bytes) return true;
  if (!addressSpace.IsRangeValid(dst,bytes) || !addressSpace.IsRangeValid(src,bytes)) return false;
  while(bytes) {
    size_t n=std::min(bytes,std::min(addressSpace.Span(dst),addressSpace.Span(src)));
    if(!n)return false;
    memmove(addressSpace.Translate(dst),addressSpace.Translate(src),n);bytes-=n;dst+=n;src+=n;
  }
  return true;
}
// GE packed transfers may use one copy only when host backing cannot overlap.
// Aliases, mirrors, strides and failures keep the original sequential row order.
inline bool CopyGuestRows(uint32_t dst,uint32_t src,unsigned dstPitch,
    unsigned srcPitch,unsigned rowBytes,unsigned rows) {
  const uint64_t total=uint64_t(rowBytes)*rows;
  if(rows>1&&dstPitch==rowBytes&&srcPitch==rowBytes&&total&&
      total<=0xffffffffu&&uint64_t(dst)+total<=0x100000000ULL&&
      uint64_t(src)+total<=0x100000000ULL) {
    auto *d=addressSpace.Contiguous(dst,size_t(total));
    const auto *s=addressSpace.Contiguous(src,size_t(total));
    const uintptr_t da=reinterpret_cast<uintptr_t>(d),sa=reinterpret_cast<uintptr_t>(s);
    if(d&&s&&(da<=sa?sa-da>=total:da-sa>=total)) {
      memcpy(d,s,size_t(total));return true;
    }
  }
  bool valid=true;
  for(unsigned row=0;row<rows;++row)
    if(!CopyGuest(dst+uint32_t(uint64_t(row)*dstPitch),
        src+uint32_t(uint64_t(row)*srcPitch),rowBytes))valid=false;
  return valid;
}
inline bool CopyFromGuest(void *dst,uint32_t src,size_t bytes) {
  if (!bytes) return true;
  if (!dst || !addressSpace.IsRangeValid(src,bytes)) return false;
  auto out=static_cast<uint8_t *>(dst);
  while(bytes) {
    size_t n=std::min(bytes,addressSpace.Span(src));if(!n)return false;
    memcpy(out,addressSpace.Translate(src),n);bytes-=n;out+=n;src+=n;
  }
  return true;
}
inline bool CopyToGuest(uint32_t dst,const void *src,size_t bytes) {
  if (!bytes) return true;
  if (!src || !addressSpace.IsRangeValid(dst,bytes)) return false;
  auto in=static_cast<const uint8_t *>(src);
  while(bytes) {
    const size_t n=std::min(bytes,addressSpace.Span(dst));
    memcpy(addressSpace.Translate(dst),in,n);bytes-=n;in+=n;dst+=n;
  }
  return true;
}
}
