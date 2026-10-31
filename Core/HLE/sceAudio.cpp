// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/ChunkFile.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/Reporting.h"

const u32 PSP_AUDIO_SAMPLE_MAX = 65536 - 64;
const int PSP_AUDIO_ERROR_SRC_FORMAT_4 = 0x80000003;
const int AUDIO_ROUTING_SPEAKER_OFF = 0;
const int AUDIO_ROUTING_SPEAKER_ON = 1;
int defaultRoutingMode = AUDIO_ROUTING_SPEAKER_ON;
int defaultRoutingVolMode = AUDIO_ROUTING_SPEAKER_ON;

void AudioChannel::DoState(PointerWrap &p)
{
	auto s = p.Section("GuestChannel", 1, 2);
	if (!s)
		return;

	p.Do(reserved);
	p.Do(sampleAddress);
	p.Do(sampleCount);
	p.Do(leftVolume);
	p.Do(rightVolume);
	p.Do(format);
	p.Do(waitingThreads);
	if (s >= 2) {
		p.Do(defaultRoutingMode);
		p.Do(defaultRoutingVolMode);
	}
	p.Do(queuedSamples);
}

void AudioChannel::reset()
{
	__AudioWakeThreads(*this, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED);
	clear();
}

void AudioChannel::clear()
{
	reserved = false;
	leftVolume = 0;
	rightVolume = 0;
	format = 0;
	sampleAddress = 0;
	sampleCount = 0;
	queuedSamples=0;
	waitingThreads.clear();
}

// There's a second Audio api called Audio2 that only has one channel, I guess the 8 channel api was overkill.
// We simply map it to an extra channel after the 8 channels, since they can be used concurrently.

// The extra channel is for SRC/Output2/Vaudio.
AudioChannel chans[PSP_AUDIO_CHANNEL_MAX + 1];

// Enqueues the buffer pointer on the channel. If channel buffer queue is full (2 items?) will block until it isn't.
// For solid audio output we'll need a queue length of 2 buffers at least, we'll try that first.

// Not sure about the range of volume, I often see 0x800 so that might be either
// max or 50%?

static u32 sceAudioOutputBlocking(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		(void)0;
		if (vol >= 0) {
			chans[chan].leftVolume = vol;
			chans[chan].rightVolume = vol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, true);
	}
}

static u32 sceAudioOutputPannedBlocking(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	int result = 0;
	// For some reason, this is the only one that checks for negative.
	if (leftvol > 0xFFFF || rightvol > 0xFFFF || leftvol < 0 || rightvol < 0) {
		(void)0;
		result = SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		result = SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		(void)0;
		result = SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		if (leftvol >= 0) {
			chans[chan].leftVolume = leftvol;
		}
		if (rightvol >= 0) {
			chans[chan].rightVolume = rightvol;
		}
		chans[chan].sampleAddress = samplePtr;
		result = __AudioEnqueue(chans[chan], chan, true);
	}

	(void)0;
	return result;

}

static u32 sceAudioOutput(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		(void)0;
		if (vol >= 0) {
			chans[chan].leftVolume = vol;
			chans[chan].rightVolume = vol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, false);
	}
}

static u32 sceAudioOutputPanned(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		(void)0;
		if (leftvol >= 0) {
			chans[chan].leftVolume = leftvol;
		}
		if (rightvol >= 0) {
			chans[chan].rightVolume = rightvol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, false);
	}
}

static int sceAudioGetChannelRestLen(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	int remainingSamples = (int)chans[chan].queuedSamples / 2;
	(void)0;
	return remainingSamples;
}

static int sceAudioGetChannelRestLength(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	int remainingSamples = (int)chans[chan].queuedSamples / 2;
	(void)0;
	return remainingSamples;
}

static u32 GetFreeChannel() {
	for (u32 i = PSP_AUDIO_CHANNEL_MAX - 1; i > 0; --i) {
		if (!chans[i].reserved)
			return i;
	}
	return -1;
}

static u32 sceAudioChReserve(int chan, u32 sampleCount, u32 format) {
	if (chan < 0) {
		chan = GetFreeChannel();
		if (chan < 0) {
			(void)0;
			return SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE;
		}
	}
	if ((u32)chan >= PSP_AUDIO_CHANNEL_MAX)	{
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	if ((sampleCount & 63) != 0 || sampleCount == 0 || sampleCount > PSP_AUDIO_SAMPLE_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED;
	}
	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_FORMAT;
	}
	if (chans[chan].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	(void)0;
	chans[chan].sampleCount = sampleCount;
	chans[chan].format = format;
	chans[chan].reserved = true;
	chans[chan].leftVolume = 0;
	chans[chan].rightVolume = 0;
	return chan;
}

static u32 sceAudioChRelease(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	if (!chans[chan].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	(void)0;
	chans[chan].reset();
	chans[chan].reserved = false;
	return 1;
}

static u32 sceAudioSetChannelDataLen(u32 chan, u32 len) {
	int result = 0;
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		result = SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		(void)0;
		result = SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else if ((len & 63) != 0 || len == 0 || len > PSP_AUDIO_SAMPLE_MAX) {
		(void)0;
		result = SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED;
	} else {
		chans[chan].sampleCount = len;
	}
	(void)0;
	return result;
}

static u32 sceAudioChangeChannelConfig(u32 chan, u32 format) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		(void)0;
		chans[chan].format = format;
		return 0;
	}
}

static u32 sceAudioChangeChannelVolume(u32 chan, u32 leftvol, u32 rightvol) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		(void)0;
		chans[chan].leftVolume = leftvol;
		chans[chan].rightVolume = rightvol;
		return 0;
	}
}

static u32 sceAudioInit() {
	(void)0;
	// Don't need to do anything
	return 0;
}

static u32 sceAudioEnd() {
	(void)0;
	// Don't need to do anything
	return 0;
}

static u32 sceAudioOutput2Reserve(u32 sampleCount) {
	if (sampleCount < 17 || sampleCount > 4111) {
		(void)0;
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	} else {
		(void)0;
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleCount = sampleCount;
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].format = PSP_AUDIO_FORMAT_STEREO;
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved = true;
	}
	return 0;
}

static u32 sceAudioOutput2OutputBlocking(u32 vol, u32 dataPtr) {
	// Note: 0xFFFFF, not 0xFFFF!
	if (vol > 0xFFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	}
	(void)0;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].leftVolume = vol;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].rightVolume = vol;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleAddress = dataPtr;
	return __AudioEnqueue(chans[PSP_AUDIO_CHANNEL_OUTPUT2], PSP_AUDIO_CHANNEL_OUTPUT2, true);
}

static u32 sceAudioOutput2ChangeLength(u32 sampleCount) {
	if (!chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	(void)0;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleCount = sampleCount;
	return 0;
}

static u32 sceAudioOutput2GetRestSample() {
	if (!chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	(void)0;
	return (u32) chans[PSP_AUDIO_CHANNEL_OUTPUT2].queuedSamples / 2;
}

static u32 sceAudioOutput2Release() {
	(void)0;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].reset();
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved = false;
	return 0;
}

static u32 sceAudioSetFrequency(u32 freq) {
	if (freq == 44100 || freq == 48000) {
		(void)0;
		__AudioSetOutputFrequency(freq);
		return 0;
	} else {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_FREQUENCY;
	}
}

static u32 sceAudioSetVolumeOffset() {
	(void)0;
	return 0;
}

static u32 sceAudioSRCChReserve(u32 sampleCount, u32 freq, u32 format) {
	if (format == 4) {
		(void)0;
		return PSP_AUDIO_ERROR_SRC_FORMAT_4;
	} else if (format != 2) {
		(void)0;
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (sampleCount < 17 || sampleCount > 4111) {
		(void)0;
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (chans[PSP_AUDIO_CHANNEL_SRC].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	} else {
		(void)0;
		chans[PSP_AUDIO_CHANNEL_SRC].reserved = true;
		chans[PSP_AUDIO_CHANNEL_SRC].sampleCount = sampleCount;
		chans[PSP_AUDIO_CHANNEL_SRC].format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
		__AudioSetOutputFrequency(freq);
	}
	return 0;
}

static u32 sceAudioSRCChRelease() {
	if (!chans[PSP_AUDIO_CHANNEL_SRC].reserved) {
		(void)0;
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	(void)0;
	chans[PSP_AUDIO_CHANNEL_SRC].reset();
	chans[PSP_AUDIO_CHANNEL_SRC].reserved = false;
	return 0;
}

static u32 sceAudioSRCOutputBlocking(u32 vol, u32 buf) {
	if (vol > 0xFFFFF) {
		(void)0;
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	}
	(void)0;
	chans[PSP_AUDIO_CHANNEL_SRC].leftVolume = vol;
	chans[PSP_AUDIO_CHANNEL_SRC].rightVolume = vol;
	chans[PSP_AUDIO_CHANNEL_SRC].sampleAddress = buf;
	return __AudioEnqueue(chans[PSP_AUDIO_CHANNEL_SRC], PSP_AUDIO_CHANNEL_SRC, true);
}

static u32 sceAudioRoutingSetMode(u32 mode) {
	(void)0;
	int previousMode = defaultRoutingMode;
	defaultRoutingMode = mode;
	return previousMode;
}

static u32 sceAudioRoutingGetMode() {
	(void)0;
	return defaultRoutingMode;
}

static u32 sceAudioRoutingSetVolumeMode(u32 mode) {
	(void)0;
	int previousMode = defaultRoutingVolMode;
	defaultRoutingVolMode = mode;
	return previousMode;
}

static u32 sceAudioRoutingGetVolumeMode() {
	(void)0;
	return defaultRoutingVolMode;
}

const HLEFunction sceAudio[] =
{
	// Newer simplified single channel audio output. Presumably for games that use Atrac3
	// directly from Sas instead of playing it on a separate audio channel.
	{0X01562BA3, &WrapU_U<sceAudioOutput2Reserve>,          "guest-service",        'x', "x"   },
	{0X2D53F36E, &WrapU_UU<sceAudioOutput2OutputBlocking>,  "guest-service", 'x', "xx"  },
	{0X63F2889C, &WrapU_U<sceAudioOutput2ChangeLength>,     "guest-service",   'x', "x"   },
	{0X647CEF33, &WrapU_V<sceAudioOutput2GetRestSample>,    "guest-service",  'x', ""    },
	{0X43196845, &WrapU_V<sceAudioOutput2Release>,          "guest-service",        'x', ""    },

	// "Traditional" audio channel interface
	{0X80F1F7E0, &WrapU_V<sceAudioInit>,                    "guest-service",                  'x', ""    },
	{0X210567F7, &WrapU_V<sceAudioEnd>,                     "guest-service",                   'x', ""    },
	{0XA2BEAA6C, &WrapU_U<sceAudioSetFrequency>,            "guest-service",          'x', "x"   },
	{0X927AC32B, &WrapU_V<sceAudioSetVolumeOffset>,         "guest-service",       'x', ""    },
	{0X8C1009B2, &WrapU_UIU<sceAudioOutput>,                "guest-service",                'x', "xix" },
	{0X136CAF51, &WrapU_UIU<sceAudioOutputBlocking>,        "guest-service",        'x', "xix" },
	{0XE2D56B2D, &WrapU_UIIU<sceAudioOutputPanned>,         "guest-service",          'x', "xiix"},
	{0X13F592BC, &WrapU_UIIU<sceAudioOutputPannedBlocking>, "guest-service",  'x', "xiix"},
	{0X5EC81C55, &WrapU_IUU<sceAudioChReserve>,             "guest-service",             'x', "ixx" },
	{0X6FC46853, &WrapU_U<sceAudioChRelease>,               "guest-service",             'x', "x"   },
	{0XE9D97901, &WrapI_U<sceAudioGetChannelRestLen>,       "guest-service",     'i', "x"   },
	{0XB011922F, &WrapI_U<sceAudioGetChannelRestLength>,    "guest-service",  'i', "x"   },
	{0XCB2E439E, &WrapU_UU<sceAudioSetChannelDataLen>,      "guest-service",     'x', "xx"  },
	{0X95FD0C2D, &WrapU_UU<sceAudioChangeChannelConfig>,    "guest-service",   'x', "xx"  },
	{0XB7E1D8E7, &WrapU_UUU<sceAudioChangeChannelVolume>,   "guest-service",   'x', "xxx" },

	// Not sure about the point of these, maybe like traditional but with ability to do sample rate conversion?
	{0X38553111, &WrapU_UUU<sceAudioSRCChReserve>,          "guest-service",          'x', "xxx" },
	{0X5C37C0AE, &WrapU_V<sceAudioSRCChRelease>,            "guest-service",          'x', ""    },
	{0XE0727056, &WrapU_UU<sceAudioSRCOutputBlocking>,      "guest-service",     'x', "xx"  },

	// Never seen these used
	{0X41EFADE7, nullptr,                                   "guest-service",         '?', ""    },
	{0XB61595C0, nullptr,                                   "guest-service",          '?', ""    },

	// Microphone interface
	{0X7DE61688, nullptr,                                   "guest-service",             '?', ""    },
	{0XE926D3FB, nullptr,                                   "guest-service",           '?', ""    },
	{0X6D4BEC68, nullptr,                                   "guest-service",                 '?', ""    },
	{0X086E5895, nullptr,                                   "guest-service",         '?', ""    },
	{0XA708C6A6, nullptr,                                   "guest-service",        '?', ""    },
	{0XA633048E, nullptr,                                   "guest-service",          '?', ""    },
	{0X87B2E651, nullptr,                                   "guest-service",          '?', ""    },

	{0X36FD8AA9, &WrapU_U<sceAudioRoutingSetMode>,          "guest-service",        'x', "x"   },
	{0X39240E7D, &WrapU_V<sceAudioRoutingGetMode>,          "guest-service",        'x', ""    },
	{0XBB548475, &WrapU_U<sceAudioRoutingSetVolumeMode>,    "guest-service",  'x', "x"   },
	{0X28235C56, &WrapU_V<sceAudioRoutingGetVolumeMode>,    "guest-service",  'x', ""    },

};

void Register_sceAudio()
{
	RegisterModule("sceAudio", ARRAY_SIZE(sceAudio), sceAudio);
}
