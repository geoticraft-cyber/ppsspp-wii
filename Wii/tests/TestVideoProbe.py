#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual bounded display diagnostic with guest memory mocks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Video.cpp').read_text();a=s.index('void WiiVideoReport()');b=s.index('\nvoid WiiVideoShutdown()',a)
code=r'''
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include "Wii/Pixel.h"
using u8=uint8_t;using u32=uint32_t;
struct GuestWord {
 u32 bits=0;
 operator u32() const {return __builtin_bswap32(bits);}
 GuestWord &operator=(u32 value) {bits=__builtin_bswap32(value);return *this;}
};
template<class T>struct PSPPointer {GuestWord ptr;};
static u32 frame=0x08000000,stride=512,format=3;static unsigned mode=1,reads=0,reports=0;
static std::vector<uint8_t> ram(2*1024*1024);static std::string report;
bool __DisplayGetFramebuf(PSPPointer<u8>*p,u32*s,u32*f,int latched) {
 p->ptr=frame+(latched?0x1000:0);*s=stride;*f=format;return true;
}
namespace Wii {
unsigned DiagnosticLevel(){return mode;}
bool GXEnabled(){return true;}
bool CopyFromGuest(void *dest,uint32_t address,unsigned bytes) {
 ++reads;assert(address>=0x08000000&&uint64_t(address-0x08000000)+bytes<=ram.size());
 memcpy(dest,ram.data()+address-0x08000000,bytes);return true;
}
}
void Log(const char *fmt,...) {++reports;char buf[1024];va_list args;va_start(args,fmt);vsnprintf(buf,sizeof(buf),fmt,args);va_end(args);report=buf;}
#define INFO_LOG(tag,...) Log(__VA_ARGS__)
'''+s[a:b]+r'''
int main() {
 mode=0;WiiVideoReport();assert(!reads&&!reports);mode=1;
 const unsigned xs[]={0,240,479},ys[]={0,136,271};
 for(format=0;format<=3;++format) {
  const unsigned bpp=format==3?4:2;
  for(unsigned i=0;i<3;++i) {
   auto *p=ram.data()+(ys[i]*stride+xs[i])*bpp;
   if(format==3){p[0]=255;p[1]=p[2]=p[3]=0;}
   else {p[0]=format==2?15:31;p[1]=0;}
  }
  const auto before=reads;WiiVideoReport();assert(reads==before+3);
  assert(report.find("sample-valid=7 rgb=ff0000,ff0000,ff0000")!=std::string::npos);
  assert(report.find("latched=08001000/")!=std::string::npos);
 }
 for(u32 invalid:{0u,0xfffffffcu}) {
  frame=invalid;format=3;const auto before=reads;WiiVideoReport();
  if(!frame)assert(reads==before);
 }
}
'''
# High-address bounds are covered with a failing translator rather than assert.
code=code.replace('++reads;assert(address>=0x08000000&&uint64_t(address-0x08000000)+bytes<=ram.size());', '++reads;if(address<0x08000000||uint64_t(address-0x08000000)+bytes>ram.size())return false;')
code=code.replace('if(!frame)assert(reads==before);', 'assert(reads==before+(frame?1:0));assert(report.find("sample-valid=0")!=std::string::npos);')
with tempfile.TemporaryDirectory(prefix='video-probe-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
print('pass: actual display probes, all four pixel formats, disabled diagnostics, display off, latched state and address overflow')
