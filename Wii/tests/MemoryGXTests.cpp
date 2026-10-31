// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Core/MemMap.h"
#include "Wii/GuestSurface.h"
#include "Wii/GXDirtyUpload.h"
#include "Wii/InstructionFetch.h"
namespace Wii { AddressSpace addressSpace; }
namespace Memory { u32 g_MemorySize=32*1024*1024; u8 *base=nullptr; }
extern "C" int __lsan_is_turned_off() {return 1;}
int main() {
  std::vector<uint8_t> ram(32*1024*1024),vram(2*1024*1024),scratch(16384);
  auto &map=Wii::addressSpace;
  map.ram=ram.data();map.ramSize=ram.size();map.vram=vram.data();map.scratch=scratch.data();
  {
    Wii::InstructionFetch fetch;u32 word=0;
    for(u32 pc:{0x08000ffcu,0x08001000u,0x48001000u,0x00013ffcu,0x041ffffcu,0x04200000u}) {
      Memory::WriteUnchecked_U32(0x12345678,pc);
      assert(fetch.ReadCommand(pc,word)&&word==0x12345678);
      Memory::WriteUnchecked_U32(0x87654321,pc);
      assert(fetch.ReadCommand(pc,word)&&word==0x87654321);
    }
    for(u32 pc:{0u,0x00014000u,0x09fffffeu,0x0a000000u,0xfffffffcu}) {
      word=0xfeedbeef;assert(!fetch.ReadCommand(pc,word)&&word==0xfeedbeef);
    }
  }
  // Compare the page fast path to the original bounded translation, including
  // every page boundary, the scratchpad subpage, holes and all four aliases.
  uint32_t random=0x12345678;
  std::vector<uint32_t> addresses={0,0x10000,0x13fff,0x14000,0x3ffffff,0x4000000,0x41fffff,0x4200000,0x47fffff,0x4800000,0x8000000,0x9ffffff,0xa000000};
  for(unsigned p=0;p<1024;++p) {addresses.push_back(p<<20);addresses.push_back((p<<20)+0xfffff);}
  for(unsigned i=0;i<100000;++i) {random=random*1664525+1013904223;addresses.push_back(random);}
  for(uint32_t a:addresses) for(uint32_t alias:{0u,0x40000000u,0x80000000u,0xc0000000u}) {
    a=(a&0x3fffffff)|alias;map.pagesReady=false;auto expected=map.Translate(a);
    if(!map.pages[0x80])map.RebuildPages();
    map.pagesReady=true;
    assert(map.Translate(a)==expected);
  }
  for(uint32_t alias:{0u,0x40000000u,0x80000000u,0xc0000000u}) {
    assert(Memory::ValidSize(alias|0x09fffffc,32)==4);
    assert(!Memory::IsValidRange(alias|0x09fffffc,8));
    assert(Memory::ValidSize(alias|0x047ffffc,32)==4);
    assert(!Memory::IsValidRange(alias|0x047ffffc,8));
    assert(Memory::IsValidRange(alias|0x041ffffc,8));
    assert(Memory::ValidSize(alias|0x13ffc,32)==4);
  }
  uint8_t data[16],out[16];for(unsigned i=0;i<16;++i)data[i]=i+1;
  assert(Wii::CopyToGuest(0x441ffff8,data,16));
  assert(Wii::CopyFromGuest(out,0x841ffff8,16));assert(!memcmp(out,data,16));
  assert(!map.Contiguous(0x041ffff8,16));
  Memory::MemcpyUnchecked(0x441ffff8,static_cast<const void *>(data),16);
  Memory::MemcpyUnchecked(static_cast<void *>(out),0x841ffff8,16);
  assert(!memcmp(out,data,16));
  Memory::MemcpyUnchecked(0x08000000u,0x441ffff8u,16);
  assert(!memcmp(ram.data(),data,16));
  memset(out,0x67,16);assert(!Wii::CopyFromGuest(out,0x047ffff8,16));
  for(auto b:out)assert(b==0x67);
  memset(vram.data()+vram.size()-8,0x67,8);
  assert(!Wii::CopyToGuest(0x047ffff8,data,16));
  for(unsigned i=0;i<8;++i)assert(vram[vram.size()-8+i]==0x67);
  uint32_t address;
  assert(Wii::SurfaceRow(0x041ffff0,512,0,0,480,4,address));
  assert(!Wii::ValidSurface(0x09fffff0,512,0,0,480,272,4));
  assert(!Wii::SurfaceRow(0x04000000,0xffffffff,0,0xffffffff,480,4,address));
  assert(!Wii::SurfaceRow(0x04000000,512,0xffffffff,0,480,4,address));
  assert(!Wii::ValidSurface(0x04000000,512,0,0xffffffff,480,272,4));
  // Exact output against reference packers in all PSP formats and upload modes.
  for(unsigned format=0;format<4;++format) for(unsigned mode=0;mode<2;++mode) {
    const bool native=mode==1&&format<3;
    const unsigned sourceBpp=format==3?4:2,bpp=native?2:4;
    const unsigned width=480,height=8,bytes=width*height*bpp,tileBytes=16*bpp;
    std::vector<uint8_t> source(width*height*sourceBpp),dest(bytes+64,0x67),reference(bytes);
    for(auto &b:source){random=random*1664525+1013904223;b=random>>24;}
    auto referenceUpload=[&]() {
      for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        auto p=source.data()+(y*width+x)*sourceBpp;
        if(native) {auto v=Wii::NativeGX16(p[0]|uint16_t(p[1])<<8,format);unsigned at=Wii::GX565TileOffset(x,y,width);reference[at]=v>>8;reference[at+1]=v;}
        else Wii::PackGXPixel(reference.data(),width,x,y,Wii::DecodePixel(p,format));
      }
    };
    unsigned flushed=0,calls=0;
    auto upload=[&](bool force) {
      unsigned changed=0;
      for(unsigned y=0;y<height;y+=4) {
        const uint8_t *rows[4];for(unsigned j=0;j<4;++j)rows[j]=source.data()+(y+j)*width*sourceBpp;
        changed+=Wii::UploadDirtyBand(dest.data()+32,width,y,rows,format,native,force,
          [&](uint8_t *p,unsigned n) {assert((p-(dest.data()+32))%32==0);assert(n%32==0);assert(p>=dest.data()+32&&p+n<=dest.data()+32+bytes);flushed+=n;++calls;});
      }
      return changed;
    };
    referenceUpload();assert(upload(true)==bytes);assert(flushed==bytes&&calls==2);
    assert(!memcmp(dest.data()+32,reference.data(),bytes));
    flushed=calls=0;assert(upload(false)==0);assert(flushed==0&&calls==0);
    // A high channel bit must survive both 16-bit and 32-bit output conversion.
    source[(4*width+7)*sourceBpp]^=0x10;
    referenceUpload();assert(upload(false)==tileBytes);assert(flushed==tileBytes&&calls==1);
    assert(!memcmp(dest.data()+32,reference.data(),bytes));
    // Adjacent and separated changes exercise flush run coalescing.
    source[0]^=0x10;source[4*sourceBpp]^=0x10;source[12*sourceBpp]^=0x10;
    flushed=calls=0;referenceUpload();assert(upload(false)==3*tileBytes);assert(flushed==3*tileBytes&&calls==2);
    assert(!memcmp(dest.data()+32,reference.data(),bytes));
    // Random sparse writes: exact source-tile reuse must equal independent
    // per-pixel packing, including changes in alpha bits ignored by scanout.
    auto shadow=source;
    for(unsigned iteration=0;iteration<200;++iteration) {
      random=random*1664525+1013904223;
      const unsigned index=random%source.size();source[index]^=uint8_t(1u<<((random>>24)&7));
      unsigned comparisons=0;
      for(unsigned y=0;y<height;y+=4) {
        const uint8_t *rows[4];for(unsigned row=0;row<4;++row)rows[row]=source.data()+(y+row)*width*sourceBpp;
        Wii::UploadDirtyBandReuse(dest.data()+32,width,y,rows,format,native,false,
          [](uint8_t *,unsigned n){assert(n%32==0);},
          [&](unsigned x){
            ++comparisons;bool match=true;
            for(unsigned row=0;row<4;++row) {
              const unsigned offset=((y+row)*width+x)*sourceBpp;
              if(memcmp(shadow.data()+offset,source.data()+offset,4*sourceBpp))match=false;
              memcpy(shadow.data()+offset,source.data()+offset,4*sourceBpp);
            }
            return match;
          });
      }
      assert(comparisons==width*height/16);
      referenceUpload();assert(!memcmp(dest.data()+32,reference.data(),bytes));
    }
    const uint8_t *rows[4];for(unsigned row=0;row<4;++row)rows[row]=source.data()+row*width*sourceBpp;
    assert(Wii::UploadDirtyBandReuse(dest.data()+32,width,0,rows,format,native,true,
      [](uint8_t *,unsigned){},[](unsigned)->bool{assert(false);return true;})==width*4*bpp);
    for(unsigned i=0;i<32;++i)assert(dest[i]==0x67&&dest[32+bytes+i]==0x67);
  }
  map={};assert(!map.Translate(0x08000000));assert(!Memory::IsValidAddress(0x08000000));
  puts("pass: page/reference differential, aliases, range preflight, mirrored copies, surface overflow, all dirty upload formats, flush coalescing and guards");
}
