#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual shutdown ordering and HOME routing with VI/loader mocks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
video=(root/'Wii/Video.cpp').read_text();a=video.index('void WiiVideoShutdown()');b=video.index('\n#include "Wii/MenuFont.h"',a)
main=(root/'Wii/Main.cpp').read_text();p=main.index('static void PollInput()');q=main.index('  const u32 gc =',p)
poll=main[p:q]+'}\n'
m=main.index('static bool MenuExitPressed()');n=main.index('static const char *SettingOnOff',m)
menu=main[m:n]
a2=main.index('  WiiVideoShutdown();\n  Wii::CloseFileLogging();');b2=main.index('\n  return 0;',a2)
cleanup=main[a2:b2].replace('fflush(', 'MockFlush(').replace('fclose(', 'MockClose(')
code=r'''
#include <cassert>
#include <vector>
#include <cstdio>
#include <atomic>
using u32=unsigned;
static std::vector<unsigned> events;
struct GXRModeObj{unsigned viTVMode;};
static GXRModeObj loaderMode{0};
static bool videoStarted=true,black=false,pending=false,quitRequested=false,returnToMenu=false;
constexpr unsigned VI_NON_INTERLACE=1;
namespace Wii{void CloseFileLogging(){events.push_back(7);}void GXShutdown(){events.push_back(1);}void RequestEmulationStop(bool){}void UnmountStorage(){events.push_back(9);}}
void VIDEO_SetBlack(bool b){black=b;events.push_back(2);}
void VIDEO_Flush(){pending=true;events.push_back(3);}
void VIDEO_WaitForFlush(){assert(pending);pending=false;events.push_back(4);}
void VIDEO_WaitVSync(){assert(black&&!pending);events.push_back(5);}
void VIDEO_Configure(GXRModeObj *m){assert(black&&!pending&&m==&loaderMode);events.push_back(6);}
void MockFlush(FILE*){events.push_back(7);}void MockClose(FILE*){events.push_back(8);}
static FILE *consoleFile=stdout,*errorFile=stderr;
static unsigned down=0,held=0,padDown=0;
constexpr unsigned WPAD_BUTTON_HOME=1,WPAD_CLASSIC_BUTTON_HOME=2,PAD_TRIGGER_Z=4,PAD_BUTTON_START=8;
void WPAD_ScanPads(){}void PAD_ScanPads(){}
unsigned WPAD_ButtonsDown(int){return down;}unsigned PAD_ButtonsHeld(int){return held;}unsigned PAD_ButtonsDown(int){return padDown;}
static void RequestExit(){quitRequested=true;Wii::RequestEmulationStop(true);}
'''+video[a:b]+poll+menu+'void Cleanup(){\n'+cleanup+'\n}\n'+r'''
int main(){
 for(unsigned selected:{0u,1u,2u,4u})for(unsigned preferred:{0u,1u}) {
  (void)selected;loaderMode.viTVMode=preferred;videoStarted=true;black=pending=false;events.clear();
  Cleanup();assert(!videoStarted&&black&&!pending);
  const std::vector<unsigned> expected=preferred?std::vector<unsigned>{1,2,3,4,5,6,2,3,4,5,5,7,9}:std::vector<unsigned>{1,2,3,4,5,6,2,3,4,5,7,9};
  assert(events==expected);
  events.clear();WiiVideoShutdown();assert(events==std::vector<unsigned>{1});
 }
 for(unsigned buttons:{0u,WPAD_BUTTON_HOME,WPAD_CLASSIC_BUTTON_HOME}) {
  quitRequested=returnToMenu=false;down=buttons;held=0;PollInput();assert(!quitRequested&&returnToMenu==bool(buttons));
 }
 quitRequested=returnToMenu=false;down=0;held=PAD_TRIGGER_Z|PAD_BUTTON_START;PollInput();assert(!quitRequested&&returnToMenu);
 // Crossing back to the menu with HOME merely held cannot request app exit.
 held=WPAD_BUTTON_HOME;down=padDown=0;assert(!MenuExitPressed()&&!quitRequested);
 down=WPAD_BUTTON_HOME;assert(MenuExitPressed());
 for(;;) {if(MenuExitPressed()) {quitRequested=true;break;}}assert(quitRequested);
 quitRequested=false;down=0;padDown=PAD_BUTTON_START;assert(MenuExitPressed());
 RequestExit();assert(quitRequested);
 puts("pass: actual shutdown restores loader mode while black, flush/retrace order, XFB lifetime, streams before unmount, game HOME/classic/GC returns to menu; menu HOME requests loader exit");
}
'''
assert 'free(' not in video[a:b]
assert 'WiiVideoShutdown();return 1' not in main
with tempfile.TemporaryDirectory(prefix='video-shutdown-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
