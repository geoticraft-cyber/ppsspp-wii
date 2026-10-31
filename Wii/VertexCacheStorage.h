// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
namespace Wii {
// Bounded disposable RAM cache. Every hit compares the complete source bytes.
class VertexCacheStorage {
  std::unique_ptr<uint8_t[]> ram_;
  size_t capacity_=0,limit_=192*1024,sourceBytes_=0,decodedBytes_=0;
public:
  void Invalidate() {sourceBytes_=decodedBytes_=0;}
  void Reset() {Invalidate();ram_.reset();capacity_=0;}
  void Configure(size_t limit) {limit_=limit;if(capacity_>limit)Reset();}
  size_t ResidentBytes() const {return capacity_;}
  bool Failed() const {return false;}
  // Borrowed data stays valid until Store/Reset/Configure mutates this entry.
  // The draw owns no cache writes after choosing its immutable vertex stream.
  const uint8_t *Borrow(const void *source,size_t sourceBytes,size_t decodedBytes) const {
    if(!source||!ram_||!sourceBytes||sourceBytes!=sourceBytes_||decodedBytes!=decodedBytes_)return nullptr;
    if(memcmp(ram_.get(),source,sourceBytes))return nullptr;
    return ram_.get()+((sourceBytes+15)&~size_t(15));
  }
  bool Load(const void *source,size_t sourceBytes,void *decoded,size_t decodedBytes) {
    const auto *stored=Borrow(source,sourceBytes,decodedBytes);
    if(!stored||!decoded)return false;
    memcpy(decoded,stored,decodedBytes);return true;
  }
  bool Store(const void *source,size_t sourceBytes,const void *decoded,size_t decodedBytes) {
    Invalidate();
    if(!source||!decoded||!sourceBytes||!decodedBytes||sourceBytes>limit_||decodedBytes>limit_-sourceBytes)return false;
    // Source strides need not be aligned. Borrowed decoded floats/shorts
    // must retain alignment, and padding is charged against the same cap.
    if(sourceBytes>size_t(-1)-15)return false;
    const size_t offset=(sourceBytes+15)&~size_t(15);
    if(offset>limit_||decodedBytes>limit_-offset)return false;
    const size_t total=offset+decodedBytes;
    if(total>capacity_) {
      Reset();ram_.reset(new(std::nothrow) uint8_t[total]);if(!ram_)return false;capacity_=total;
    }
    memcpy(ram_.get(),source,sourceBytes);memcpy(ram_.get()+offset,decoded,decodedBytes);
    sourceBytes_=sourceBytes;decodedBytes_=decodedBytes;return true;
  }
};
}
