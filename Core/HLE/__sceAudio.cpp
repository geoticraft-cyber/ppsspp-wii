// Copyright (c) 2012- PPSSPP Project. GPL-2.0-or-later.
// Guest wait/counter compatibility; no PCM, mixer, resampler or host output.
#include "Common/ChunkFile.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/sceKernelThread.h"
#include <algorithm>
int mixFrequency=44100;
static int updateEvent=-1,interval=0;
static bool scheduled=false;
static void ClockChanged(){interval=int(usToCycles(1000000ULL)*64/44100);}
void __AudioUpdate(bool){for(auto &chan:chans)if(chan.reserved){
  __AudioWakeThreads(chan,0,64);chan.queuedSamples-=std::min(chan.queuedSamples,128u);
}}
static void Tick(u64,int late){
  scheduled=false;__AudioUpdate();
  for(const auto &c:chans)if(c.reserved&&(c.queuedSamples||!c.waitingThreads.empty())){
    CoreTiming::ScheduleEvent(std::max(1,interval-late),updateEvent,0);scheduled=true;break;
  }
}
void __AudioInit(){scheduled=false;mixFrequency=44100;ClockChanged();for(auto &c:chans)c.clear();
  updateEvent=CoreTiming::RegisterEvent("GuestSinkTick",Tick);
  CoreTiming::RegisterMHzChangeCallback(ClockChanged);
}
void __AudioShutdown(){scheduled=false;for(auto &c:chans)c.clear();}
void __AudioDoState(PointerWrap &p){auto s=p.Section("GuestSink",1);if(!s)return;
  p.Do(updateEvent);p.Do(mixFrequency);p.Do(scheduled);for(auto &c:chans)c.DoState(p);
  CoreTiming::RestoreRegisterEvent(updateEvent,"GuestSinkTick",Tick);ClockChanged();
}
u32 __AudioEnqueue(AudioChannel &chan,int index,bool blocking){
  u32 result=chan.sampleCount;
  if(!chan.sampleAddress&&(index==PSP_AUDIO_CHANNEL_SRC||index==PSP_AUDIO_CHANNEL_OUTPUT2))result=0;
  if(chan.queuedSamples){
    if(!blocking)return SCE_ERROR_AUDIO_CHANNEL_BUSY;
    if(!__KernelIsDispatchEnabled())return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
    chan.waitingThreads.push_back({__KernelGetCurThread(),int(chan.queuedSamples/2)});
    __KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL,index+1,result,0,false,"guest service wait");
  }
  if(chan.sampleAddress){
    chan.queuedSamples=unsigned(std::min<uint64_t>(0x7fffffffu,uint64_t(chan.queuedSamples)+chan.sampleCount*2));
    if(!scheduled){CoreTiming::ScheduleEvent(interval,updateEvent,0);scheduled=true;}
  }
  return result;
}
void __AudioSetOutputFrequency(int value){mixFrequency=value;}
void __AudioWakeThreads(AudioChannel &chan, int result, int step) {
	u32 error;
	bool wokeThreads = false;
	for (size_t w = 0; w < chan.waitingThreads.size(); ++w) {
		AudioChannelWaitInfo &waitInfo = chan.waitingThreads[w];
		waitInfo.numSamples -= step;

		// If it's done (there will still be samples on queue) and actually still waiting, wake it up.
		u32 waitID = __KernelGetWaitID(waitInfo.threadID, WAITTYPE_AUDIOCHANNEL, error);
		if (waitInfo.numSamples <= 0 && waitID != 0) {
			// (void)0;
			u32 ret = result == 0 ? __KernelGetWaitValue(waitInfo.threadID, error) : SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
			__KernelResumeThreadFromWait(waitInfo.threadID, ret);
			wokeThreads = true;

			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
		// This means the thread stopped waiting, so stop trying to wake it.
		else if (waitID == 0)
			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
	}

	if (wokeThreads) {
		__KernelReSchedule("guest service drain");
	}
}

void __AudioWakeThreads(AudioChannel &chan, int result) {
	__AudioWakeThreads(chan, result, 0x7FFFFFFF);
}

