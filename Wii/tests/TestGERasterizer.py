#!/usr/bin/env python3
"""Actual triangle rasterizer versus the frozen preceding build, including feedback."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
def body(path):
 s=path.read_text();return s[s.index('namespace Rasterizer {'):s.index('// Direct sprite walking')]+ '\n}\n'
old=body(root/'Wii/tests/GEReferenceRasterizer.cpp').replace('namespace Rasterizer {','namespace ReferenceRasterizer {',1)
new=body(root/'GPU/Software/Rasterizer.cpp')
s=(root/'GPU/Software/Sampler.cpp').read_text();sampler=s[s.index('template <unsigned int texel_size_bits'):s.rindex('\n};')]
t=(root/'GPU/Software/TransformUnit.cpp').read_text();a=t.index('DrawingCoords TransformUnit::ScreenToDrawing');b=t.index('ScreenCoords TransformUnit::DrawingToScreen',a);coords=t[a:b]
code=r'''
#if defined(__SANITIZE_ADDRESS__)
extern "C" int __lsan_is_turned_off(){return 1;}
#endif
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include "Common/ColorConv.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Sampler.h"
#include "Wii/PreparedBuffer.h"
#include "Wii/DXTTexel.h"
#include "Wii/GraphicsWork.h"
#undef ERROR_LOG_REPORT
#define ERROR_LOG_REPORT(...) assert(false)
#undef _dbg_assert_msg_
#define _dbg_assert_msg_(tag,condition,...) assert(condition)
GPUgstate gstate{};GPUStateCache gstate_c{};FormatBuffer fb,depthbuf;
Config::Config(){} Config::~Config(){} Config g_Config;
u32 clut[4096]{};
namespace Wii {AddressSpace addressSpace;}
namespace Sampler {
'''+sampler+r'''
NearestFunc GetNearestFunc(){return GetWiiNearestFunc();}
LinearFunc GetLinearFunc(){return GetWiiLinearFunc();}
}
'''+coords+old+new+r'''
static uint32_t F24(float value){uint32_t bits;memcpy(&bits,&value,4);return bits>>8;}
int main(){
 std::vector<u8>ram(1<<20),vram(1<<21);
 Wii::addressSpace.ram=ram.data();Wii::addressSpace.ramSize=ram.size();Wii::addressSpace.vram=vram.data();Wii::addressSpace.RebuildPages();
 uint32_t seed=0x423901;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 for(auto &c:ram)c=random();for(auto &c:vram)c=random();for(auto &c:clut)c=random();
 unsigned cases=0;
 for(unsigned trial=0;trial<1800;++trial){
   gstate={};gstate.framebufpixformat=trial%4;gstate.fbwidth=32;gstate.zbwidth=trial%7?32:65;
   gstate.maxz=65535;gstate.minz=trial%5?0:1000;
   gstate.vertType=trial&4?GE_VTYPE_THROUGH:0;
   gstate.textureMapEnable=trial%4!=0;gstate.texformat=trial%11;
   gstate.texfunc=(trial%5)|((trial&8)?0x100:0)|((trial&16)?0x10000:0);
   gstate.texsize[0]=4|(4<<8);gstate.texbufwidth[0]=16;
   const u32 source=trial%3?0x08000000:0x04000000;
   gstate.texaddr[0]=source&0xffffff;gstate.texbufwidth[0]|=source>>8;
   assert(gstate.getTextureAddress(0)==source);
   gstate.texfilter=trial&32?0x101:0;gstate.texwrap=trial&64?0x101:0;
   gstate.texmode=trial&128?1:0;gstate.texlevel=(trial%4)|((trial%3)*24<<16);
   gstate.texlodslope=F24(2.f);
   gstate.clutformat=(trial%4)|(255<<8);
   if(trial%7==0) {
     gstate.texmode|=2<<16;gstate.texfilter|=4|(trial&1?2:0);
     for(unsigned level=1;level<=2;++level){
       gstate.texsize[level]=(4-level)|((4-level)<<8);
       gstate.texbufwidth[level]=16|(source>>8);gstate.texaddr[level]=level*4096;
     }
   }
   gstate.shademodel=trial&256?GE_SHADE_GOURAUD:GE_SHADE_FLAT;
   gstate.texenvcolor=random()&0xffffff;
   gstate.alphaBlendEnable=trial&512?1:0;
   gstate.blend=(trial%6)<<8|(random()%16)<<4|(random()%16);
   if(trial%3==0)gstate.blend=GE_SRCBLEND_SRCALPHA|(GE_DSTBLEND_INVSRCALPHA<<4);
   gstate.blendfixa=random()&0xffffff;gstate.blendfixb=random()&0xffffff;
   gstate.alphaTestEnable=trial&2?1:0;gstate.alphatest=(random()&0xffff00)|(random()%8);
   gstate.zTestEnable=trial%3?1:0;gstate.ztestfunc=random()%8;gstate.zmsk=trial&1;
   gstate.pmskc=trial%9?0:random()&0xffffff;gstate.pmska=random()&255;
   if(trial%13==0){gstate.stencilTestEnable=1;gstate.stenciltest=(random()&0xffff00)|(random()%8);gstate.stencilop=random()&0x070707;}
   if(trial%23==0){gstate.stencilTestEnable=1;gstate.stenciltest=GE_COMP_ALWAYS;gstate.stencilop=0;}
   if(trial%17==0){gstate.colorTestEnable=1;gstate.colortest=random()%4;gstate.colortestmask=random()&0xffffff;gstate.colorref=random()&0xffffff;}
   if(trial%19==0){gstate.logicOpEnable=1;gstate.lop=random()%16;}
   if(trial%5==0){gstate.fogEnable=1;gstate.fogcolor=random()&0xffffff;}
   fb.data=trial%5?vram.data():vram.data()+0x1ffff0;
   depthbuf.data=trial%3?ram.data()+8192:fb.data;
   VertexData v[3]{};
   v[0].screenpos=ScreenCoords((random()%8)*16,0,10000);
   v[1].screenpos=ScreenCoords((16+random()%5)*16,(random()%5)*16,12000);
   v[2].screenpos=ScreenCoords(0,(16+random()%5)*16,9000);
   for(unsigned i=0;i<3;++i){
     v[i].clippos.w=0.5f+i*0.5f;v[i].fogdepth=i*0.5f;
     v[i].color0=Vec4<int>(random()%256,random()%256,random()%256,random()%256);
     v[i].color1=trial%2?Vec3<int>(0,0,0):Vec3<int>(random()%32,random()%32,random()%32);
     v[i].texturecoords=Vec2<float>((i==1)?1.f:0,(i==2)?1.f:0);
     if(gstate.isModeThrough())v[i].texturecoords*=16.f;
   }
   // Snapshot all potentially touched backing bytes, including mirrored rows.
   const std::vector<u8> initialV(vram),initialR(ram.begin(),ram.begin()+16384);
   ReferenceRasterizer::DrawTriangleSlice<false>(v[0],v[1],v[2],0,0,20*16,20*16,true,0,11);
   const std::vector<u8> expectedV(vram),expectedR(ram.begin(),ram.begin()+16384);
   memcpy(vram.data(),initialV.data(),vram.size());memcpy(ram.data(),initialR.data(),16384);
   Rasterizer::DrawTriangleSlice<false>(v[0],v[1],v[2],0,0,20*16,20*16,true,0,11);
   if(memcmp(vram.data(),expectedV.data(),vram.size())||memcmp(ram.data(),expectedR.data(),16384)){
     fprintf(stderr,"triangle mismatch trial %u\n",trial);return 1;
   }
   ++cases;
 }
 printf("pass: %u complete old/new triangle draws, all formats, alpha/blend/depth/masks, stencil/logic fallbacks, secondary colors, filtered/compressed textures, feedback and aliased depth/color\n",cases);
#ifdef GE_BENCHMARK
 gstate={};gstate.framebufpixformat=GE_FORMAT_8888;gstate.fbwidth=gstate.zbwidth=128;gstate.maxz=65535;
 gstate.shademodel=GE_SHADE_GOURAUD;gstate.textureMapEnable=1;gstate.texformat=GE_TFMT_8888;
 gstate.texsize[0]=6|(6<<8);gstate.texbufwidth[0]=64|(0x08000000>>8);gstate.texaddr[0]=0;assert(gstate.getTextureAddress(0)==0x08000000);gstate.texfunc=GE_TEXFUNC_MODULATE|0x100;
 fb.data=vram.data();depthbuf.data=ram.data()+65536;
 VertexData v[3]{};v[0].screenpos=ScreenCoords(0,0,10000);v[1].screenpos=ScreenCoords(128*16,0,10000);v[2].screenpos=ScreenCoords(0,128*16,10000);
 for(unsigned i=0;i<3;++i){v[i].color0=Vec4<int>(173,239,83,191);v[i].clippos.w=1.f;v[i].fogdepth=i*0.5f;v[i].texturecoords=Vec2<float>(i==1?1.f:0,i==2?1.f:0);}
 for(unsigned workload=0;workload<5;++workload){
   gstate.alphaTestEnable=workload==1||workload==4;gstate.alphatest=GE_COMP_GEQUAL|(128<<8)|(255<<16);
   gstate.fogEnable=workload==3;gstate.fogcolor=0x7f9f4f;
   gstate.framebufpixformat=workload==4?GE_FORMAT_4444:GE_FORMAT_8888;
   gstate.alphaBlendEnable=workload==2;gstate.blend=GE_SRCBLEND_SRCALPHA|(GE_DSTBLEND_INVSRCALPHA<<4);
   for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn){
     const bool reference=(turn^(round&1))==0;auto start=std::chrono::steady_clock::now();
     for(unsigned i=0;i<200;++i){if(reference)ReferenceRasterizer::DrawTriangleSlice<false>(v[0],v[1],v[2],0,0,127*16,127*16,true,0,64);
       else Rasterizer::DrawTriangleSlice<false>(v[0],v[1],v[2],0,0,127*16,127*16,true,0,64);}
     printf("host triangle workload %u round %u %s %.3f ms\n",workload,round,reference?"old":"new",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
   }
 }
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='ge-raster-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 flags=['-O3','-DGE_BENCHMARK'] if '--benchmark' in sys.argv else ['-O2','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all']
 subprocess.run(['g++','-std=c++11',*flags,'-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),str(root/'GPU/Common/TextureDecoder.cpp'),str(root/'ext/xxhash.c'),str(root/'GPU/Math3D.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
