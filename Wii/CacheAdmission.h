// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace Wii {
// A fingerprint only controls whether it is worth saving a fresh decode.
// It never authorizes a hit: stored source bytes must still compare exactly.
class CacheAdmission {
  uint32_t previous_=0;
  bool seen_=false;
public:
  void Reset() {seen_=false;}
  bool Observe(const void *source,size_t bytes) {
    const auto *p=static_cast<const uint8_t *>(source);uint32_t hash=2166136261u;
    for(size_t i=0;i<bytes;++i)hash=(hash^p[i])*16777619u;
    const bool stable=seen_&&hash==previous_;
    previous_=hash;seen_=true;return stable;
  }
};
}
