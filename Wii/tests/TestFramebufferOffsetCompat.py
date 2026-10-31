#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""run the real framebuffer selection loop with controlled candidate metadata."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Common/FramebufferCommon.cpp').read_text()
a=s.index('\tVirtualFramebuffer *vfb = nullptr;',s.index('FramebufferManagerCommon::DoSetRenderFrameBuffer'))
b=s.index('\n\tif (vfb) {',a)
loop=s[a:b]
code=r'''
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstdio>
#include "Core/Compatibility.h"
using u32=uint32_t;
enum {GE_FORMAT_565,GE_FORMAT_5551,GE_FORMAT_4444,GE_FORMAT_8888};
#define WARN_LOG_REPORT_ONCE(...) ((void)0)
static CompatFlags flags{};
struct TestCompat {const CompatFlags &flags() const{return ::flags;}};
struct TestCore {TestCompat compat;};
TestCore &PSP_CoreParameter(){static TestCore core;return core;}
struct State {unsigned curRTOffsetX=0;void SetCurRTOffsetX(unsigned n){curRTOffsetX=n;}}gstate_c;
struct VirtualFramebuffer {
 u32 fb_address;int fb_stride,format,z_stride,width,height;
};
struct Params {u32 fb_address;int fb_stride,fmt,z_stride;bool isModeThrough;};
VirtualFramebuffer *Select(std::vector<VirtualFramebuffer*> vfbs_,const Params &params,int drawing_width=64,int drawing_height=64) {
 gstate_c.SetCurRTOffsetX(0);bool vfbFormatChanged=false;
'''+loop+r'''
 (void)vfbFormatChanged;return vfb;
}
int main(){
 for(int format:{GE_FORMAT_565,GE_FORMAT_5551,GE_FORMAT_4444,GE_FORMAT_8888}) {
  const u32 bpp=format==GE_FORMAT_8888?4:2;
  VirtualFramebuffer outer{0x04000000,512,format,512,512,272};
  VirtualFramebuffer exact{0x04000000+64*bpp,512,format,512,128,128};
  Params request{exact.fb_address,512,format,512,false};
  flags={};assert(Select({&outer},request)==&outer&&gstate_c.curRTOffsetX==64);
  flags.DisallowFramebufferAtOffset=true;
  assert(!Select({&outer},request)&&gstate_c.curRTOffsetX==0);
  assert(Select({&outer,&exact},request)==&exact&&gstate_c.curRTOffsetX==0);
  assert(Select({&exact},request)==&exact&&gstate_c.curRTOffsetX==0);
  flags.DisallowFramebufferAtOffset=false;assert(Select({&outer},request)==&outer);
  // Disabling offset reuse must not disable a valid exact binding or merge
  // unrelated targets. Existing format/stride compatibility still applies.
  request.fb_stride=256;assert(!Select({&outer},request));
  request.fb_address=0x04100000;assert(!Select({&outer},request));
 }
 puts("pass: real selection loop, four formats, flagged rejection of containing targets, exact-target selection, unflagged reuse and bounds/stride guards");
}
'''
with tempfile.TemporaryDirectory(prefix='fb-offset-') as t:
 t=Path(t);cpp=t/'test.cpp';cpp.write_text(code);exe=t/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
