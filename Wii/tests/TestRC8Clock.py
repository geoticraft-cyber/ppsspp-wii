#!/usr/bin/env python3
"""Run the actual power clock override/setters and Wii timing initialization."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Core/HLE/scePower.cpp').read_text()
getter=s[s.index('int GetLockedCPUSpeedMhz()'):s.index('#if PPSSPP_PLATFORM(WII)',s.index('int GetLockedCPUSpeedMhz()'))]
a=s.index('void __PowerApplyWiiClock');b=s.index('\n#endif',a)
c=s.index('static u32 scePowerSetClockFrequency(');d=s.index('static u32 scePowerSetBusClockFrequency(',c)
t=(root/'Core/CoreTiming.cpp').read_text();x=t.index('\t// Set the selected clock before HLE');y=t.index('\n#endif',x)
code='''#include <cassert>
#include <cstdio>
#include <cstdint>
using u32=uint32_t;
#define INFO_LOG(...)
#define DEBUG_LOG(...)
#define WARN_LOG(...)
constexpr u32 SCE_KERNEL_ERROR_INVALID_VALUE=0x80000107;
struct {int iLockedCPUSpeed=0;}g_Config;
struct Flags {bool RequireDefaultCPUClock=false;};
struct Compat {Flags f;const Flags &flags()const{return f;}};
struct Params {Compat compat;} params;
Params &PSP_CoreParameter(){return params;}
static int RealpllFreq=222,RealbusFreq=111,pllFreq=222,busFreq=111;
namespace CoreTiming {static int clock=222;int GetClockFrequencyMHz(){return clock;}void SetClockFrequencyMHz(int n){clock=n;}}
u32 hleDelayResult(u32 r,const char *,int){return r;}
'''+getter+s[a:b]+s[c:d]+'''\nstatic int CPU_HZ;
static void InitClock(){
'''+t[x:y]+'''
}
int main(){
 for(int mhz:{0,135,168,201,234,267,300,333}){
  g_Config.iLockedCPUSpeed=mhz;CPU_HZ=333000000;InitClock();assert(CPU_HZ==(mhz?mhz:222)*1000000);
  CoreTiming::clock=333;__PowerApplyWiiClock(mhz);assert(CoreTiming::clock==(mhz?mhz:222));
  assert(g_Config.iLockedCPUSpeed==mhz);assert(pllFreq==(mhz?mhz:222));assert(busFreq==(mhz?mhz/2:111));
  assert(scePowerSetClockFrequency(333,333,166)==0);assert(CoreTiming::clock==(mhz?mhz:333));
  assert(scePowerSetCpuClockFrequency(333)==0);assert(CoreTiming::clock==(mhz?mhz:333));
  assert(scePowerSetClockFrequency(222,111,111)==0);assert(CoreTiming::clock==(mhz?mhz:111));
  assert(scePowerSetCpuClockFrequency(111)==0);assert(CoreTiming::clock==(mhz?mhz:111));
  for(u32 bad:{0u,334u,0xffffffffu})assert(scePowerSetCpuClockFrequency(bad)==SCE_KERNEL_ERROR_INVALID_VALUE);
  assert(scePowerSetClockFrequency(222,333,111)==SCE_KERNEL_ERROR_INVALID_VALUE);
 }
 // A game INI's changed lock is overwritten by the frontend selection.
 g_Config.iLockedCPUSpeed=333;CoreTiming::clock=333;__PowerApplyWiiClock(135);assert(CoreTiming::clock==135&&g_Config.iLockedCPUSpeed==135);
 __PowerApplyWiiClock(0);assert(CoreTiming::clock==222&&g_Config.iLockedCPUSpeed==0);
 assert(scePowerSetClockFrequency(333,333,166)==0&&CoreTiming::clock==333);
 puts("pass: actual clock helpers: all menu MHz, game requests under lock/auto, validation, INI override and auto after a previous locked game");
}
'''
code=code.replace('#include <cstdint>','#include <cstdint>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='rc8-clock-') as t:
 p=Path(t);(p/'test.cpp').write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2',str(p/'test.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
main=(root/'Wii/Main.cpp').read_text();a=main.index('if (initOK)');b=main.index('StartInput();',a)
assert '__PowerApplyWiiClock(runSettings.pspClockMHz);' in main[a:b]
