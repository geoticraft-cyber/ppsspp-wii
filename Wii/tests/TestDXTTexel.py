#!/usr/bin/env python3
"""Exact single-texel decoding against the existing full-block decoder, plus host timing."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
code=r'''
#if defined(__SANITIZE_ADDRESS__)
extern "C" int __lsan_is_turned_off(){return 1;}
#endif
#include <cassert>
#include <cstdio>
#include <chrono>
#include "Wii/DXTTexel.h"
static uint32_t seed=0x46573829;
static unsigned Random(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(){
  DXT5Block block{};u32 expected[16];unsigned checks=0;
  for(unsigned trial=0;trial<65536;++trial){
    auto *bytes=reinterpret_cast<u8*>(&block);for(unsigned i=0;i<sizeof(block);++i)bytes[i]=Random();
    block.alpha1=trial>>8;block.alpha2=trial&255;
    if(trial%5==0)block.color.color2=block.color.color1;
    DecodeDXT1Block(expected,&block.color,4,4,false);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){assert(Wii::DXTTexel(block.color,x,y)==expected[y*4+x]);++checks;}
    DXT3Block three{};memcpy(&three,&block,sizeof(three));
    DecodeDXT3Block(expected,&three,4,4);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){assert(Wii::DXTTexel(three,x,y)==expected[y*4+x]);++checks;}
    DecodeDXT5Block(expected,&block,4,4);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){assert(Wii::DXTTexel(block,x,y)==expected[y*4+x]);++checks;}
  }
  printf("pass: %u exact DXT texels, all alpha endpoint pairs, equal/reversed color endpoints\n",checks);
  DXT5Block blocks[256];for(auto &b:blocks){auto *p=reinterpret_cast<u8*>(&b);for(unsigned i=0;i<sizeof(b);++i)p[i]=Random();}
  volatile u32 sink=0;
  for(unsigned format:{1u,3u,5u})for(unsigned round=0;round<5;++round)for(unsigned turn=0;turn<2;++turn){
    const unsigned mode=turn^(round&1);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<2000000;++i){const auto &b=blocks[i&255];const unsigned x=(i>>8)&3,y=(i>>10)&3;
      if(format==1){if(mode)sink^=Wii::DXTTexel(b.color,x,y);else {DecodeDXT1Block(expected,&b.color,4,4,false);sink^=expected[y*4+x];}}
      else if(format==3){const auto &three=*reinterpret_cast<const DXT3Block*>(&b);if(mode)sink^=Wii::DXTTexel(three,x,y);else {DecodeDXT3Block(expected,&three,4,4);sink^=expected[y*4+x];}}
      else {if(mode)sink^=Wii::DXTTexel(b,x,y);else {DecodeDXT5Block(expected,&b,4,4);sink^=expected[y*4+x];}}
    }
    printf("host dxt%u round %u %s %.3f ms\n",format,round,mode?"texel":"block",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
}
'''
with tempfile.TemporaryDirectory(prefix='dxt-texel-',dir=root) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O3',*(['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if '--asan' in sys.argv else []),'-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),str(root/'GPU/Common/TextureDecoder.cpp'),str(root/'ext/xxhash.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
