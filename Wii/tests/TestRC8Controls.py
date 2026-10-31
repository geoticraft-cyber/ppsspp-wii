#!/usr/bin/env python3
"""Execute the actual gameplay mapper against input snapshots, without WPAD hardware."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Main.cpp').read_text();s=s[s.index('static void PollInput()'):s.index('static void *InputWorker')]
names=sorted(set(re.findall(r'\b(?:WPAD_(?:BUTTON|CLASSIC_BUTTON|NUNCHUK_BUTTON)_\w+|PAD_(?:BUTTON|TRIGGER)_\w+|CTRL_\w+)\b',s)))
constants=[]
for prefix in ['WPAD_','PAD_','CTRL_']:
 for i,name in enumerate(n for n in names if n.startswith(prefix)):
  constants.append('constexpr unsigned %s=1u<<%d;'%(name,i))
code='''#include "Wii/Input.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
using u32=unsigned;
'''+ '\n'.join(constants)+'''
constexpr int WPAD_EXP_NONE=0,WPAD_EXP_NUNCHUK=1,WPAD_EXP_CLASSIC=2;
struct joystick_t{float mag=0,ang=0;};
struct expansion_t{int type=0;struct {joystick_t js;}nunchuk;struct {joystick_t ljs;}classic;};
static expansion_t attached;
static unsigned wm,gc,down,result;static float rx,ry;static bool returnToMenu=false;
void WPAD_ScanPads(){}void PAD_ScanPads(){}
unsigned WPAD_ButtonsDown(int){return down;}unsigned WPAD_ButtonsHeld(int){return wm;}
unsigned PAD_ButtonsHeld(int){return gc;}int PAD_StickX(int){return 0;}int PAD_StickY(int){return 0;}
void WPAD_Expansion(int,expansion_t *p){*p=attached;}
namespace Wii{void RequestEmulationStop(bool){}}
void __CtrlSetHostState(unsigned b,float x,float y){result=b;rx=x;ry=y;}
'''+s+'''
int main(){
 attached.type=WPAD_EXP_NUNCHUK;
 const unsigned wiis[]={WPAD_BUTTON_A,WPAD_BUTTON_B,WPAD_BUTTON_1,WPAD_BUTTON_2,WPAD_BUTTON_PLUS,WPAD_BUTTON_MINUS,WPAD_NUNCHUK_BUTTON_C,WPAD_NUNCHUK_BUTTON_Z,WPAD_BUTTON_UP,WPAD_BUTTON_DOWN,WPAD_BUTTON_LEFT,WPAD_BUTTON_RIGHT};
 const unsigned psps[]={CTRL_CROSS,CTRL_CIRCLE,CTRL_SQUARE,CTRL_TRIANGLE,CTRL_START,CTRL_SELECT,CTRL_LTRIGGER,CTRL_RTRIGGER,CTRL_UP,CTRL_DOWN,CTRL_LEFT,CTRL_RIGHT};
 for(unsigned i=0;i<12;++i){wm=wiis[i];PollInput();assert(result==psps[i]);assert(rx==0&&ry==0);}
 wm=WPAD_BUTTON_MINUS|WPAD_BUTTON_A;PollInput();assert(result==(CTRL_SELECT|CTRL_CROSS));
 wm=0;attached.nunchuk.js.mag=1;attached.nunchuk.js.ang=45;PollInput();assert(std::fabs(rx-0.7071068f)<0.0001f&&std::fabs(ry-rx)<0.0001f);
 for(float angle:{0.f,90.f,180.f,270.f}){attached.nunchuk.js.ang=angle;PollInput();assert(std::fabs(rx*rx+ry*ry-1)<0.0001f);}
 for(float mag:{0.f,0.05f,0.12f}){attached.nunchuk.js.mag=mag;PollInput();assert(rx==0&&ry==0);}
 attached.nunchuk.js.mag=NAN;PollInput();assert(rx==0&&ry==0);
 attached.type=WPAD_EXP_CLASSIC;wm=WPAD_CLASSIC_BUTTON_B;PollInput();assert(result==CTRL_CROSS);
 attached.type=WPAD_EXP_NONE;wm=WPAD_BUTTON_RIGHT;PollInput();assert(rx==1&&ry==0&&result==CTRL_RIGHT);
 wm=0;gc=PAD_BUTTON_X;PollInput();assert(result==CTRL_SQUARE);
 gc=0;down=WPAD_BUTTON_HOME;PollInput();assert(returnToMenu);
 down=wm=0;PollInput();assert(result==0&&rx==0&&ry==0);
 puts("pass: actual gameplay controls: direct faces, simultaneous select, nunchuk radial movement, deadzone, invalid reports, classic/GC fallback, HOME, release");
}
'''
with tempfile.TemporaryDirectory(prefix='rc8-controls-') as t:
 p=Path(t);(p/'test.cpp').write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-I'+str(root),str(p/'test.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
