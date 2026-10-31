#!/usr/bin/env python3
"""Exercise actual overlay sampling, drawing, both presenters, and invalid frames."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Perf.cpp').read_text();a=s.index('namespace Wii {');b=s.index('static void PerfMemory',a)
code=r'''
#include "Wii/Perf.h"
#include <cassert>
#include <cstdio>
static uint64_t wall=1000000,guest=0;
static int mhz=222;
uint64_t gettime(){return wall;}uint64_t ticks_to_microsecs(uint64_t v){return v;}
struct Heap{unsigned uordblks,fordblks;};Heap mallinfo(){return {1024,32*1024*1024};}
unsigned SYS_GetArena1Size(){return 0;}unsigned SYS_GetArena2Size(){return 0;}
struct CPU{unsigned pc=0;}cpu,*currentMIPS=&cpu;
namespace CoreTiming{uint64_t GetGlobalTimeUs(){return guest;}int GetClockFrequencyMHz(){return mhz;}}
namespace Wii{void AllocationSnapshot(unsigned,unsigned,unsigned,unsigned,unsigned){}}
'''+s[a:b]+r'''
}
int main(){
 Wii::PerfReset();assert(!Wii::GetPerfSnapshot().valid);
 wall+=100000;Wii::PerfGE(1000000);
 wall+=200000;Wii::PerfRun(1000000);
 for(int i=0;i<15;++i)Wii::PerfPresent(wall,true);
 Wii::PerfPresent(wall,false);
 wall=1499999;Wii::UpdateSnapshot();assert(!Wii::GetPerfSnapshot().valid);
 wall=1500000;guest=500000;Wii::UpdateSnapshot();
 auto p=Wii::GetPerfSnapshot();assert(p.valid&&p.speed==100&&p.fps==30&&p.cpu==40&&p.ge==20);
 assert(p.heapFree==32*1024*1024&&p.clockMHz==222);
 wall=2000000;guest=750000;mhz=333;Wii::UpdateSnapshot();p=Wii::GetPerfSnapshot();
 assert(p.speed==50&&p.clockMHz==333&&p.fps==0&&p.cpu==0&&p.ge==0);
 Wii::ResetWindow();for(int i=0;i<30;++i)Wii::PerfPresent(wall,true);
 wall=2500000;guest=1250000;Wii::UpdateSnapshot();p=Wii::GetPerfSnapshot();assert(p.speed==100&&p.fps==60);
 Wii::PerfReset();assert(!Wii::GetPerfSnapshot().valid);
 wall+=500000;guest+=500000;Wii::UpdateSnapshot();p=Wii::GetPerfSnapshot();assert(p.fps==0&&p.cpu==0&&p.ge==0);
 puts("pass: actual overlay samples: time-based speed, successful presented fps, CPU/GE, heap, logger windows and per-game reset");
}
'''
v=(root/'Wii/Video.cpp').read_text();a=v.index('// XFB-only overlay:');b=v.index('// Three small guest-memory',a)
video=r'''
#include "Wii/Settings.h"
#include "Wii/Perf.h"
#include "Wii/Presentation.h"
#include "Wii/Pixel.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <cstring>
using u8=uint8_t;using u32=uint32_t;
template<class T>struct PSPPointer{u32 ptr=0;};
struct Mode{unsigned fbWidth=640,xfbHeight=480;}storage,*mode=&storage;
static void *buffers[2];static unsigned backBuffer=0;
static bool valid=true,gx=true;static unsigned format=3,presents=0,rectangles=0,clears=0;static std::vector<std::string> text;
static std::vector<u8> pixels(512*272*4,0x77);
bool __DisplayGetFramebuf(PSPPointer<u8>* p,u32* s,u32* f,int){p->ptr=0x04000000;*s=512;*f=format;return valid;}
namespace Memory{bool IsValidRange(u32 address,unsigned size){return address>=0x04000000&&uint64_t(address-0x04000000)+size<=pixels.size();}u8 *GetPointerUnchecked(u32 a){return pixels.data()+a-0x04000000;}}
namespace Wii{
 Settings settings;Settings &GetSettings(){return settings;}
 PerfSnapshot snapshot;const PerfSnapshot &GetPerfSnapshot(){return snapshot;}
 const char *GXStatus(){return gx?"ready":"off / cpu";}
 bool GXPresent(u32,unsigned,unsigned,const void *,void *){return gx;}
 struct Space{unsigned Span(u32){return 4096;}}addressSpace;
 bool CopyFromGuest(void *p,u32 a,unsigned size){if(!Memory::IsValidRange(a,size))return false;memcpy(p,Memory::GetPointerUnchecked(a),size);return true;}
}
void AcquireBackBuffer(){}void PresentBackBuffer(){++presents;}
constexpr unsigned COLOR_BLACK=0;
void VIDEO_ClearFrameBuffer(Mode*m,void*p,unsigned){++clears;memset(p,0,m->fbWidth*m->xfbHeight*2);}
void WiiMenuRect(int x,int y,int w,int h,unsigned){assert(x>=0&&y>=0&&x+w<=640&&y+h<=480);++rectangles;}
void WiiMenuText(int,int,const char *s,unsigned,int=1){assert(strlen(s)<76);text.push_back(s);}
'''+v[a:b]+r'''
int main(){
 const auto original=pixels;
 for(unsigned height:{480u,576u})for(bool wide:{false,true})for(bool useGX:{false,true}){
  storage.xfbHeight=height;gx=useGX;Wii::settings.aspect=wide;
  std::vector<u32> words(640*height/2+32,0xdeadbeef);buffers[0]=words.data()+16;
  text.clear();rectangles=0;Wii::settings.performanceOverlay=false;assert(WiiVideoPresent());assert(text.empty()&&!rectangles);
  Wii::settings.performanceOverlay=true;Wii::snapshot=Wii::PerfSnapshot();assert(WiiVideoPresent());assert(text.size()==2&&text[0].find("sampling")!=std::string::npos);
  text.clear();Wii::snapshot.valid=true;Wii::snapshot.speed=100;Wii::snapshot.fps=30;Wii::snapshot.clockMHz=267;
  assert(WiiVideoPresent());assert(text.size()==3&&text[0].find("267 mhz")!=std::string::npos&&text[2].find("gx ")==0);
  for(const auto &line:text)assert(line.find("jit")==std::string::npos);
  for(unsigned i=0;i<16;++i)assert(words[i]==0xdeadbeef&&words[words.size()-1-i]==0xdeadbeef);
  assert(pixels==original);
 }
 const unsigned before=presents;valid=false;assert(!WiiVideoPresent());valid=true;format=9;assert(!WiiVideoPresent());assert(presents==before);
 puts("pass: actual overlay/present paths: toggle, sampling and all metrics, GX/CPU, aspects, 480/576 lines, no guest writes, invalid frames rejected");
}
'''
with tempfile.TemporaryDirectory(prefix='rc8-overlay-') as t:
 p=Path(t)
 for name,body in [('samples',code),('video',video)]:
  cpp=p/(name+'.cpp');cpp.write_text(body);exe=p/name
  subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-I'+str(root),str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
