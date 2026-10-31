#include "Wii/FramePacer.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
  Wii::FramePacer p;
  p.Next(1,1.0/60);assert(p.deadline>1&&p.deadline<1.02);
  for(unsigned n=1;n<600;++n)p.Next(1+n/60.0,1.0/60);
  assert(p.deadline>10.99&&p.deadline<11.01);
  p.Next(100,1.0/60,true);assert(p.deadline>100&&p.deadline<100.02);
  p.Next(1,1.0/60);assert(p.deadline>1&&p.deadline<1.02);
  p.Next(1.5,1.0/60);assert(p.deadline>=1.5-2.0/60);
  p.Next(2,0);assert(!p.deadline);
  p.Next(std::numeric_limits<double>::quiet_NaN(),1.0/60);assert(!p.deadline);
  p.Reset();assert(!p.deadline&&!p.last);
  puts("pass: pacing deadline, bounded debt, pause, clock reversal and invalid input; no frame dropping");
}
