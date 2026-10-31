// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Wii {
inline uint32_t DecodePixel(const uint8_t *p, unsigned format) {
  if (format == 3)
    return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
  const uint16_t v = uint16_t(p[0]) | uint16_t(p[1]) << 8;
  unsigned r, g, b;
  if (format == 0) {
    r=v&31;g=(v>>5)&63;b=v>>11;
    r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);
  } else if (format == 1) {
    r=v&31;g=(v>>5)&31;b=(v>>10)&31;
    r=(r<<3)|(r>>2);g=(g<<3)|(g>>2);b=(b<<3)|(b>>2);
  } else {
    r = (v & 15) * 17; g = ((v >> 4) & 15) * 17; b = ((v >> 8) & 15) * 17;
  }
  return r << 16 | g << 8 | b;
}
inline unsigned ClampByte(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }
inline uint32_t FromYUYV(uint32_t pair, bool right) {
  const int y = int(right ? (pair >> 8) & 255 : pair >> 24) - 16;
  const int u = int((pair >> 16) & 255) - 128, v = int(pair & 255) - 128;
  return ClampByte((298*y+409*v+128)>>8)<<16 |
    ClampByte((298*y-100*u-208*v+128)>>8)<<8 | ClampByte((298*y+516*u+128)>>8);
}
inline uint32_t ToYUYV(uint32_t a, uint32_t b) {
  const int r0 = (a >> 16) & 255, g0 = (a >> 8) & 255, b0 = a & 255;
  const int r1 = (b >> 16) & 255, g1 = (b >> 8) & 255, b1 = b & 255;
  const int r = (r0 + r1) / 2, g = (g0 + g1) / 2, blue = (b0 + b1) / 2;
  const unsigned y0 = ClampByte(((66*r0 + 129*g0 + 25*b0 + 128) >> 8) + 16);
  const unsigned y1 = ClampByte(((66*r1 + 129*g1 + 25*b1 + 128) >> 8) + 16);
  const unsigned u = ClampByte(((-38*r - 74*g + 112*blue + 128) >> 8) + 128);
  const unsigned v = ClampByte(((112*r - 94*g - 18*blue + 128) >> 8) + 128);
  return y0 << 24 | u << 16 | y1 << 8 | v;
}
}
