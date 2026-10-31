#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
video=(root/'Wii/Video.cpp').read_text();a=video.index('static GXRModeObj *RequestedMode');b=video.index('static bool ConfigureMode',a)
main=(root/'Wii/Main.cpp').read_text();c=main.index('static void AdjustSetting');d=main.index('static bool TestVideoMode',c)
with tempfile.TemporaryDirectory(prefix='video-modes-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text('''#include "Wii/Settings.h"
#include <cassert>
#include <cstdio>
struct GXRModeObj {int id;};
GXRModeObj TVNtsc480IntDf{1},TVNtsc480Prog{2},TVPal576ProgScale{4};
bool component=false;
bool VIDEO_HaveComponentCable(){return component;}
''' +video[a:b]+main[c:d]+'''
int main(){
 for(bool cable:{false,true}){
  component=cable;assert(RequestedMode(0)->id==(cable?2:1));
  assert(WiiVideoModeSupported(1));assert(WiiVideoModeSupported(2)==cable);assert(WiiVideoModeSupported(4)==cable);
  assert(!WiiVideoModeSupported(3));assert(!WiiVideoModeSupported(-1)&&!WiiVideoModeSupported(5));
 }
 auto &s=Wii::GetSettings();s.video=0;
 for(int expected:{1,2,4,0}){AdjustSetting(1,1);assert(s.video==expected);}
 for(int expected:{4,2,1,0}){AdjustSetting(1,-1);assert(s.video==expected);}
 bool old=s.cpuSpriteCache;AdjustSetting(12,1);assert(s.cpuSpriteCache!=old);
 puts("pass: actual video selector and settings adjustments: no PAL interlace, auto 480i60/480p, cable checks, retained IDs, experimental toggles");
}
''');exe=p/'test';subprocess.run(['g++','-std=c++11','-O2','-I'+str(root),str(cpp),str(root/'Wii/Settings.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe.resolve())],check=True)
