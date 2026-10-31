extern "C" int __lsan_is_turned_off() {return 1;}
#include "Wii/Presentation.h"
#include "Wii/GXUpload.h"
#include "Wii/Pixel.h"
#include "Common/ColorConv.h"
#include <cassert>
#include <vector>
#include <cstdio>
static uint32_t RGB(uint32_t abgr) {return (abgr&255)<<16|(abgr&0xff00)|((abgr>>16)&255);}
int main() {
  for(unsigned height:{480u,576u}) {
    auto wide=Wii::DisplayRect(640,height,true),standard=Wii::DisplayRect(640,height,false);
    assert(wide.height==height&&wide.top==0&&standard.height==height*3/4);
    assert(standard.top==(height-standard.height)/2&&standard.width==wide.width);
  }
  const auto efb=Wii::DisplayRect(640,480,false),xfb=Wii::DisplayRect(640,576,false);
  assert(efb.height*6==xfb.height*5&&efb.top*6==xfb.top*5);
  std::vector<uint8_t> src(480*4),dest(480*272*4+64,0x67);
  for(unsigned format=0;format<3;++format) {
    for(unsigned v=0;v<65536;++v) {
      const uint16_t native=Wii::NativeGX16(v,format);
      uint32_t r,g,b;
      if(format==0) {r=(native>>11)&31;g=(native>>5)&63;b=native&31;r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);}
      else if(format==1) {assert(native&0x8000);r=(native>>10)&31;g=(native>>5)&31;b=native&31;r=(r<<3)|(r>>2);g=(g<<3)|(g>>2);b=(b<<3)|(b>>2);}
      else {assert((native&0xf000)==0x7000);r=((native>>8)&15)*17;g=((native>>4)&15)*17;b=(native&15)*17;}
      const uint32_t expected=RGB(format==0?RGB565ToRGBA8888(v):format==1?RGBA5551ToRGBA8888(v):RGBA4444ToRGBA8888(v));
      assert((r<<16|g<<8|b)==expected);
      uint8_t p[]={uint8_t(v),uint8_t(v>>8)};assert(Wii::DecodePixel(p,format)==expected);
    }
    for(unsigned y=0;y<272;++y) {
      for(unsigned x=0;x<480;++x) {const unsigned v=x*109+y*37;src[x*2]=v;src[x*2+1]=v>>8;}
      Wii::UploadNative16Row(dest.data()+32,480,y,src.data(),format);
      for(unsigned x=0;x<480;++x) {
        unsigned at=32+Wii::GX565TileOffset(x,y,480);
        uint16_t actual=uint16_t(dest[at])<<8|dest[at+1];
        assert(actual==Wii::NativeGX16(uint16_t(src[x*2])|uint16_t(src[x*2+1])<<8,format));
      }
    }
  }
  for(unsigned y=0;y<272;++y) {
    for(unsigned x=0;x<480*4;++x)src[x]=x*13+y*17;
    Wii::UploadRGBA8Row(dest.data()+32,480,y,src.data());
    for(unsigned x=0;x<480;++x)assert(Wii::UnpackGXPixel(dest.data()+32,480,x,y)==Wii::DecodePixel(src.data()+x*4,3));
  }
  for(unsigned i=0;i<32;++i)assert(dest[i]==0x67&&dest[dest.size()-1-i]==0x67);
  puts("PASS: PAL EFB/XFB aspect agreement, 196608 exhaustive native-color cases, all 480x272 upload tiles and guards");
}
