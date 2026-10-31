// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/DisplaySourceMemo.h"
#include <cassert>
#include <vector>
#include <cstdio>
int main(){
 Wii::DisplaySourceMemo memo;
 for(unsigned format=0;format<4;++format){
  const unsigned pitch=480*(format==3?4:2);
  std::vector<uint8_t> frame(pitch*272,0x71);
  assert(memo.Configure(format,false)&&memo.Bytes()==pitch*272);
  for(unsigned y=0;y<272;y+=4){
   const uint8_t *rows[4]={frame.data()+y*pitch,frame.data()+(y+1)*pitch,frame.data()+(y+2)*pitch,frame.data()+(y+3)*pitch};
   assert(!memo.Matches(y,rows));memo.Store(y,rows);assert(memo.Matches(y,rows));
   frame[(y+3)*pitch+pitch-1]^=1;assert(!memo.Matches(y,rows));frame[(y+3)*pitch+pitch-1]^=1;
   assert(memo.Matches(y,rows));
   assert(memo.HasBand(y)&&memo.MatchesTile(y,476,rows));
   frame[(y+3)*pitch+pitch-1]^=1;
   assert(!memo.MatchesTile(y,476,rows)&&memo.MatchesTile(y,0,rows));
   memo.StoreTile(y,476,rows);assert(memo.Matches(y,rows));
   assert(!memo.MatchesTile(y,480,rows)&&!memo.MatchesTile(y,1,rows));
   memo.Invalidate(y);assert(!memo.Matches(y,rows));
  }
  assert(!memo.Configure(format,true)&&!memo.Bytes());
 }
 assert(!memo.Configure(4,false)&&!memo.Bytes());
 puts("pass: bounded exact source memo, all formats and 68 bands, tails, uninitialized bands, low-memory release and reset");
}
