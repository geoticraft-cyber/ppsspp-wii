#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual VI mode allocation/reuse ordering, including OOM rollback."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Video.cpp').read_text();a=s.index('static bool ConfigureMode(');b=s.index('\nbool WiiVideoApplyMode',a)
body=s[a:b].replace('memalign(', 'MockAllocate(').replace('free(', 'MockFree(')
code=r'''
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <map>
struct GXRModeObj{unsigned fbWidth,xfbHeight,viTVMode;};
static GXRModeObj a{640,480,0},b{640,480,1},pal{640,576,1};
static GXRModeObj *mode=nullptr,*configured=nullptr;static void *buffers[2]={},*display=nullptr,*console=nullptr;
static unsigned backBuffer=0,xfbBytes=0,allocations=0,freeCount=0,failOn=0;static bool black=false,pending=false;
static std::map<void*,unsigned> allocated;
constexpr unsigned VI_DISPLAY_PIX_SZ=2,VI_NON_INTERLACE=1,COLOR_BLACK=0;
#define MEM_K1_TO_K0(p) (p)
#define MEM_K0_TO_K1(p) (p)
unsigned VIDEO_PadFramebufferWidth(unsigned w){return (w+15)&~15u;}
void *MockAllocate(unsigned,unsigned n){++allocations;if(failOn==allocations)return nullptr;void*p=malloc(n);assert(p);allocated[p]=n;return p;}
void MockFree(void*p){assert(p!=display&&p!=console);assert(allocated.erase(p)==1);++freeCount;std::free(p);}
void VIDEO_ClearFrameBuffer(GXRModeObj*m,void*p,unsigned){assert(allocated.at(p)>=m->fbWidth*(m->xfbHeight+2)*2);if(p==display)assert(black&&!pending);}
void VIDEO_SetBlack(bool value){black=value;}void VIDEO_Flush(){pending=true;}void VIDEO_WaitForFlush(){pending=false;}
void VIDEO_WaitVSync(){}
void VIDEO_Configure(GXRModeObj*m){assert(black&&!pending);configured=m;}
void VIDEO_SetNextFramebuffer(void*p){assert(allocated.count(p));display=p;}
void console_init(void*p,int,int,unsigned,unsigned,unsigned){assert(allocated.count(p));console=p;}
'''+body+r'''
int main(){
 assert(ConfigureMode(&a)&&allocations==2&&freeCount==0);auto*p=buffers[0];const auto small=xfbBytes;
 // Equal-sized interlace/progressive changes must work even when allocation fails.
 failOn=allocations+1;assert(ConfigureMode(&b)&&allocations==2&&buffers[0]==p&&xfbBytes==small);
 // Failed growth leaves the current buffers/mode/console untouched.
 assert(!ConfigureMode(&pal)&&mode==&b&&buffers[0]==p&&console==p&&allocated.size()==2);
 failOn=allocations+2;assert(!ConfigureMode(&pal)&&mode==&b&&buffers[0]==p&&allocated.size()==2&&freeCount==1);
 failOn=0;assert(ConfigureMode(&pal)&&allocated.size()==2&&freeCount==3);const auto big=xfbBytes;assert(big>small);
 p=buffers[0];failOn=allocations+1;assert(ConfigureMode(&a)&&buffers[0]==p&&xfbBytes==big&&allocated.size()==2);
 assert(ConfigureMode(&pal)&&buffers[0]==p);assert(!ConfigureMode(nullptr));
 display=console=nullptr;MockFree(buffers[0]);MockFree(buffers[1]);
 puts("pass: actual VI configuration: same-size/downward reuse, both growth allocation failures, retained working mode and XFB/console handoff before free");
}
'''
with tempfile.TemporaryDirectory(prefix='video-allocation-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
