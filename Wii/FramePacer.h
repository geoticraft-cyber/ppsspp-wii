// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace Wii {
// Wall-clock pacing only. Every guest frame is rendered; no skip schedule.
struct FramePacer {
  double deadline=0,last=0;
  void Reset() {*this=FramePacer();}
  void Next(double now,double step,bool pause=false) {
    if(!std::isfinite(now)||!std::isfinite(step)||step<=0) {Reset();return;}
    if(pause||!deadline||now<last||now-last>1.0)deadline=now;
    last=now;deadline=std::max(deadline+step,now-step*2);
  }
};
inline FramePacer &GetFramePacer() {static FramePacer pacer;return pacer;}
}
