// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace Wii {
// Bounded one-window cache; the caller owns locking and the actual I/O.
// Request history is independent of cache hits: consecutive guest reads grow
// read-ahead, while random probes restart at 8 KiB. Never exceeds the existing cap.
class ReadAheadPolicy {
  uint64_t next_=~uint64_t(0);
  size_t window_=8192;
public:
  size_t Observe(uint64_t at,size_t length,size_t capacity) {
    if(at==next_) window_=window_>capacity/2?capacity:window_*2;
    else window_=std::min<size_t>(8192,capacity);
    next_=at+length;
    return std::min(capacity,std::max(window_,length));
  }
  void Reset() {next_=~uint64_t(0);window_=8192;}
};
class ReadCache {
public:
  // 64 KiB normally; USB is capped at 128 KiB (64 KiB in low-memory mode).
  size_t capacity=64*1024;
  uint64_t begin=0;size_t valid=0;uint8_t *data=nullptr;
  bool Contains(uint64_t at,size_t bytes) const {return data&&at>=begin&&at-begin<=valid&&bytes<=valid-size_t(at-begin);}
  void Copy(uint64_t at,size_t bytes,void *out) const {memcpy(out,data+size_t(at-begin),bytes);}
};
}
