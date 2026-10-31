#!/usr/bin/env python3
"""Compare draw-selected Wii samplers with the generic texel reader."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/Sampler.cpp').read_text()
reader=s[s.index('template <unsigned int texel_size_bits'):s.rindex('\n};')]
code=r'''
#if defined(__SANITIZE_ADDRESS__)
// LeakSanitizer cannot enumerate threads in the restricted host environment.
extern "C" int __lsan_is_turned_off() {return 1;}
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "Common/ColorConv.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/Sampler.h"
#include "Wii/BufferAccess.h"
#include "Wii/DXTTexel.h"
using namespace Math3D;
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
GPUgstate gstate{};GPUStateCache gstate_c{};u32 clut[4096]{};
namespace Wii {AddressSpace addressSpace;}
namespace Sampler {
'''+reader+r'''
}
int main() {
 std::vector<uint8_t> ram(0x2000000),vram(0x200000);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();
 uint32_t seed=0xabcd2345;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(auto &n:vram)n=random();for(auto &n:ram)n=random();
 unsigned comparisons=0;
 for(unsigned format=0;format<11;++format)for(unsigned swizzle=0;swizzle<2;++swizzle) {
  gstate.texformat=format;gstate.texmode=swizzle;
  auto nearest=Sampler::GetWiiNearestFunc();auto linear=Sampler::GetWiiLinearFunc();
  for(unsigned trial=0;trial<10000;++trial) {
   const u8 *base=trial%3==0?ram.data()+ram.size()-256:vram.data()+0x1ffff0;
   if(trial%100==0)base=nullptr;
   int u[4],v[4];for(unsigned i=0;i<4;++i){u[i]=random()%64;v[i]=random()%64;}
   const int fu=random()%256,fv=random()%256,level=random()%8;
   if(format>=8&&base) {
     for(unsigned i=0;i<4;++i) {
       u32 data[16];const unsigned offset=((v[i]/4)*16+u[i]/4)*(format==8?8:16);
       const u8 *block=Sampler::TextureAccess(base,offset,format==8?8:16);
       if(format==8)DecodeDXT1Block(data,(const DXT1Block*)block,4,4,false);
       if(format==9)DecodeDXT3Block(data,(const DXT3Block*)block,4,4);
       if(format==10)DecodeDXT5Block(data,(const DXT5Block*)block,4,4);
       assert(nearest(u[i],v[i],base,64,level)==data[(v[i]%4)*4+u[i]%4]);
     }
   }
   assert(nearest(u[0],v[0],base,64,level)==Sampler::SampleNearest(u[0],v[0],base,64,level));
   assert(linear(u,v,fu,fv,base,64,level)==Sampler::SampleLinear(u,v,fu,fv,base,64,level));
   comparisons+=2;
   // A following sample must observe source edits immediately.
   if(base)const_cast<u8*>(base)[0]^=17;
  }
 }
 printf("pass: %u exact sampler comparisons, direct/indexed formats, nearest/bilinear, swizzle, mip levels, null, VRAM wrap and RAM bounds\n",comparisons);
}
'''
with tempfile.TemporaryDirectory(prefix='performance-sampler-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),str(root/'GPU/Common/TextureDecoder.cpp'),str(root/'ext/xxhash.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
