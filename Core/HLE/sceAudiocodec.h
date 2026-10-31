// Copyright (c) 2012- PPSSPP Project. GPL-2.0-or-later.
#pragma once
class PointerWrap;
void Register_sceAudiocodec();
void __AudioCodecInit();void __AudioCodecShutdown();void __sceAudiocodecDoState(PointerWrap &);
