#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual menu pixel writer with guarded host XFBs; no VI hardware."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
video=(root/'Wii/Video.cpp').read_text()
writer=video[video.index('void WiiMenuRect('):video.index('void WiiMenuBegin()')]
with tempfile.TemporaryDirectory(prefix='menu-aspect-',dir=Path.cwd()) as temp:
    temp=Path(temp)
    source=temp/'menu.cpp'
    source.write_text(r'''
#include "Wii/Presentation.h"
#include "Wii/Pixel.h"
#include "Wii/Settings.h"
#include <vector>
#include <cassert>
#include <climits>
#include <cstdio>
#include <limits>
using u32=uint32_t;
struct Mode {unsigned fbWidth,xfbHeight;};
static Mode storage,*mode=&storage;static void *buffers[2];static unsigned backBuffer=0;
namespace Wii {Settings &GetSettings() {static Settings settings;return settings;}}
'''+writer+r'''
int main() {
  for(unsigned height:{480u,576u})for(bool wide:{false,true}) {
    storage={640,height};Wii::GetSettings().aspect=wide;
    std::vector<u32> words(640*height/2+32,0xdeadbeef);buffers[0]=words.data()+16;
    std::fill(words.begin()+16,words.end()-16,0);
    WiiMenuRect(0,0,640,480,0xffffff);
    auto view=Wii::MenuDisplayRect(640,height,wide);
    assert(view.left==(wide?80:0) && view.width==(wide?480:640));
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<640;x+=2)
      assert(words[16+(y*640+x)/2]==(x>=view.left && x<view.left+view.width?Wii::ToYUYV(0xffffff,0xffffff):0));
    for(int x:{INT_MIN,-1000,-1,0,639,640,INT_MAX})
      for(int width:{INT_MIN,-1,0,1,20,INT_MAX})WiiMenuRect(x,-10,width,INT_MAX,0x1256a3);
    for(unsigned i=0;i<16;++i)assert(words[i]==0xdeadbeef && words[words.size()-1-i]==0xdeadbeef);
    // Pointer and drawn rectangles use inverse mappings, including PAL scaling.
    for(unsigned x=0;x<640;x+=4) {
      float px,py;const float raw=view.left+(x+0.5f)*view.width/640;
      assert(Wii::MenuPointer(raw,240,wide,px,py));assert(std::abs(px-(x+0.5f))<0.001f && py==240);
    }
  }
  float x,y;assert(!Wii::MenuPointer(79,200,true,x,y) && x<0);
  assert(!Wii::MenuPointer(560,200,true,x,y) && x<0);
  assert(!Wii::MenuPointer(320,std::numeric_limits<float>::quiet_NaN(),true,x,y));
  assert(Wii::MenuPointer(80,0,true,x,y) && x==0 && y==0);
  assert(Wii::MenuPointer(0,0,false,x,y) && x==0);
  puts("pass: actual menu writer, distinct 4:3/16:9 viewports, PAL/NTSC, inverse pointer mapping, clipping extremes and XFB guards");
}
''')
    exe=temp/'menu'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(root),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
