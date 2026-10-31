// Copyright (c) 2012- PPSSPP Project. GPL-2.0-or-later.
// Export metadata retained from the original PPSSPP modules.
// Compatibility entry points only: no mixer, codec, sample storage or output.
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MIPS/MIPS.h"
#include <algorithm>
static void ContainerOne(){currentMIPS->r[2]=1;currentMIPS->r[3]=0;}
static void ContainerMemory(){const auto *r=currentMIPS->r;currentMIPS->r[2]=std::max(r[5]*r[6],r[7]<<1)+(r[5]<<6)+r[8]+256;currentMIPS->r[3]=0;}
static unsigned grain=256;
static void ServiceOK(){currentMIPS->r[2]=0;currentMIPS->r[3]=0;}
static void ServiceUnavailable(){currentMIPS->r[2]=SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;currentMIPS->r[3]=0;}
static void ServiceEnded(){currentMIPS->r[2]=0xffffffffu;currentMIPS->r[3]=0;}
static void ServiceGrain(){currentMIPS->r[2]=grain;currentMIPS->r[3]=0;}
static void ServiceSetGrain(){unsigned n=currentMIPS->r[5];if(n>=64&&n<=2048&&!(n&31))grain=n;ServiceOK();}
static void ServiceTick(){currentMIPS->r[2]=hleDelayResult(u32(0),"guest service",500);currentMIPS->r[3]=0;}
void __SasInit(){grain=256;} void __SasShutdown(){}
void __SasDoState(PointerWrap &p){auto s=p.Section("SilentSynthesis",1);if(s)p.Do(grain);}
void __SasGetDebugStats(char *out,size_t size){if(out&&size)*out=0;}
void __AtracInit(){} void __AtracShutdown(){} void __AtracDoState(PointerWrap &){}
void __Mp3Shutdown(){} void __Mp3DoState(PointerWrap &){}
void __AACShutdown(){} void __AACDoState(PointerWrap &){}
void __AudioCodecInit(){} void __AudioCodecShutdown(){} void __sceAudiocodecDoState(PointerWrap &){}
const HLEFunction sceSasCore[] = {
  {0X42778A9F, &ServiceSetGrain, "guest-service", 'x', ""},
  {0XA3589D81, &ServiceTick, "guest-service", 'x', ""},
  {0X50A14DFC, &ServiceTick, "guest-service", 'x', ""},
  {0X68A46B95, &ServiceEnded, "guest-service", 'x', ""},
  {0X440CA7D8, &ServiceOK, "guest-service", 'x', ""},
  {0XAD84D37F, &ServiceOK, "guest-service", 'x', ""},
  {0X99944089, &ServiceOK, "guest-service", 'x', ""},
  {0XB7660A23, &ServiceOK, "guest-service", 'x', ""},
  {0X019B25EB, &ServiceOK, "guest-service", 'x', ""},
  {0X9EC3676A, &ServiceOK, "guest-service", 'x', ""},
  {0X5F9529F6, &ServiceOK, "guest-service", 'x', ""},
  {0X74AE582A, &ServiceOK, "guest-service", 'x', ""},
  {0XCBCD4F79, &ServiceOK, "guest-service", 'x', ""},
  {0XA0CF2FA4, &ServiceOK, "guest-service", 'x', ""},
  {0X76F01ACA, &ServiceOK, "guest-service", 'x', ""},
  {0XF983B186, &ServiceOK, "guest-service", 'x', ""},
  {0XD5A229C9, &ServiceOK, "guest-service", 'x', ""},
  {0X33D4AB37, &ServiceOK, "guest-service", 'x', ""},
  {0X267A6DD2, &ServiceOK, "guest-service", 'x', ""},
  {0X2C8E6AB3, &ServiceOK, "guest-service", 'x', ""},
  {0X787D04D5, &ServiceOK, "guest-service", 'x', ""},
  {0XA232CBE6, &ServiceOK, "guest-service", 'x', ""},
  {0XD5EBBBCD, &ServiceOK, "guest-service", 'x', ""},
  {0XBD11B7C2, &ServiceGrain, "guest-service", 'x', ""},
  {0XD1E0A01E, &ServiceSetGrain, "guest-service", 'x', ""},
  {0XE175EF66, &ServiceOK, "guest-service", 'x', ""},
  {0XE855BF76, &ServiceOK, "guest-service", 'x', ""},
  {0X07F58C24, &ServiceOK, "guest-service", 'x', ""},
  {0XE1CD9561, &ServiceOK, "guest-service", 'x', ""},
  {0X4AA9EAD6, &ServiceOK, "guest-service", 'x', ""},
  {0X7497EA85, &ServiceOK, "guest-service", 'x', ""},
  {0XF6107F00, &ServiceOK, "guest-service", 'x', ""},
};
void Register_sceSasCore()
{
	RegisterModule("sceSasCore", ARRAY_SIZE(sceSasCore), sceSasCore);
}
const HLEFunction sceAtrac3plus[] = {
  {0X7DB31251, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X6A8C3CD5, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD5C28CC0, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X780F88D1, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XCA3CA3D2, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XA554A158, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X31668BAA, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XFAA4F89B, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XE88F759B, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD6A5F2F7, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XE23E3A35, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X36FAABFB, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X9AE849A7, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X83E85EA0, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XA2BBA8BE, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5D268707, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X61EB33F5, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X644E5607, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3F6E26B5, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X83BF7AFD, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X0E2A73AB, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X7A20E7AF, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD1F59FDB, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X868120B5, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X132F1ECA, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XECA32A99, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X0FAE370E, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X2DD3E298, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5CF9D852, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XF6837A1A, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X472E3825, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X9CD7DE03, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XB3B5D042, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5622B7C1, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5DD66588, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X231FC6B7, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X1575D64B, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X0C116E1B, &ServiceUnavailable, "guest-service", 'x', ""},
};
void Register_sceAtrac3plus() {
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
const HLEFunction sceMp3[] = {
  {0X07EC321A, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X0DB149F4, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X2A368661, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X354D27EA, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X35750070, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3C2FA058, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3CEF484F, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X44E07129, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X732B042A, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X7F696782, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X87677E40, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X87C263D1, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X8AB81558, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X8F450998, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XA703FE0F, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD021C0FB, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD0A56296, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD8F54A51, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XF5478233, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XAE6D2027, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3548AEC8, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X0840E808, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X1B839B83, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XE3EE2C81, &ServiceUnavailable, "guest-service", 'x', ""},
};
void Register_sceMp3() {
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
const HLEFunction sceAudiocodec[] = {
  {0X70A703F8, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5B37EB1D, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X8ACA11D5, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3A20A200, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X29681260, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X9D3F790C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X59176A0F, &ServiceUnavailable, "guest-service", 'x', ""},
};
void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}
const HLEFunction sceMp4[] = {
  {0X68651CBC, &ServiceOK, "guest-service", 'x', ""},
  {0X9042B257, &ServiceOK, "guest-service", 'x', ""},
  {0XB1221EE7, &ServiceOK, "guest-service", 'x', ""},
  {0X538C2057, &ServiceOK, "guest-service", 'x', ""},
  {0X113E9E7B, &ServiceOK, "guest-service", 'x', ""},
  {0X7443AF1D, &ServiceOK, "guest-service", 'x', ""},
  {0X5EB65F26, &ContainerOne, "guest-service", 'x', ""},
  {0X7ADFD01C, &ServiceOK, "guest-service", 'x', ""},
  {0XBCA9389C, &ContainerMemory, "guest-service", 'x', ""},
  {0X9C8F4FC1, &ServiceOK, "guest-service", 'x', ""},
  {0X0F0187D2, &ServiceOK, "guest-service", 'x', ""},
  {0X9CE6F5CF, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X4ED4AB1E, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X10EE0D2C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X496E8A65, &ServiceOK, "guest-service", 'x', ""},
  {0XB4B400D1, &ServiceOK, "guest-service", 'x', ""},
  {0XF7C51EC1, &ServiceOK, "guest-service", 'x', ""},
  {0X74A1CA3E, &ServiceOK, "guest-service", 'x', ""},
  {0XD8250B75, nullptr, "guest-service", 'x', ""},
  {0X8754ECB8, &ServiceOK, "guest-service", 'x', ""},
  {0X31BCD7E0, nullptr, "guest-service", 'x', ""},
  {0X5601A6F0, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X7663CB5C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X503A3CBA, &ServiceOK, "guest-service", 'x', ""},
  {0X01C76489, nullptr, "guest-service", 'x', ""},
  {0X6710FE77, nullptr, "guest-service", 'x', ""},
  {0X5D72B333, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X7D332394, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X131BDE57, &ServiceOK, "guest-service", 'x', ""},
  {0X17EAA97D, nullptr, "guest-service", 'x', ""},
  {0X28CCB940, nullptr, "guest-service", 'x', ""},
  {0X3069C2B5, nullptr, "guest-service", 'x', ""},
  {0XD2AC9A7E, nullptr, "guest-service", 'x', ""},
  {0X4FB5B756, nullptr, "guest-service", 'x', ""},
  {0X427BEF7F, nullptr, "guest-service", 'x', ""},
  {0X532029B8, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XA6C724DC, nullptr, "guest-service", 'x', ""},
  {0X3C2183C7, nullptr, "guest-service", 'x', ""},
  {0X9CA13D1A, nullptr, "guest-service", 'x', ""},
};
const HLEFunction sceAac[] = {
  {0XE0C89ACA, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X33B8C009, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5CFFC57C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X23D35CAE, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X7E4CFEE4, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X523347D9, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XBBDD6403, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD7C51541, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XAC6DCBE3, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X02098C69, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X6DC7758A, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X506BF66C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XD2DA2BBA, &ServiceUnavailable, "guest-service", 'x', ""},
};
void Register_sceMp4()
{
	RegisterModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
	RegisterModule("sceAac", ARRAY_SIZE(sceAac), sceAac);
}
const HLEFunction sceG729[] = {
  {0X13F1028A, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X17C11696, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X3489D1F3, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X55E14F75, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X5A409D1B, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X74804D93, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X890B86AE, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X8C87A2CA, &ServiceUnavailable, "guest-service", 'x', ""},
  {0X94714D50, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XAA1E5462, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XCFCD367C, &ServiceUnavailable, "guest-service", 'x', ""},
  {0XDB7259D5, &ServiceUnavailable, "guest-service", 'x', ""},
};
void Register_sceG729()
{
	RegisterModule("sceG729", ARRAY_SIZE(sceG729), sceG729);
}

const HLEFunction sceP3da[] = {
  {0X374500A5, &ServiceOK, "guest-service", 'x', ""},
  {0X43F756A2, &ServiceOK, "guest-service", 'x', ""},
  {0X013016F3, &ServiceOK, "guest-service", 'x', ""},
};
void Register_sceP3da(){RegisterModule("sceP3da",ARRAY_SIZE(sceP3da),sceP3da);}
