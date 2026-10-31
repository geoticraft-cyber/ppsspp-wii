#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Core/HLE/sceDisplay.cpp').read_text();a=s.index('#if PPSSPP_PLATFORM(WII)',s.index('static void DoFrameTiming('));b=s.index('#else',a);body=s[a:].split('\n',1)[1][:b-a-len(s[a:].split('\n',1)[0])-1]
# Explicit delimiter avoids nesting assumptions in unrelated desktop timing.
body=s[s.index('\n',a)+1:b]
with tempfile.TemporaryDirectory(prefix='frame-timing-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text('''#include "Wii/FramePacer.h"
#include "Wii/Settings.h"
#include <cassert>
#include <cstdio>
struct {int iFrameSkip;}g_Config;
bool wasPaused=false;double clockNow=1;unsigned waits=0;
void time_update(){} double time_now_d(){return clockNow;}
void usleep(long us){++waits;clockNow+=(us>0?us:1)/1000000.0;}
static void Timing(bool &throttle,bool &skipFrame,float timestep){
''' +body+'''
}
int main(){
 auto &p=Wii::GetFramePacer();
 {
  p.Reset();clockNow=1;bool skip=false,throttle=true;
  for(unsigned n=0;n<300;++n){clockNow+=0.001;Timing(throttle,skip,1.0f/60);assert(!throttle&&!skip);}
  const double expected=1+299.0/60;assert(std::abs(clockNow-expected)<0.01);
 }
 assert(waits>0);p.Reset();
 for(unsigned n=0;n<100;++n){bool skip,throttle;clockNow+=0.05;Timing(throttle,skip,1.0f/60);assert(!skip);}
 wasPaused=true;clockNow+=10;bool skip,throttle;Timing(throttle,skip,1.0f/60);assert(!skip);
 puts("pass: actual wii timing branch: normal wall-clock pacing, slow frames and pause; every frame rendered");
}
''');exe=p/'test';subprocess.run(['g++','-std=c++11','-O2','-I'+str(root),str(cpp),str(root/'Wii/Settings.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe.resolve())],check=True)
