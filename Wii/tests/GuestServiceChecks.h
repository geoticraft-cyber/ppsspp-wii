// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelThread.h"
#include "Wii/LogFilter.h"
#include <cassert>
#include <cstdio>
static void GuestServiceChecks(){
  static_assert(sizeof(AudioChannel)<128,"channel must contain metadata only");
  assert(!g_Config.bEnableSound);
  assert(CoreTiming::GetScheduledEventsSummary().find("GuestSinkTick")==std::string::npos);
  auto &c=chans[0];c.clear();c.reserved=true;c.sampleCount=128;c.sampleAddress=0xdead0000;
  assert(__AudioEnqueue(c,0,false)==128&&c.queuedSamples==256);
  assert(CoreTiming::GetScheduledEventsSummary().find("GuestSinkTick")!=std::string::npos);
  assert(__AudioEnqueue(c,0,false)==SCE_ERROR_AUDIO_CHANNEL_BUSY);
  const u32 state=sceKernelSuspendDispatchThread();
  assert(__AudioEnqueue(c,0,true)==SCE_KERNEL_ERROR_CAN_NOT_WAIT);
  sceKernelResumeDispatchThread(state);
  __AudioUpdate();assert(c.queuedSamples==128);__AudioUpdate();assert(!c.queuedSamples);
  c.sampleAddress=0;assert(__AudioEnqueue(c,0,false)==128&&!c.queuedSamples);c.reset();
  const auto end=GetFunc("sceSasCore",0x68a46b95);assert(end&&end->func);end->func();assert(currentMIPS->r[2]==0xffffffffu);
  currentMIPS->r[5]=512;GetFunc("sceSasCore",0xd1e0a01e)->func();GetFunc("sceSasCore",0xbd11b7c2)->func();assert(currentMIPS->r[2]==512);
  const auto codec=GetFunc("sceAtrac3plus",0x7db31251);assert(codec&&codec->func);codec->func();assert(currentMIPS->r[2]==SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED);
  assert(Wii::HiddenServiceText("AUDIO buffer")&&Wii::HiddenServiceText("sceAudioOutput")&&Wii::HiddenServiceText("Sound backend"));
  assert(!Wii::HiddenServiceText("GX upload complete")&&!Wii::HiddenServiceText("CPU dispatch"));
  std::vector<u8> stateBytes(8192);u8 *cursor=stateBytes.data();
  c.reserved=true;c.queuedSamples=64;
  {PointerWrap p(&cursor,PointerWrap::MODE_WRITE);__AudioDoState(p);assert(!p.error);}
  c.clear();cursor=stateBytes.data();
  {PointerWrap p(&cursor,PointerWrap::MODE_READ);__AudioDoState(p);assert(!p.error);}
  assert(c.reserved&&c.queuedSamples==64);c.clear();
  GetFunc("sceMp4",0x68651cbc)->func();assert(currentMIPS->r[2]==0);
  GetFunc("sceMp4",0x5eb65f26)->func();assert(currentMIPS->r[2]==1);
  printf("PASS: metadata-only guest sink: %zu bytes for all channels, busy/drain/release, dispatch-disabled wait, no sample reads, completion flags and unsupported codecs\n",sizeof(chans));
}
