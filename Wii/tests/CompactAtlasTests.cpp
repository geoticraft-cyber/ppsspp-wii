// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/Util/ppge_atlas.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" int __lsan_is_turned_off() { return 1; }
static void Word(uint32_t bits, unsigned bytes) {
  for (unsigned i=0; i<bytes; ++i) { assert(std::putchar(bits&255)!=EOF); bits>>=8; }
}
int main() {
  // Existing unsorted and overlapping range tables must retain first-match order.
  const AtlasChar chars[]={{0,0,1,1,0,0,2,3,4},{}};
  const AtlasCharRange ranges[]={{50,51,1},{10,11,0},{50,51,0}};
  const AtlasFont legacy={0,0,0,0,chars,ranges,3,"legacy"};
  assert(legacy.getChar(10)==&chars[0]);
  assert(!legacy.getChar(50) && !legacy.getChar(9));
  const AtlasFont *font=ppge_atlas.fonts[0];
  assert(font->charIndex && font->charIndexCount==65534);
  assert(!font->getChar(INT_MIN) && !font->getChar(INT_MAX));
  for(int cp=-256;cp<=0x110100;++cp) {
    const AtlasChar *glyph=font->getChar(cp);
    if(cp<0 || cp>=font->charIndexCount) assert(!glyph);
    if(cp<0 || cp>65535) continue;
    const AtlasChar zero={};
    if(!glyph) glyph=&zero;
    const float fields[]={glyph->sx,glyph->sy,glyph->ex,glyph->ey,glyph->ox,glyph->oy,glyph->wx};
    for(float value:fields) { uint32_t bits;std::memcpy(&bits,&value,4);Word(bits,4); }
    Word(glyph->pw,2);Word(glyph->ph,2);
  }
}
