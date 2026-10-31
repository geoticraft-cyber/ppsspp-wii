#!/usr/bin/env python3
"""Run the actual complex sprite loop against its reference pixel path, including feedback."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Rasterizer.cpp').read_text()
helpers=s[s.index('static inline u32 GetPixelColor('):s.index('static inline void ApplyTexturing(')]
tail=s[s.index('  ++Wii::GetGraphicsWork().pixelStateFallbacks;',s.index('bool TryDrawSprite')):s.index('// Draws triangle, vertices specified') ]
tail=tail.replace('PrepareWiiPixelPipeline();','forceReference?WiiPixelPipeline{}:PrepareWiiPixelPipeline();')
code=r'''
#if defined(__SANITIZE_ADDRESS__)
// LeakSanitizer cannot enumerate threads in the restricted host environment.
extern "C" int __lsan_is_turned_off() {return 1;}
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Common/ColorConv.h"
#include "Wii/PreparedBuffer.h"
#include "Wii/GraphicsWork.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/SoftGpu.h"
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
using namespace Math3D;
GPUgstate gstate{};GPUStateCache gstate_c{};FormatBuffer fb,depthbuf;
namespace Wii {AddressSpace addressSpace;bool EmulationStopRequested(){return false;}}
'''+helpers+r'''
static bool Run(bool forceReference,bool feedback) {
 const int x0=0,x1=8,y0=0,y1=4;
 const bool textured=true;const float du=1,dv=1,invW=1.0f/8,invH=1.0f/4;
 struct {int x=0,y=0;}top;
 VertexData tl{},br{};br.color0=Vec4<int>(72,194,83,127);br.color1=Vec3<int>(11,23,31);br.screenpos.z=12345;
 const auto sample=[&](Vec4<int> &color,float u,float v) {
   const int x=int(u*8),y=int(v*4);
   // Repeated nearest samples of the preceding framebuffer pixel expose write
   // order, including aliased depth/color surfaces and destination blending.
   const u32 texel=feedback?GetPixelColor(x?x-1:0,y):u32((x*131+y*53)*0x1234567u);
   color=GetTextureFunctionOutput(br.color0,Vec4<int>::FromRGBA(texel));
 };
'''+tail+r'''
int main() {
 std::vector<uint8_t> ram(0x2000000),vram(0x200000);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();
 uint32_t seed=0x12564578;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 unsigned comparisons=0;
 for(unsigned trial=0;trial<4000;++trial) {
   gstate={};gstate.framebufpixformat=(trial&1)?GE_FORMAT_565:GE_FORMAT_8888;
   gstate.fbwidth=gstate.zbwidth=64;gstate.maxz=65535;gstate.vertType=GE_VTYPE_THROUGH;
   gstate.zTestEnable=(trial>>1)&1;gstate.ztestfunc=(trial>>2)&7;gstate.zmsk=(trial>>5)&1;
   gstate.alphaBlendEnable=(trial>>6)&1;gstate.blend=(trial%6)<<8|(random()%16)<<4|(random()%16);
   gstate.blendfixa=random()&0xffffff;gstate.blendfixb=random()&0xffffff;
   // Exercise fallbacks too: masks, tests, packed stencil formats and doubling.
   if(trial%11==0)gstate.pmskc=0x010203;
   if(trial%13==0){gstate.textureMapEnable=1;gstate.texfunc=0x10000;}
   if(trial%17==0)gstate.framebufpixformat=GE_FORMAT_4444;
   fb.data=vram.data();depthbuf.data=trial%3?ram.data():fb.data;
   for(unsigned i=0;i<4096;++i){vram[i]=random();ram[i]=random();}
   const std::vector<u8> initialV(vram.begin(),vram.begin()+4096),initialR(ram.begin(),ram.begin()+4096);
   Run(true,trial&2);
   const std::vector<u8> expectedV(vram.begin(),vram.begin()+4096),expectedR(ram.begin(),ram.begin()+4096);
   memcpy(vram.data(),initialV.data(),4096);memcpy(ram.data(),initialR.data(),4096);
   Run(false,trial&2);
   assert(memcmp(vram.data(),expectedV.data(),4096)==0&&memcmp(ram.data(),expectedR.data(),4096)==0);++comparisons;
 }
 // Only states with remaining reference-only behavior reject preparation.
 for(unsigned kind=0;kind<3;++kind) {
  gstate={};gstate.framebufpixformat=GE_FORMAT_8888;
  if(kind==0)gstate.stencilTestEnable=1;
  if(kind==1)gstate.logicOpEnable=1;
  if(kind==2)gstate.clearmode=1;
  assert(!PrepareWiiPixelPipeline().draw);
 }
#ifdef SPRITE_BENCHMARK
 gstate={};gstate.framebufpixformat=GE_FORMAT_8888;gstate.fbwidth=gstate.zbwidth=64;
 gstate.vertType=GE_VTYPE_THROUGH;gstate.maxz=65535;gstate.alphaBlendEnable=1;
 gstate.blend=GE_SRCBLEND_SRCALPHA|(GE_DSTBLEND_INVSRCALPHA<<4);
 fb.data=vram.data();depthbuf.data=ram.data();
 for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn) {
   const bool reference=(turn^(round&1))==0;
   const auto start=std::chrono::steady_clock::now();
   for(unsigned i=0;i<50000;++i)Run(reference,false);
   printf("host sprite round %u %s %.3f ms\n",round,reference?"reference":"prepared",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
#endif
 printf("pass: %u exact complex sprite loops (32 pixels each), sequential texture feedback and aliased depth/color; formats, all depth predicates, depth writes, blend equations/factors, clamp, stencil preservation, VRAM wrap, bounds and fallback gates\n",comparisons);
}
'''
with tempfile.TemporaryDirectory(prefix='sprite-pipeline-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11',*(['-O3','-DSPRITE_BENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all']),'-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
