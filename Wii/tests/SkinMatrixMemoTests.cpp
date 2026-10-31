// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/SkinMatrixMemo.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 Wii::SkinMatrixMemo memo;unsigned reused=0,computed=0;
 for(unsigned batch=0;batch<200;++batch){
  memo.Reset();const unsigned count=1+batch%8;
  float weights[8]={},bone[96],expected[12],actual[12];
  for(unsigned i=0;i<96;++i)bone[i]=(int(i+batch)%19-9)*0.125f;
  for(unsigned vertex=0;vertex<40;++vertex){
   for(unsigned j=0;j<count;++j)weights[j]=float((vertex/5+j)%4)*0.25f;
   std::memset(expected,0,sizeof(expected));
   for(unsigned j=0;j<count;++j)if(weights[j]!=0)for(unsigned i=0;i<12;++i)expected[i]+=weights[j]*bone[j*12+i];
   if(memo.Load(weights,count,actual))++reused;
   else{++computed;std::memcpy(actual,expected,sizeof(actual));memo.Store(weights,count,actual);}
   assert(!std::memcmp(expected,actual,sizeof(actual)));
  }
  memo.Reset();assert(!memo.Load(weights,count,actual));
  // Distinct signed-zero/NaN payloads are keys, never numerical equality.
  unsigned bits=0x80000000;std::memcpy(weights,&bits,4);memo.Store(weights,count,actual);
  bits=0;std::memcpy(weights,&bits,4);assert(!memo.Load(weights,count,actual));
 }
 assert(reused==6400&&computed==1600);
 printf("pass: 8000 skin matrices, %u exact reuses; all bone counts, weight changes, batch resets and bitwise keys\n",reused);
}
