#pragma once
#include <cstdint>
#include <cmath>
namespace Wii {
// Preserve stick direction through the dead zone, including diagonals.
inline void RadialDeadzone(float &x,float &y) {
  if(!std::isfinite(x)||!std::isfinite(y)) {x=y=0;return;}
  const float length=std::sqrt(x*x+y*y);
  if(length<=0.12f) {x=y=0;return;}
  const float magnitude=std::fmin(1.0f,(length-0.12f)/0.88f);
  x=x/length*magnitude;y=y/length*magnitude;
}
inline void PolarStick(float magnitude,float degrees,float &x,float &y) {
  if(!std::isfinite(magnitude)||!std::isfinite(degrees)||magnitude<=0) {x=y=0;return;}
  const float angle=degrees*(3.141592654f/180.0f);
  magnitude=std::fmin(magnitude,1.0f);
  x=magnitude*std::sin(angle);y=magnitude*std::cos(angle);
  RadialDeadzone(x,y);
}
// Wii Remote uses upright D-pad. Without an analog attachment it also drives
// the PSP analog stick, allowing analog-only games to be played.
inline void AddDpadAnalog(bool up,bool down,bool left,bool right,float &x,float &y) {
  if(x==0)x=float(right)-float(left);
  if(y==0)y=float(up)-float(down);
}
}
