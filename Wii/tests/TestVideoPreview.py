#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual timed preview cancel/exit and failed-restore fallback."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Main.cpp').read_text();a=s.index('static bool TestVideoMode(');b=s.index('static void SettingsScreen(',a)
code=r'''
#include "Wii/Settings.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdint>
using u32=unsigned;
static bool quitRequested=false,unsupported=false;static unsigned buttons=0,pad=0,scans=0;static int applied=1;
static std::vector<int> fail;
constexpr unsigned WPAD_BUTTON_A=1,WPAD_BUTTON_2=2,WPAD_CLASSIC_BUTTON_A=4,PAD_BUTTON_A=8,WPAD_BUTTON_B=16,WPAD_BUTTON_1=32,WPAD_CLASSIC_BUTTON_B=64,PAD_BUTTON_B=128,HOME=256;
namespace Wii{uint64_t PerfTime(){return uint64_t(scans)*1000000;}}
void DrawSettingsScreen(int,const char*){}void WPAD_ScanPads(){++scans;}void PAD_ScanPads(){}void VIDEO_WaitVSync(){}
unsigned WPAD_ButtonsDown(int){return buttons;}unsigned PAD_ButtonsDown(int){return pad;}
bool MenuExitPressed(){return buttons&HOME;}void RequestExit(){quitRequested=true;}
bool WiiVideoModeSupported(int){return !unsupported;}
bool WiiVideoApplyMode(int mode){if(std::find(fail.begin(),fail.end(),mode)!=fail.end())return false;applied=mode;return true;}
'''+s[a:b]+r'''
static void Reset(){quitRequested=unsupported=false;buttons=pad=scans=0;fail.clear();applied=1;Wii::GetSettings().video=2;}
int main(){
 Reset();buttons=HOME|WPAD_BUTTON_A;assert(!TestVideoMode(3,1,2)&&quitRequested&&applied==1&&scans==1);
 Reset();buttons=WPAD_BUTTON_A;assert(TestVideoMode(3,1,2)&&!quitRequested&&applied==2);
 Reset();buttons=WPAD_BUTTON_B;assert(!TestVideoMode(3,1,2)&&applied==1&&Wii::GetSettings().video==1);
 Reset();assert(!TestVideoMode(3,1,2)&&scans>=10&&applied==1);
 Reset();quitRequested=true;assert(!TestVideoMode(3,1,2)&&scans==0&&applied==1);
 Reset();unsupported=true;assert(!TestVideoMode(3,1,2)&&scans==0&&applied==1&&Wii::GetSettings().video==1);
 Reset();buttons=WPAD_BUTTON_B;fail={4};assert(!TestVideoMode(3,4,2)&&applied==0&&Wii::GetSettings().video==0);
 Reset();buttons=WPAD_BUTTON_B;fail={4,0};assert(!TestVideoMode(3,4,2)&&applied==2&&Wii::GetSettings().video==2);
 puts("pass: actual video preview: HOME wins over confirm, external quit, timeout, cancel, unsupported mode and honest failed-restore settings");
}
'''
with tempfile.TemporaryDirectory(prefix='video-preview-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),str(root/'Wii/Settings.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
