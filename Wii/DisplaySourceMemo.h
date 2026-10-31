// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
namespace Wii {
// Exact raw scanout bytes, bounded to one 480x272 RGBA framebuffer. No hashes
// or guest write tracking. Skips conversion only while the shared GX texture
// is known to contain this presentation. Low-memory mode releases the shadow.
class DisplaySourceMemo {
  std::unique_ptr<uint8_t[]> data_;
  unsigned bytes_=0,format_=4;
  uint32_t valid_[3]={};
public:
  void Reset() {data_.reset();bytes_=0;format_=4;for(auto &word:valid_)word=0;}
  unsigned Format() const {return format_;}
  unsigned Bytes() const {return bytes_;}
  bool Configure(unsigned format,bool lowMemory) {
    if(lowMemory||format>3) {Reset();return false;}
    const unsigned needed=480*272*(format==3?4:2);
    if(data_&&format_==format)return true;
    Reset();data_.reset(new(std::nothrow) uint8_t[needed]);
    if(!data_)return false;
    bytes_=needed;format_=format;return true;
  }
  bool HasBand(unsigned y) const {
    return data_&&y<=268&&!(y&3)&&(valid_[y/128]&(1u<<((y/4)&31)));
  }
  bool MatchesTile(unsigned y,unsigned x,const uint8_t *const rows[4]) const {
    if(!HasBand(y)||x>476||(x&3))return false;
    const unsigned bpp=format_==3?4:2,pitch=480*bpp;
    for(unsigned row=0;row<4;++row)
      if(!rows[row]||memcmp(data_.get()+(y+row)*pitch+x*bpp,rows[row]+x*bpp,4*bpp))return false;
    return true;
  }
  void StoreTile(unsigned y,unsigned x,const uint8_t *const rows[4]) {
    if(!HasBand(y)||x>476||(x&3))return;
    const unsigned bpp=format_==3?4:2,pitch=480*bpp;
    for(unsigned row=0;row<4;++row)
      memcpy(data_.get()+(y+row)*pitch+x*bpp,rows[row]+x*bpp,4*bpp);
  }
  bool Matches(unsigned y,const uint8_t *const rows[4]) const {
    if(!data_||y>268||(y&3)||!(valid_[y/128]&(1u<<((y/4)&31))))return false;
    const unsigned pitch=480*(format_==3?4:2);
    for(unsigned row=0;row<4;++row)
      if(!rows[row]||memcmp(data_.get()+(y+row)*pitch,rows[row],pitch))return false;
    return true;
  }
  void Invalidate(unsigned y) {if(y<=268&&!(y&3))valid_[y/128]&=~(1u<<((y/4)&31));}
  void Store(unsigned y,const uint8_t *const rows[4]) {
    if(!data_||y>268||(y&3))return;
    const unsigned pitch=480*(format_==3?4:2);
    for(unsigned row=0;row<4;++row)memcpy(data_.get()+(y+row)*pitch,rows[row],pitch);
    valid_[y/128]|=1u<<((y/4)&31);
  }
};
}
