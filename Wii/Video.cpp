// SPDX-License-Identifier: GPL-2.0-or-later
#include <gccore.h>
#include <cstdlib>
#include <cstdio>
#include "Wii/Perf.h"
#include <malloc.h>
#include <algorithm>
#include <cstring>
#include <ogc/libversion.h>
#include "Core/HLE/sceDisplay.h"
#include "Core/MemMap.h"
#include "Wii/Pixel.h"
#include "Wii/Video.h"
#include "Wii/GXBridge.h"
#include "Wii/GuestCopy.h"
#include "Wii/Settings.h"
#include "Wii/Presentation.h"
#include "Wii/Diagnostics.h"
#include "Common/Log.h"

static GXRModeObj *mode;
static GXRModeObj loaderMode;
static bool videoStarted=false;
static void *buffers[2];
static unsigned backBuffer;
static unsigned xfbBytes=0;

static void AcquireBackBuffer() {
  VIDEO_WaitForFlush();
  backBuffer = VIDEO_GetCurrentFramebuffer() == buffers[0] ? 1 : 0;
}
static void PresentBackBuffer() {
  VIDEO_SetNextFramebuffer(buffers[backBuffer]);
  VIDEO_Flush();
  // A retrace alone does not guarantee the pending register update was latched.
  VIDEO_WaitForFlush();
  AcquireBackBuffer();
}

#ifndef _LIBOGC2_REVISION_
#error This Wii port requires libogc2 headers and libraries.
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error The Wii host must be compiled as big endian.
#endif


// Retain only 480i60 and progressive outputs. Auto never selects PAL interlace.
static GXRModeObj *RequestedMode(int video) {
  switch (video) {
  case 1: return &TVNtsc480IntDf;
  case 2: return VIDEO_HaveComponentCable() ? &TVNtsc480Prog : nullptr;
  case 3: return nullptr;
  case 4: return VIDEO_HaveComponentCable() ? &TVPal576ProgScale : nullptr;
  default: return VIDEO_HaveComponentCable() ? &TVNtsc480Prog : &TVNtsc480IntDf;
  }
}

bool WiiVideoModeSupported(int video) {
  return video >= 0 && video <= 4 && RequestedMode(video) != nullptr;
}

static bool ConfigureMode(GXRModeObj *newMode) {
  if (!newMode) return false;
  if(mode==newMode&&buffers[0]&&buffers[1])return true;
  const unsigned needed=VIDEO_PadFramebufferWidth(newMode->fbWidth)*
    unsigned(newMode->xfbHeight+2)*VI_DISPLAY_PIX_SZ;
  const bool reuse=buffers[0]&&buffers[1]&&needed<=xfbBytes;
  void *newBuffers[2]={reuse?buffers[0]:nullptr,reuse?buffers[1]:nullptr};
  if(!reuse)for(void *&buffer:newBuffers) {
    buffer=memalign(32,needed);
    if(!buffer) {
      for(void *allocated:newBuffers)if(allocated)free(MEM_K1_TO_K0(allocated));
      return false;
    }
    buffer=MEM_K0_TO_K1(buffer);
    VIDEO_ClearFrameBuffer(newMode,buffer,COLOR_BLACK);
  }

  VIDEO_SetBlack(true);
  VIDEO_Flush();
  VIDEO_WaitForFlush();
  VIDEO_WaitVSync();
  if(reuse)for(void *buffer:newBuffers)VIDEO_ClearFrameBuffer(newMode,buffer,COLOR_BLACK);
  VIDEO_Configure(newMode);
  VIDEO_SetNextFramebuffer(newBuffers[0]);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  if (newMode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
  VIDEO_WaitForFlush(); // Old XFBs must no longer be pending before freeing them.

  void *oldBuffers[2]={buffers[0],buffers[1]};
  if(!reuse)xfbBytes=needed;
  buffers[0] = newBuffers[0]; buffers[1] = newBuffers[1];
  mode = newMode; backBuffer = 1;
  VIDEO_SetBlack(false);
  VIDEO_Flush();
  console_init(buffers[0], 20, 20, mode->fbWidth - 40, mode->xfbHeight - 40,
               mode->fbWidth * VI_DISPLAY_PIX_SZ);
  // The console and VI now both refer to the replacement XFBs.
  if(!reuse)for(void *buffer:oldBuffers)if(buffer)free(MEM_K1_TO_K0(buffer));
  return true;
}

bool WiiVideoApplyMode(int video) {
  return WiiVideoModeSupported(video) && ConfigureMode(RequestedMode(video));
}

bool WiiVideoInit() {
  VIDEO_Init();
  loaderMode=*VIDEO_GetPreferredMode(nullptr);
  videoStarted=true;
  // Storage/settings are not mounted yet, so bootstrap in Auto. Main.cpp
  // applies the persisted selection immediately after loading wii.ini.
  return WiiVideoApplyMode(0);
}

// XFB-only overlay: never writes PSP VRAM or changes the GX pipeline.
static void DrawPerformanceOverlay(bool gxScanout) {
  if(!Wii::GetSettings().performanceOverlay)return;
  const auto &p=Wii::GetPerfSnapshot();
  char line[96];
  WiiMenuRect(12,12,616,78,0x301010);
  if(!p.valid) {
    WiiMenuText(20,20,"performance: sampling...",0xffffff);
  } else {
    std::snprintf(line,sizeof(line),"speed %.1f%%  fps %.1f  psp %d mhz",p.speed,p.fps,p.clockMHz);
    WiiMenuText(20,20,line,0xffffff);
    std::snprintf(line,sizeof(line),"cpu/hle %.1f%%  ge %.1f%%  heap free %.2f mib",p.cpu,p.ge,double(p.heapFree)/1048576.0);
    WiiMenuText(20,42,line,0xffffff);
  }
  std::snprintf(line,sizeof(line),"gx %s  scanout %s",Wii::GXStatus(),gxScanout?"gx":"cpu");
  WiiMenuText(20,64,line,0xffffff);
}

bool WiiVideoPresent() {
  PSPPointer<u8> frame{};
  u32 stride, format;
  if (!__DisplayGetFramebuf(&frame, &stride, &format, 0) || !frame.ptr || format > 3 || stride < 480)
    return false;
  AcquireBackBuffer();
  if(Wii::GXPresent(frame.ptr,stride,format,mode,buffers[backBuffer])) { DrawPerformanceOverlay(true);PresentBackBuffer();return true; }
  VIDEO_ClearFrameBuffer(mode,buffers[backBuffer],COLOR_BLACK);
  const unsigned bytesPerPixel = format == 3 ? 4 : 2;
  // Translate each scanline separately: VRAM mirrors are not contiguous backing stores.
  const unsigned width = mode->fbWidth & ~1u;
  const auto rect=Wii::DisplayRect(mode->fbWidth,mode->xfbHeight,Wii::GetSettings().aspect!=0);
  const unsigned height=rect.height,top=rect.top;
  u32 *out = static_cast<u32 *>(buffers[backBuffer]);
  alignas(32) uint8_t scanline[480*4];
  for (unsigned y = 0; y < height; ++y) {
    const unsigned row = y * 272 / height;
    const u32 address = frame.ptr + row * stride * bytesPerPixel;
    if (!Memory::IsValidRange(address, 480 * bytesPerPixel)) return false;
    const u8 *src = Memory::GetPointerUnchecked(address);
    if(Wii::addressSpace.Span(address)<480*bytesPerPixel) {
      if(!Wii::CopyFromGuest(scanline,address,480*bytesPerPixel))return false;
      src=scanline;
    }
    for (unsigned x = 0; x < width; x += 2) {
      const unsigned sx0 = x * 480 / width, sx1 = (x + 1) * 480 / width;
      out[((y + top) * mode->fbWidth + x) / 2] = Wii::ToYUYV(
        Wii::DecodePixel(src + sx0 * bytesPerPixel, format),
        Wii::DecodePixel(src + sx1 * bytesPerPixel, format));
    }
  }
  DrawPerformanceOverlay(false);
  PresentBackBuffer();
  return true;
}

// Three small guest-memory probes per heartbeat, only in diagnostic mode.
// These are sample colors, not a whole-frame black-screen detector.
void WiiVideoReport() {
  if(Wii::DiagnosticLevel()<1)return;
  PSPPointer<u8> frame{},latched{};u32 stride=0,format=0,latchedStride=0,latchedFormat=0;
  if(!__DisplayGetFramebuf(&frame,&stride,&format,0))return;
  __DisplayGetFramebuf(&latched,&latchedStride,&latchedFormat,1);
  const unsigned bpp=format==3?4:2;
  uint32_t colors[3]={};unsigned valid=0;
  const unsigned xs[]={0,240,479},ys[]={0,136,271};
  if(frame.ptr&&format<=3&&stride>=480&&stride<=2048)for(unsigned i=0;i<3;++i) {
    const uint64_t address=uint64_t(frame.ptr)+(uint64_t(ys[i])*stride+xs[i])*bpp;
    uint8_t pixel[4]={};
    if(address<=0xffffffffULL&&Wii::CopyFromGuest(pixel,uint32_t(address),bpp)) {
      colors[i]=Wii::DecodePixel(pixel,format);valid|=1u<<i;
    }
  }
  INFO_LOG(BOOT,"DISPLAY frame=%08x stride=%u format=%u latched=%08x/%u/%u sample-valid=%x rgb=%06x,%06x,%06x gx-enabled=%u",
    uint32_t(frame.ptr),stride,format,uint32_t(latched.ptr),latchedStride,latchedFormat,valid,
    colors[0],colors[1],colors[2],unsigned(Wii::GXEnabled()));
}

void WiiVideoShutdown() {
  Wii::GXShutdown();
  if(!videoStarted)return;
  VIDEO_SetBlack(true);
  VIDEO_Flush();
  VIDEO_WaitForFlush();
  VIDEO_WaitVSync();
  // Restore the system's standard signal, not the app's forced 576p mode.
  // Keep the display black and leave XFB allocations alive until libogc's
  // reload stub takes over. Its console/retrace cleanup may still use them.
  VIDEO_Configure(&loaderMode);
  VIDEO_SetBlack(true);
  VIDEO_Flush();
  VIDEO_WaitForFlush();
  VIDEO_WaitVSync();
  if(loaderMode.viTVMode & VI_NON_INTERLACE)VIDEO_WaitVSync();
  videoStarted=false;
}

#include "Wii/MenuFont.h"
// UI coordinates are always 640x480, independent of PAL/NTSC scanout size.
void WiiMenuRect(int x, int y, int w, int h, unsigned rgb) {
  Wii::PresentationRect rect;
  if(!Wii::MenuPixelRect(x,y,w,h,mode->fbWidth,mode->xfbHeight,Wii::GetSettings().aspect!=0,rect))return;
  const int x0=rect.left,x1=rect.left+rect.width,y0=rect.top,y1=rect.top+rect.height;
  u32 *out = static_cast<u32 *>(buffers[backBuffer]);
  const u32 color = Wii::ToYUYV(rgb, rgb);
  if (x0 >= x1 || y0 >= y1) return;
  for (int row=y0; row<y1; ++row) {
    int col=x0;
    if (col&1) {
      u32 &pair=out[(row*mode->fbWidth+col)/2];
      pair=Wii::ToYUYV(Wii::FromYUYV(pair,false),rgb); ++col;
    }
    for (;col+1<x1;col+=2) out[(row*mode->fbWidth+col)/2]=color;
    if(col<x1) {
      u32 &pair=out[(row*mode->fbWidth+col)/2];
      pair=Wii::ToYUYV(rgb,Wii::FromYUYV(pair,true));
    }
  }
}
void WiiMenuBegin() {
  AcquireBackBuffer();
  VIDEO_ClearFrameBuffer(mode,buffers[backBuffer],COLOR_BLACK);
  WiiMenuRect(0,0,640,480,0xf4e7e7);
}
void WiiMenuText(int x, int y, const char *text, unsigned rgb, int scale) {
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p, x+=8*scale) {
    const unsigned char c = *p >= 32 && *p < 127 ? *p : '?';
    for (int row=0; row<16; ++row) {
      const unsigned bits = menuFont[c][row];
      for (int col=0; col<8; ++col) if (bits & (0x80>>col))
        WiiMenuRect(x+col*scale, y+row*scale, scale, scale, rgb);
    }
  }
}
void WiiMenuEnd() {
  PresentBackBuffer();
}
