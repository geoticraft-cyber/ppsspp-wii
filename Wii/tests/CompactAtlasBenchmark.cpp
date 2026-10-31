// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfx/texture_atlas.h"
#include "Core/Util/ppge_atlas.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <climits>
extern "C" const AtlasChar *UploadedGetChar(int);
int main() {
 const auto font=ppge_atlas.fonts[0];
 for(int cp=-256;cp<=0x110100;++cp){auto a=font->getChar(cp),b=UploadedGetChar(cp);assert(bool(a)==bool(b));if(a)assert(!memcmp(a,b,sizeof(*a)));}
 assert(!font->getChar(INT_MIN)&&!font->getChar(INT_MAX));
 puts("pass: exhaustive unicode glyph presence and exact payload equivalence");
 unsigned sum=0;
 for(int mode=0;mode<3;++mode)for(int trial=0;trial<7;++trial)for(int slot=0;slot<2;++slot){bool updated=(trial+slot)&1;auto start=std::chrono::steady_clock::now();
 for(unsigned n=0;n<2000000;++n){int cp=mode==0?32+n%95:mode==1?19968+(n*17u)%20992:(n*7919u)%65536;auto c=updated?font->getChar(cp):UploadedGetChar(cp);if(c)sum+=c->pw;}
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();printf("%d,%d,%s,%.6f\n",mode,trial,updated?"updated":"uploaded",ms);
 }printf("checksum %u\n",sum);
}
