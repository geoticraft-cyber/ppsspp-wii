#include "Wii/Settings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
  auto &s=Wii::GetSettings();assert(!s.cpuProfile&&s.cpuProfileStride==64);s.cpuProfile=true;s.cpuProfileStride=8;assert(!s.logging);s.logging=true;s.performanceOverlay=true;s.video=4;s.pspClockMHz=135;s.lowMemory=true;
  s.gxSprites=false;s.gxLinear=true;s.gxTextureCache=false;s.cpuSpriteCache=false;s.diagnosticLevel=3;
  const char *path="settings-1.4-test.ini";
  assert(Wii::SaveSettings(path));s=Wii::Settings();assert(Wii::LoadSettings(path));
  assert(s.cpuProfile&&s.cpuProfileStride==8);
  assert(s.logging&&s.performanceOverlay&&s.video==4&&s.pspClockMHz==135&&s.lowMemory&&!s.gxSprites&&s.gxLinear&&!s.gxTextureCache&&!s.cpuSpriteCache&&s.diagnosticLevel==3);
  // Legacy JIT keys cannot reactivate a removed backend and are not saved.
  FILE *legacy=fopen(path,"w");assert(legacy);fputs("experimental_audio=1\nspeed=2\nnative_jit=1\njit=1\njit_fpu=1\ncached_interpreter=1\ncolor16=1\nusb_vertex_swap=1\nvideo=3\nframeskip=14\nadaptive_frameskip=1\n",legacy);fclose(legacy);
  s=Wii::Settings();assert(Wii::LoadSettings(path));assert(!s.logging&&s.video==0);
  assert(Wii::SaveSettings(path));legacy=fopen(path,"r");assert(legacy);
  char content[2048]{};assert(fread(content,1,sizeof(content)-1,legacy)>0);fclose(legacy);
  assert(!strstr(content,"experimental_audio=")&&!strstr(content,"native_jit=")&&!strstr(content,"\njit=")&&!strstr(content,"jit_fpu=")&&!strstr(content,"color16=")&&!strstr(content,"usb_vertex_swap=")&&!strstr(content,"frameskip=")&&!strstr(content,"speed="));
  s.cpuProfileStride=999;s.video=5;s.pspClockMHz=999;s.diagnosticLevel=7;Wii::ValidateSettings(s);
  assert(s.cpuProfileStride==64);
  assert(s.video==0&&s.pspClockMHz==0&&s.diagnosticLevel==1);
  for(int mhz:{0,135,168,201,234,267,300,333}) {s.pspClockMHz=mhz;Wii::ValidateSettings(s);assert(s.pspClockMHz==mhz);}
  int clock=333;
  for(int expected:{300,267,234,201,168,135,0,333}) {clock=Wii::StepPSPClock(clock,1);assert(clock==expected);}
  assert(Wii::StepPSPClock(135,-1)==168);
  std::remove(path);std::remove("settings-1.4-test.ini.bak");
  puts("PASS: 1.3 settings persistence, experimental options and invalid-value clamps");
}
