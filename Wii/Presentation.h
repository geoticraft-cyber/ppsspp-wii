// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace Wii {
struct PresentationRect { unsigned left,top,width,height; };
// aspect is the television's physical display shape, not the framebuffer's
// storage width/height. PSP widescreen fills a 16:9 TV and is letterboxed on 4:3.
inline PresentationRect DisplayRect(unsigned width,unsigned height,bool widescreen) {
  const unsigned visible=widescreen?height:height*3/4;
  return {0,(height-visible)/2,width,visible};
}
// A 4:3 menu canvas must occupy 3/4 of a 16:9 television's width.
// Keep XFB boundaries even for packed YUYV pixels.
inline PresentationRect MenuDisplayRect(unsigned width,unsigned height,bool widescreen) {
  const unsigned visible=(widescreen?unsigned(uint64_t(width)*3/4):width)&~1u;
  return {((width-visible)/2)&~1u,0,visible,height};
}
inline bool MenuPixelRect(int x,int y,int w,int h,unsigned width,unsigned height,bool widescreen,PresentationRect &out) {
  if(w<=0||h<=0)return false;
  const int64_t x0=std::max<int64_t>(0,x),y0=std::max<int64_t>(0,y);
  const int64_t x1=std::min<int64_t>(640,int64_t(x)+w),y1=std::min<int64_t>(480,int64_t(y)+h);
  if(x0>=x1||y0>=y1)return false;
  const auto view=MenuDisplayRect(width,height,widescreen);
  const unsigned left=view.left+uint64_t(x0)*view.width/640,top=uint64_t(y0)*view.height/480;
  const unsigned right=view.left+uint64_t(x1)*view.width/640,bottom=uint64_t(y1)*view.height/480;
  out={left,top,right-left,bottom-top};return out.width&&out.height;
}
// WPAD's input surface remains 640x480. Undo the same viewport transform.
inline bool MenuPointer(float screenX,float screenY,bool widescreen,float &x,float &y) {
  const auto view=MenuDisplayRect(640,480,widescreen);
  if(!std::isfinite(screenX)||!std::isfinite(screenY)||screenX<view.left||screenX>=view.left+view.width||screenY<0||screenY>=480) {
    x=y=-100;return false;
  }
  x=(screenX-view.left)*640/view.width;y=screenY;return true;
}
}
