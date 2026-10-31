// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/CpuProfile.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace Wii {uint64_t cpuProfileFakeClock=0;}
static unsigned lines;
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char*,int,const char *fmt,...) {++lines;va_list a;va_start(a,fmt);vprintf(fmt,a);puts("");va_end(a);}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE) {return true;}
int main() {
 using namespace Wii;
 CpuProfileConfigure(false,1);
 {CpuScope a(CpuPhase::HLE);cpuProfileFakeClock=100;CpuProfileCount(0);}
 assert(!CpuProfileData().buckets[unsigned(CpuPhase::HLE)].calls&&!CpuProfileData().counters[0]);
 CpuProfileDump("off");assert(!lines);
 CpuProfileConfigure(true,1);cpuProfileFakeClock=100;
 {CpuScope a(CpuPhase::HLE,false,12);cpuProfileFakeClock=110;
  {CpuScope b(CpuPhase::IO);cpuProfileFakeClock=130;}
  cpuProfileFakeClock=150;}
 assert(CpuProfileData().buckets[unsigned(CpuPhase::HLE)].ticks==30);
 assert(CpuProfileData().buckets[unsigned(CpuPhase::HLE)].inclusive==50);
 assert(CpuProfileData().buckets[unsigned(CpuPhase::IO)].ticks==20);
 assert(!CpuProfileData().top);
 CpuProfileDump("nested");assert(lines);
 // Early End is idempotent; sampled interpreter scopes are weighted once.
 CpuProfileConfigure(true,8);
 for(unsigned i=0;i<80;++i) {
  CpuScope scope(CpuPhase::Interpreter,true,0x08000000);
  cpuProfileFakeClock+=15;scope.End();scope.End();
 }
 auto &s=CpuProfileData();
 assert(s.buckets[unsigned(CpuPhase::Interpreter)].calls==80);
 assert(s.buckets[unsigned(CpuPhase::Interpreter)].samples==10);
 assert(s.buckets[unsigned(CpuPhase::Interpreter)].ticks==1200);
 // Site collisions are bounded and do not alter category totals. A hot site
 // survives cold keys mapping to its set; a live scope prevents reset.
 CpuProfileConfigure(true,1);
 for(unsigned n=0;n<100;++n) {
  {CpuScope hot(CpuPhase::HLE,false,12);cpuProfileFakeClock+=10;CpuProfileConfigure(false);}
  {CpuScope cold(CpuPhase::HLE,false,12+((n+1)<<18));++cpuProfileFakeClock;}
 }
 assert(CpuProfileData().enabled&&!CpuProfileData().top);
 assert(CpuProfileData().buckets[unsigned(CpuPhase::HLE)].ticks==1100);
 bool hotFound=false;
 for(auto &site:CpuProfileData().sites)if(site.key==12&&site.calls==100)hotFound=true;
 assert(hotFound&&CpuProfileData().collisions>0);
 CpuProfileConfigure(true,999);assert(CpuProfileData().stride==64);
 CpuProfileConfigure(false);assert(!CpuProfileData().top);
 printf("pass: disabled, nested exclusive/inclusive, early exits, sampling, reset; state=%zu scope=%zu\n",sizeof(CpuProfileState),sizeof(CpuScope));
}
