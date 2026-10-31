// Copyright (c) 2012- PPSSPP Project. GPL-2.0-or-later.
#pragma once
#include "sceAudio.h"
void __AudioInit();void __AudioShutdown();void __AudioDoState(PointerWrap &);
void __AudioUpdate(bool reset=false);void __AudioSetOutputFrequency(int);
u32 __AudioEnqueue(AudioChannel &,int,bool);
void __AudioWakeThreads(AudioChannel &,int,int);void __AudioWakeThreads(AudioChannel &,int);
