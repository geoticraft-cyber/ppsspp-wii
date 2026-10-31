#include "Wii/CpuProfile.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/libversion.h>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include "Wii/Input.h"
#include "Wii/Perf.h"
#include "Wii/Presentation.h"
#include "Wii/AllocationDiagnostics.h"
#include "Wii/Settings.h"
#include "Wii/VertexCacheStorage.h"
#include "Wii/Diagnostics.h"
#include "Wii/FileLogging.h"
#include "Wii/Runtime.h"
#include <ogc/ios.h>
#include "file/zip_read.h"
#include "Wii/DiscBrowser.h"
#include "Wii/MenuLayout.h"
#include "Wii/Menu.h"
#include "Wii/Storage.h"
#include "Wii/GXBridge.h"
#include <dirent.h>
#include <cstdio>
#include <string>
#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Core/Config.h"
#include "Core/HLE/scePower.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "Wii/Video.h"

static std::atomic<bool> quitRequested{false};
static std::atomic<bool> returnToMenu{false};
static std::atomic<bool> inputRunning{false};
static lwp_t inputThread;
static bool inputThreadStarted=false;
static Wii::StorageDevice sourceDevice=Wii::StorageDevice::SD;
static std::string selectedImage;
static void RequestExit() { quitRequested = true;Wii::RequestEmulationStop(true); }
static void ResetRequested() { RequestExit(); }

class WiiHost final : public Host {
public:
  bool InitGraphics(std::string *, GraphicsContext **ctx) override { *ctx = nullptr; return true; }
  void ShutdownGraphics() override {}
  void InitSound() override {}
  void ShutdownSound() override {}
  bool IsDebuggingEnabled() override { return false; }
  bool ShouldSkipUI() override { return true; }
  void SendDebugOutput(const std::string &text) override { fwrite(text.data(), 1, text.size(), stdout); }
  void NotifyUserMessage(const std::string &message, float, u32, const char *) override { printf("%s\n", message.c_str()); }
};

class WiiLog final : public LogListener {
public:
  void Log(const LogMessage &message) override {
    Wii::DiagnosticMessage(unsigned(message.level),message.header,message.msg.c_str(),Wii::RuntimePC(),Wii::RuntimePhase());
  }
};

static bool ApplyLoggingSettings(const std::string &root) {
  auto &settings=Wii::GetSettings();
  const bool opened=Wii::SetFileLogging(root,settings.logging,settings.diagnosticLevel);
  if(!opened)settings.logging=false;
  g_Config.bEnableLogging=settings.logging;
  Wii::SetDiagnosticLevel(settings.logging?settings.diagnosticLevel:0);
  for(int i=0;i<LogTypes::NUMBER_OF_LOGS;++i) {
    const auto type=static_cast<LogTypes::LOG_TYPE>(i);
    LogManager::GetInstance()->SetEnabled(type,settings.logging);
    LogManager::GetInstance()->SetLogLevel(type,settings.diagnosticLevel>=3?LogTypes::LDEBUG:LogTypes::LINFO);
  }
  return opened;
}

static void PollInput() {
  WPAD_ScanPads();
  PAD_ScanPads();
  if ((WPAD_ButtonsDown(0) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) ||
      ((PAD_ButtonsHeld(0) & (PAD_TRIGGER_Z | PAD_BUTTON_START)) == (PAD_TRIGGER_Z | PAD_BUTTON_START)))
    { returnToMenu=true;Wii::RequestEmulationStop(true); }
  const u32 gc = PAD_ButtonsHeld(0), wm = WPAD_ButtonsHeld(0);
  u32 buttons = 0;
  expansion_t expansion{};WPAD_Expansion(0,&expansion);
  // Upright remote: direct face buttons, without a SELECT modifier.
  if(wm&WPAD_BUTTON_A)buttons|=CTRL_CROSS;
  if(wm&WPAD_BUTTON_B)buttons|=CTRL_CIRCLE;
  if(wm&WPAD_BUTTON_1)buttons|=CTRL_SQUARE;
  if(wm&WPAD_BUTTON_2)buttons|=CTRL_TRIANGLE;
  if(wm&WPAD_BUTTON_MINUS)buttons|=CTRL_SELECT;
  const struct {u32 gc,wii,psp;} mappings[]={
    {PAD_BUTTON_A,0,CTRL_CROSS},{PAD_BUTTON_B,0,CTRL_CIRCLE},
    {PAD_BUTTON_X,0,CTRL_SQUARE},{PAD_BUTTON_Y,0,CTRL_TRIANGLE},
    {PAD_TRIGGER_L,0,CTRL_LTRIGGER},{PAD_TRIGGER_R,0,CTRL_RTRIGGER},
    {PAD_BUTTON_START,WPAD_BUTTON_PLUS,CTRL_START},{PAD_TRIGGER_Z,0,CTRL_SELECT},
    {PAD_BUTTON_UP,WPAD_BUTTON_UP,CTRL_UP},{PAD_BUTTON_DOWN,WPAD_BUTTON_DOWN,CTRL_DOWN},
    {PAD_BUTTON_LEFT,WPAD_BUTTON_LEFT,CTRL_LEFT},{PAD_BUTTON_RIGHT,WPAD_BUTTON_RIGHT,CTRL_RIGHT}};
  for(const auto &map:mappings)if((gc&map.gc)||(wm&map.wii))buttons|=map.psp;
  const struct { u32 wii, psp; } classic[] = {
    {WPAD_CLASSIC_BUTTON_B, CTRL_CROSS}, {WPAD_CLASSIC_BUTTON_A, CTRL_CIRCLE},
    {WPAD_CLASSIC_BUTTON_Y, CTRL_SQUARE}, {WPAD_CLASSIC_BUTTON_X, CTRL_TRIANGLE},
    {WPAD_CLASSIC_BUTTON_UP, CTRL_UP}, {WPAD_CLASSIC_BUTTON_DOWN, CTRL_DOWN},
    {WPAD_CLASSIC_BUTTON_LEFT, CTRL_LEFT}, {WPAD_CLASSIC_BUTTON_RIGHT, CTRL_RIGHT},
    {WPAD_CLASSIC_BUTTON_PLUS, CTRL_START}, {WPAD_CLASSIC_BUTTON_MINUS, CTRL_SELECT},
    {WPAD_CLASSIC_BUTTON_FULL_L|WPAD_CLASSIC_BUTTON_ZL, CTRL_LTRIGGER}, {WPAD_CLASSIC_BUTTON_FULL_R|WPAD_CLASSIC_BUTTON_ZR, CTRL_RTRIGGER},
  };
  // Nunchuk C shares a bit with Classic R; only decode the active expansion.
  if(expansion.type==WPAD_EXP_CLASSIC)
    for (const auto &map : classic) if (wm & map.wii) buttons |= map.psp;
  if (expansion.type == WPAD_EXP_NUNCHUK) {
    if (wm & WPAD_NUNCHUK_BUTTON_C) buttons |= CTRL_LTRIGGER;
    if (wm & WPAD_NUNCHUK_BUTTON_Z) buttons |= CTRL_RTRIGGER;
  }
  float x = PAD_StickX(0) / 128.0f, y = PAD_StickY(0) / 128.0f;
  const joystick_t *stick = expansion.type == WPAD_EXP_NUNCHUK ? &expansion.nunchuk.js :
    expansion.type == WPAD_EXP_CLASSIC ? &expansion.classic.ljs : nullptr;
  if (stick) {
    Wii::PolarStick(stick->mag,stick->ang,x,y);
  } else {
    Wii::RadialDeadzone(x,y);
  }
  if(!stick)Wii::AddDpadAnalog(buttons&CTRL_UP,buttons&CTRL_DOWN,buttons&CTRL_LEFT,buttons&CTRL_RIGHT,x,y);
  __CtrlSetHostState(buttons,x,y);
}

static void *InputWorker(void *) {
  auto changed=Wii::PerfTime();uint32_t beat=Wii::RuntimeBeat();
  while(inputRunning.load()) {
    PollInput();const uint32_t current=Wii::RuntimeBeat();
    if(current!=beat) {beat=current;changed=Wii::PerfTime();}
    else if(Wii::PerfTime()-changed>5000000) {
      WARN_LOG(BOOT,"WATCHDOG: no core progress for 5s; phase=%u pc=%08x (1 CPU, 2 GE, 3 raster, 4 GX, 5 I/O)",Wii::RuntimePhase(),Wii::RuntimePC());
      Wii::DumpDiagnosticTrace("watchdog-no-progress",true);
      changed=Wii::PerfTime();
    }
    usleep(8000);
  }
  return nullptr;
}
static void StartInput() {
  inputRunning=true;
  inputThreadStarted=LWP_CreateThread(&inputThread,InputWorker,nullptr,nullptr,32*1024,80)==0;
  if(!inputThreadStarted)inputRunning=false;
}
static void StopInput() {
  inputRunning=false;
  if(inputThreadStarted)LWP_JoinThread(inputThread,nullptr);
  inputThreadStarted=false;
  __CtrlSetHostState(0,0,0);
}

static void ConfigureCore(const std::string &root) {
  g_Config.iCpuCore = static_cast<int>(CPUCore::INTERPRETER);
  g_Config.iPSPModel = PSP_MODEL_FAT;
  g_Config.iInternalResolution = 1;
  g_Config.iNumWorkerThreads = 1;
  g_Config.bFastMemory = false;

  g_Config.bSeparateSASThread = false;
  g_Config.bSeparateIOThread = false;
  g_Config.bEnableSound = false;
  g_Config.bSoftwareRendering = true;
  g_Config.bCacheFullIsoInRam = false;
  g_Config.bEnableWlan = false;
  g_Config.bEnableAdhocServer = false;
  g_Config.bCheckForNewVersion = false;
  g_Config.bAutoSaveSymbolMap = false;
  g_Config.bIgnoreBadMemAccess = false;
  g_Config.bHideSlowWarnings = true;
  ApplyLoggingSettings(root);
  g_Config.sReportHost.clear();
  g_Config.memStickDirectory = root + "memstick/";
  g_Config.flash0Directory = root + "assets/flash0/";

  const Wii::Settings &settings = Wii::GetSettings();
  g_Config.iFrameSkip = 0;
  g_Config.iLockedCPUSpeed = settings.pspClockMHz;
  g_Config.bFrameSkipUnthrottle=false;
  g_Config.bAutoFrameSkip=false;
  g_Config.iRenderingMode = 1;
  g_Config.sPostShaderName = "Off";
}

static bool MenuExitPressed() {
  return (WPAD_ButtonsDown(0) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) ||
    (PAD_ButtonsDown(0) & PAD_BUTTON_START);
}

static const char *SettingOnOff(bool value) { return value ? "Enabled" : "Disabled"; }

static void DrawSettingsScreen(int selected, const std::string &status) {
  const Wii::Settings &s = Wii::GetSettings();
  char text[128];
  WiiMenuBegin();
  WiiMenuRect(0,0,640,66,0xffffff);
  WiiMenuRect(0,64,640,3,0xd44343);
  WiiMenuText(24,16,"PPSSPP Wii 1.4 settings",0x981212,2);
  const int y0=96, row=30, first=(selected/10)*10;
  std::string labels[17];
  std::snprintf(text,sizeof(text),"TV aspect ratio         %s",s.aspect?"16:9":"4:3"); labels[0]=text;
  std::snprintf(text,sizeof(text),"Video output            %s",Wii::VideoModeName(s.video)); labels[1]=text;
  std::snprintf(text,sizeof(text),"GX acceleration         %s",SettingOnOff(s.gx)); labels[2]=text;
  std::snprintf(text,sizeof(text),"Skip unsupported movies %s",SettingOnOff(s.skipMovies)); labels[3]=text;
  std::snprintf(text,sizeof(text),"Skip buffer effects     %s",s.skipBufferEffects?"On (graphics hack)":"Off"); labels[4]=text;
  std::snprintf(text,sizeof(text),"Vertex cache hack       %s",s.vertexCache?"On (checked)":"Off"); labels[5]=text;
  std::snprintf(text,sizeof(text),"Offline HLE fast path   %s",SettingOnOff(s.offlineHLE)); labels[6]=text;
  std::snprintf(text,sizeof(text),"PSP clock (experimental) %s",s.pspClockMHz?std::to_string(s.pspClockMHz).c_str():"Auto"); labels[7]=text;
  labels[8]=std::string("Low memory caches       ")+SettingOnOff(s.lowMemory);
  labels[9]=std::string("GX sprite offload       ")+SettingOnOff(s.gxSprites);
  labels[10]=std::string("GX display smoothing    ")+SettingOnOff(s.gxLinear);
  labels[11]=std::string("GX texture reuse        ")+SettingOnOff(s.gxTextureCache);
  labels[12]=std::string("CPU sprite texel reuse  ")+SettingOnOff(s.cpuSpriteCache);
  labels[13]=std::string("GX advanced workarounds ")+SettingOnOff(s.gxAdvancedSprites);
  labels[14]=std::string("Performance overlay     ")+SettingOnOff(s.performanceOverlay);
  labels[15]=std::string("File logging            ")+SettingOnOff(s.logging);
  labels[16]="Save and back";
  std::snprintf(text,sizeof(text),"Page %d / 2",selected/10+1); WiiMenuText(24,72,text,0x986969);
  for(int i=first;i<std::min(first+10,17);++i) {
    const int y=y0+(i-first)*row;
    if(i==selected)WiiMenuRect(20,y-4,600,28,0xfbd2d2);
    WiiMenuText(34,y,labels[i].c_str(),0x5c2525);
  }
  WiiMenuText(24,414,"A: change   Left/Right: adjust   B: save/back",0x855252);
  WiiMenuText(24,438,"PSP clock changes emulation; Wii CPU clock is unchanged.",0x986969);
  WiiMenuText(24,458,"Buffer-effects hack can hide graphics. Video: timed revert.",0x986969);
  if(!status.empty()) WiiMenuText(350,414,status.c_str(),0x912424);
  WiiMenuEnd();
}

static void AdjustSetting(int item, int delta) {
  Wii::Settings &s=Wii::GetSettings();
  switch(item) {
  case 0: s.aspect=!s.aspect; break;
  case 1: s.video=(s.video+delta+5)%5; if(s.video==3)s.video=delta>0?4:2; break;
  case 2: s.gx=!s.gx; break;
  case 3: s.skipMovies=!s.skipMovies; break;
  case 4: s.skipBufferEffects=!s.skipBufferEffects; break;
  case 5: s.vertexCache=!s.vertexCache; break;
  case 6:s.offlineHLE=!s.offlineHLE; break;
  case 7: {
    s.pspClockMHz=Wii::StepPSPClock(s.pspClockMHz,delta);break;
  }
  case 8:s.lowMemory=!s.lowMemory; break;
  case 9:s.gxSprites=!s.gxSprites; break;
  case 10:s.gxLinear=!s.gxLinear; break;
  case 11:s.gxTextureCache=!s.gxTextureCache; break;
  case 12:s.cpuSpriteCache=!s.cpuSpriteCache;break;
  case 13:s.gxAdvancedSprites=!s.gxAdvancedSprites;break;
  case 14:s.performanceOverlay=!s.performanceOverlay;break;
  case 15:s.logging=!s.logging;break;
  default: break;
  }
  Wii::ValidateSettings(s);
}

static bool TestVideoMode(int selected, int oldVideo, int newVideo) {
  if (oldVideo == newVideo) return true;
  if (!WiiVideoModeSupported(newVideo) || !WiiVideoApplyMode(newVideo)) {
    Wii::GetSettings().video = oldVideo;
    return false;
  }

  // RECOVERED_1_2: boot.dol contains a "Video mode test" path and the UI says
  // "Video: timed revert."  Preserve that safety behavior: A keeps the mode,
  // B/1 or timeout restores the previous mode. The exact original countdown
  // duration is stripped; ten seconds is used conservatively here.
  const double deadline = Wii::PerfTime() / 1000000.0 + 10.0;
  for (;!quitRequested;) {
    const double now = Wii::PerfTime() / 1000000.0;
    int remaining = int(deadline - now + 0.999);
    if (remaining < 0) remaining = 0;
    char status[96];
    std::snprintf(status, sizeof(status), "Video mode test: A keep / B revert (%ds)", remaining);
    DrawSettingsScreen(selected, status);
    WPAD_ScanPads(); PAD_ScanPads();
    const u32 down = WPAD_ButtonsDown(0), gc = PAD_ButtonsDown(0);
    if(MenuExitPressed() || quitRequested) {RequestExit();break;}
    if ((down & (WPAD_BUTTON_A | WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_A)) || (gc & PAD_BUTTON_A))
      return true;
    if ((down & (WPAD_BUTTON_B | WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_B)) || (gc & PAD_BUTTON_B) || now >= deadline)
      break;
    VIDEO_WaitVSync();
  }
  if(!WiiVideoApplyMode(oldVideo)) {
    // Keep the stored selection honest if restoration cannot allocate.
    // The normal downward transition reuses its existing larger XFBs.
    if(WiiVideoApplyMode(0))Wii::GetSettings().video=0;
    else Wii::GetSettings().video=newVideo;
  } else Wii::GetSettings().video=oldVideo;
  return false;
}

static void SettingsScreen(const std::string &root) {
  int selected=0;
  std::string status;
  bool dirty=true;
  while(!quitRequested) {
    WPAD_ScanPads(); PAD_ScanPads();
    const u32 down=WPAD_ButtonsDown(0), gc=PAD_ButtonsDown(0);
    if((down&(WPAD_BUTTON_HOME|WPAD_CLASSIC_BUTTON_HOME)) || (gc&PAD_BUTTON_START)) { quitRequested=true; break; }
    const bool up=(down&(WPAD_BUTTON_UP|WPAD_CLASSIC_BUTTON_UP))||(gc&PAD_BUTTON_UP);
    const bool dn=(down&(WPAD_BUTTON_DOWN|WPAD_CLASSIC_BUTTON_DOWN))||(gc&PAD_BUTTON_DOWN);
    const bool left=(down&(WPAD_BUTTON_LEFT|WPAD_CLASSIC_BUTTON_LEFT))||(gc&PAD_BUTTON_LEFT);
    const bool right=(down&(WPAD_BUTTON_RIGHT|WPAD_CLASSIC_BUTTON_RIGHT))||(gc&PAD_BUTTON_RIGHT);
    const bool activate=(down&(WPAD_BUTTON_A|WPAD_BUTTON_2|WPAD_CLASSIC_BUTTON_A))||(gc&PAD_BUTTON_A);
    bool back=(down&(WPAD_BUTTON_B|WPAD_BUTTON_1|WPAD_CLASSIC_BUTTON_B))||(gc&PAD_BUTTON_B);
    if(up) { selected=(selected+16)%17; dirty=true; }
    if(dn) { selected=(selected+1)%17; dirty=true; }
    if(left||right) {
      if(selected<16) {
        const int oldVideo=Wii::GetSettings().video;
        AdjustSetting(selected,right?1:-1);
        if(selected==1) {
          if(!TestVideoMode(selected,oldVideo,Wii::GetSettings().video)) status="Video mode reverted";
          else status="Video mode kept";
        }
      }
      dirty=true;
    }
    if(activate) {
      if(selected==16) back=true;
      else {
        const int oldVideo=Wii::GetSettings().video;
        AdjustSetting(selected,1);
        if(selected==1) {
          if(!TestVideoMode(selected,oldVideo,Wii::GetSettings().video)) status="Video mode reverted";
          else status="Video mode kept";
        }
        dirty=true;
      }
    }
    if(back) {
      const bool loggingReady=ApplyLoggingSettings(root);
      Wii::GXShutdown();
      Wii::SetGXEnabled(Wii::GetSettings().gx);
      status=Wii::SaveSettings(root+"wii.ini")?"Settings saved":"Settings could not be saved to this device.";
      if(!loggingReady)status="Settings saved; log files could not be opened.";
      DrawSettingsScreen(selected,status);
      VIDEO_WaitVSync();
      break;
    }
    if(dirty) { DrawSettingsScreen(selected,status); dirty=false; }
    else VIDEO_WaitVSync();
  }
}

static std::string ChooseDisc(const std::string &status, const std::string &root) {
  std::vector<Wii::DiscImage> games;
  Wii::ScanReport report;
  Wii::MenuView view{}; view.games=&games; view.report=&report; view.status=status;
  unsigned repeat=0;
  bool dirty=true, clickArmed=false, pointerInside=true;
  float px=320,py=240;
  float lastIRx=-1,lastIRy=-1;
  int dialogSelection=0;
  std::string directory=Wii::DiscDirectory(sourceDevice);
  view.usb=sourceDevice==Wii::StorageDevice::USB;
  auto scan = [&] {
    Wii::DrawMessage("Reading game device", "Scanning "+directory+" and its subfolders...");
    Wii::MountStorage(sourceDevice);
    view.rootReadable=Wii::StorageReadable(sourceDevice);
    view.cardInserted=Wii::StorageInserted(sourceDevice);
    view.rootSummary=Wii::DirectorySummary(Wii::DeviceRoot(sourceDevice));
    games=Wii::ScanDiscs(directory,&view.scanError,&report,true);
    view.page=0; view.selected=0; view.hover=-1;
    view.info=false; view.confirm=false;
    for(size_t i=0;i<games.size();++i)if(games[i].path==selectedImage) { view.selected=i;view.page=i/6; }
    if(!view.rootReadable) view.status="Device cannot be read. Open Info for details.";
    else if(!report.folderReadable) view.status="Game folder unavailable. Try Find or Info.";
    else if(games.empty()) view.status="Scan complete: no .iso/.cso names in this folder tree.";
    else view.status="Select an image, then Load. Rescan retries this folder.";
    INFO_LOG(BOOT,"Device detected=%d root=%d %s",view.cardInserted,view.rootReadable,view.rootSummary.c_str());
    INFO_LOG(BOOT,"SCAN %s: %s; metadata failures=%u; %s",directory.c_str(),report.Summary().c_str(),report.metadataFailures,view.scanError.c_str());
    for(const auto &name:report.examples) INFO_LOG(BOOT,"SD entry: %s",name.c_str());
    for(const auto &problem:report.problems) WARN_LOG(BOOT,"SD read: %s",problem.c_str());
    dirty=true; clickArmed=false;
  };
  scan();
  if(!status.empty()) view.status=status;
  while(!quitRequested) {
    WPAD_ScanPads(); PAD_ScanPads();
    if(MenuExitPressed()) { quitRequested=true; break; }
    const u32 down=WPAD_ButtonsDown(0), held=WPAD_ButtonsHeld(0);
    const u32 gcDown=PAD_ButtonsDown(0), gcHeld=PAD_ButtonsHeld(0);
    if(!view.info&&!view.confirm&&((down&(WPAD_BUTTON_MINUS|WPAD_CLASSIC_BUTTON_MINUS))||(gcDown&PAD_BUTTON_X))) {
      SettingsScreen(root);
      lastIRx=lastIRy=-1; // Re-map stationary IR after an aspect change.
      view.gxEnabled=Wii::GXEnabled();
      dirty=true;
    }
    const u32 wmA=WPAD_BUTTON_A|WPAD_BUTTON_2|WPAD_CLASSIC_BUTTON_A;
    const bool pressed=(down&wmA)||(gcDown&PAD_BUTTON_A);
    if(!(held&wmA) && !(gcHeld&PAD_BUTTON_A)) clickArmed=true;
    const bool click=pressed&&clickArmed;
    if(click) clickArmed=false;
    ir_t ir{}; WPAD_IR(0,&ir);
    if(ir.valid && std::isfinite(ir.x) && std::isfinite(ir.y) && (std::abs(ir.x-lastIRx)>1 || std::abs(ir.y-lastIRy)>1 || pressed)) {
      pointerInside=Wii::MenuPointer(ir.x,ir.y,Wii::GetSettings().aspect!=0,px,py);view.pointer=true;
      lastIRx=ir.x;lastIRy=ir.y;
    }
    const int sx=PAD_StickX(0),sy=PAD_StickY(0);
    if(std::abs(sx)>20 || std::abs(sy)>20) { px+=sx/18.0f;py-=sy/18.0f;view.pointer=true;pointerInside=true; }
    if(pointerInside) {px=std::max(4.0f,std::min(628.0f,px));py=std::max(4.0f,std::min(460.0f,py));}
    if(view.px!=int(px)||view.py!=int(py)) { view.px=int(px);view.py=int(py);dirty=true; }
    const bool up=(held&(WPAD_BUTTON_UP|WPAD_CLASSIC_BUTTON_UP))||(gcHeld&PAD_BUTTON_UP);
    const bool dn=(held&(WPAD_BUTTON_DOWN|WPAD_CLASSIC_BUTTON_DOWN))||(gcHeld&PAD_BUTTON_DOWN);
    const bool left=(held&(WPAD_BUTTON_LEFT|WPAD_CLASSIC_BUTTON_LEFT))||(gcHeld&PAD_BUTTON_LEFT);
    const bool right=(held&(WPAD_BUTTON_RIGHT|WPAD_CLASSIC_BUTTON_RIGHT))||(gcHeld&PAD_BUTTON_RIGHT);
    const bool moved=(down&(WPAD_BUTTON_UP|WPAD_BUTTON_DOWN|WPAD_BUTTON_LEFT|WPAD_BUTTON_RIGHT|WPAD_CLASSIC_BUTTON_UP|WPAD_CLASSIC_BUTTON_DOWN|WPAD_CLASSIC_BUTTON_LEFT|WPAD_CLASSIC_BUTTON_RIGHT)) || (gcDown&(PAD_BUTTON_UP|PAD_BUTTON_DOWN|PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT));
    if(up||dn||left||right) ++repeat;else repeat=0;
    if(moved||(repeat>24&&repeat%6==0)) {
      view.pointer=false;dirty=true;
      if(view.info||view.confirm) dialogSelection=left?0:right?1:dialogSelection;
      else if(!games.empty()) {
        int shift=up?-3:dn?3:left?-1:1;
        const int count=int(games.size());
        view.selected=size_t((int(view.selected)+shift+count*3)%count);
        view.page=view.selected/6;
      }
    }
    int hover=-1;
    if(view.info||view.confirm) {
      if(view.pointer) for(unsigned i=0;i<2;++i) if(Wii::DialogButton(i).Contains(view.px,view.py)) hover=20+int(i);
      if(!view.pointer) hover=20+dialogSelection;
    } else if(view.pointer) {
      for(unsigned i=0;i<6;++i) {
        if(view.page*6+i<games.size()&&Wii::GameRow(i).Contains(view.px,view.py)) hover=int(i);
        if(Wii::MenuButton(i).Contains(view.px,view.py)) hover=10+int(i);
      }
      if(Wii::ExitButton().Contains(view.px,view.py)) hover=19;
      if(Wii::GXButton().Contains(view.px,view.py))hover=42;
      for(unsigned i=0;i<2;++i)if(Wii::SourceButton(i).Contains(view.px,view.py))hover=40+int(i);
    }
    if(hover!=view.hover) { view.hover=hover;dirty=true; }
    const bool back=(down&(WPAD_BUTTON_B|WPAD_BUTTON_1|WPAD_CLASSIC_BUTTON_B))||(gcDown&PAD_BUTTON_B);
    bool launch=false;
    if(back) {
      if(view.info||view.confirm) { view.info=false;view.confirm=false;dirty=true; }
      else scan();
    }
    if(click) {
      dirty=true;
      if(view.info||view.confirm) {
        if(hover==21) { view.info=false;view.confirm=false; }
        else if(hover==20) {
          if(view.info) { directory=Wii::DeviceRoot(sourceDevice);scan(); }
          else launch=true;
        }
      } else if(hover==19) { quitRequested=true;break; }
      else if(hover==42) { auto &cfg=Wii::GetSettings(); cfg.gx=!cfg.gx; Wii::SetGXEnabled(cfg.gx); Wii::SaveSettings(root+"wii.ini"); dirty=true; }
      else if(hover>=0&&hover<6) { view.selected=view.page*6+hover;view.confirm=true;dialogSelection=0; }
      else if(hover==10||(!view.pointer&&hover<0)) {
        if(!games.empty()) { view.confirm=true;dialogSelection=0; }
      } else if(hover==11||hover==12) {
        const size_t pages=std::max(size_t(1),(games.size()+5)/6);
        view.page=(view.page+pages+(hover==11?-1:1))%pages;
        view.selected=view.page*6;
      } else if(hover==13) scan();
      else if(hover==14) { directory=Wii::DeviceRoot(sourceDevice);scan(); }
      else if(hover==15) { view.info=true;dialogSelection=0; }
      else if(hover==40||hover==41) { sourceDevice=hover==40?Wii::StorageDevice::SD:Wii::StorageDevice::USB;view.usb=hover==41;directory=Wii::DiscDirectory(sourceDevice);scan(); }
    }
    if(launch&&!games.empty()) {
      std::string error;
      if(Wii::ProbeDisc(games[view.selected].path,&error)) {
        selectedImage=games[view.selected].path;
        Wii::DrawMessage("Loading PSP game",games[view.selected].name+" - HOME returns to the game menu.");
        return selectedImage;
      }
      if(!error.empty()) {
        view.status=error;view.confirm=true;dirty=true;
        WARN_LOG(BOOT,"Disc preflight: %s",error.c_str());
      }
    }
    view.gxEnabled=Wii::GXEnabled();
    if(dirty) { Wii::DrawMenu(view);dirty=false; }
    else VIDEO_WaitVSync();
  }
  return "";
}

int main(int argc, char **argv) {
  // IOS58 supplies the USB2 host interface used by libogc2 mass storage.
  const int previousIOS=IOS_GetVersion();
  const int reloadResult=previousIOS==58?0:IOS_ReloadIOS(58);
  if (!WiiVideoInit()) return 1;
  WPAD_Init(); PAD_Init();
  WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
  WPAD_SetVRes(0, 640, 480);
  SYS_SetPowerCallback(RequestExit);
  SYS_SetResetCallback(ResetRequested);
  Wii::DrawMessage("PPSSPP Wii", "Mounting SD card and checking game storage...");
  bool sd=Wii::MountStorage(Wii::StorageDevice::SD);
  bool usb=false;
  if(!sd || (argc>0 && argv && argv[0] && std::string(argv[0]).find("usb:")==0))
    usb=Wii::MountStorage(Wii::StorageDevice::USB);
  while(!sd&&!usb&&!quitRequested) {
    Wii::DrawMessage("No game device", "SD and USB could not be mounted. Use a FAT32 card or USB mass-storage drive. A/2 retries both devices; HOME exits.");
    bool retry=false;
    while(!retry&&!quitRequested) {
      WPAD_ScanPads();PAD_ScanPads();
      if(MenuExitPressed())quitRequested=true;
      retry=(WPAD_ButtonsDown(0)&(WPAD_BUTTON_A|WPAD_BUTTON_2|WPAD_CLASSIC_BUTTON_A))||(PAD_ButtonsDown(0)&PAD_BUTTON_A);
      VIDEO_WaitVSync();
    }
    if(retry) { sd=Wii::MountStorage(Wii::StorageDevice::SD);usb=Wii::MountStorage(Wii::StorageDevice::USB); }
  }
  if(quitRequested) { WiiVideoShutdown();Wii::UnmountStorage();return 0; }
  sourceDevice=usb?Wii::StorageDevice::USB:Wii::StorageDevice::SD;
  const std::string root=std::string(Wii::DeviceRoot(sourceDevice))+"apps/ppsspp-wii/";
  chdir(Wii::DeviceRoot(sourceDevice));
  File::CreateFullPath(root);
  File::CreateFullPath(root + "memstick/PSP/SYSTEM/");
  // Read the opt-in before opening or rotating any diagnostic files.
  Wii::LoadSettings(root + "wii.ini");
  Wii::SetFileLogging(root,Wii::GetSettings().logging,Wii::GetSettings().diagnosticLevel);
  LogManager::Init();
  WiiLog logger;
  LogManager::GetInstance()->AddListener(&logger);
  WiiHost wiiHost;
  host = &wiiHost;
  // The previous build omitted this, causing compat.ini and language reads to fail.
  VFSRegister("", new DirectoryAssetReader((root + "assets/").c_str()));
  g_Config.Load((root + "ppsspp.ini").c_str());
  // Settings are only available after storage is mounted; switch from the
  // bootstrap Auto mode to the persisted selection now.
  if (!WiiVideoApplyMode(Wii::GetSettings().video)) {
    Wii::GetSettings().video = 0;
    WiiVideoApplyMode(0);
  }
  Wii::SetGXEnabled(Wii::GetSettings().gx);
  ConfigureCore(root);
  INFO_LOG(BOOT,"USB host: IOS %d -> %d, reload result %d; libogc2 negotiates device speed",previousIOS,IOS_GetVersion(),reloadResult);
  INFO_LOG(BOOT, "PPSSPP Wii 1.4-rc11-r2-perf1, %s, host byte order: big endian", _V_STRING);
  const Wii::Settings &bootSettings=Wii::GetSettings();
  INFO_LOG(BOOT,"Options: GX=%d skip-buffer-effects=%d vertex-cache=%d offline-HLE=%d; real crypto retained",
    bootSettings.gx,bootSettings.skipBufferEffects,bootSettings.vertexCache,bootSettings.offlineHLE);
  std::string status;
  while (!quitRequested) {
    const std::string path = ChooseDisc(status, root);
    if (path.empty()) break;
    status.clear();
    if (!Wii::ProbeDisc(path, &status)) continue;
    Wii::DrawMessage("Loading PSP game",path);
    Wii::DiagnosticText("session-begin",path.c_str());
    INFO_LOG(BOOT, "LAUNCH: %s", path.c_str());
    Wii::FlushFileLogging();
    ConfigureCore(root);
    CoreParameter parameters{};
    parameters.cpuCore = CPUCore::INTERPRETER;
    parameters.gpuCore = GPUCORE_SOFTWARE;
    parameters.graphicsContext = nullptr;
    parameters.enableSound = false;
    parameters.fileToStart = path;
    parameters.startPaused = false;
    parameters.printfEmuLog = false;
    parameters.headLess = true;
    parameters.renderWidth = parameters.pixelWidth = 480;
    parameters.renderHeight = parameters.pixelHeight = 272;
    parameters.updateRecent = false;
    // Presentation waits for video retrace; avoid a second desktop throttle.
    parameters.unthrottle = true;
    returnToMenu=false;
    Wii::RequestEmulationStop(false);
    INFO_LOG(BOOT,"CPU selected: interpreter");
    INFO_LOG(BOOT,"Run options: GX=%u skip-buffer-effects=%u vertex-cache=%u skip-movies=%u low-memory=%u",
      unsigned(Wii::GetSettings().gx),unsigned(Wii::GetSettings().skipBufferEffects),
      unsigned(Wii::GetSettings().vertexCache),unsigned(Wii::GetSettings().skipMovies),
      unsigned(Wii::GetSettings().lowMemory));
    INFO_LOG(BOOT,"Diagnostics level=%d (0 off, 1 events, 2 sampled instructions, 3 full interpreter trace); full trace disables native execution",Wii::GetSettings().diagnosticLevel);
    const uint64_t bootStarted=Wii::PerfTime();
    Wii::CpuProfileConfigure(Wii::GetSettings().logging&&Wii::GetSettings().cpuProfile,Wii::GetSettings().cpuProfileStride);
    INFO_LOG(BOOT,"before PSP_Init");
    const bool initOK=PSP_Init(parameters, &status);
    ApplyLoggingSettings(root);
    INFO_LOG(BOOT,"after PSP_Init");
    Wii::CpuProfileDump("initialization");
    if (initOK) {
      const Wii::Settings &runSettings=Wii::GetSettings();
      INFO_LOG(BOOT,"BOOT ready in %.3f seconds; aspect=%s video=%s",
        (Wii::PerfTime()-bootStarted)/1000000.0,runSettings.aspect?"16:9":"4:3",Wii::VideoModeName(runSettings.video));
      // Game-specific INIs must not restore desktop unthrottle frame dropping.
      g_Config.bFrameSkipUnthrottle=false;g_Config.bAutoFrameSkip=false;
      g_Config.iFrameSkip=0;
      __PowerApplyWiiClock(runSettings.pspClockMHz);
      INFO_LOG(BOOT,"PSP clock requested=%d effective=%d MHz (0 means game controlled)",runSettings.pspClockMHz,CoreTiming::GetClockFrequencyMHz());
      UpdateUIState(UISTATE_INGAME);
      coreState = CORE_RUNNING;
      Wii::RequestEmulationStop(false);Wii::RuntimeProgress(0,1);
      Wii::DiagnosticCPU(currentMIPS->pc,currentMIPS->r,currentMIPS->hi,currentMIPS->lo,currentMIPS->fcr31,currentMIPS->downcount,currentMIPS->inDelaySlot,currentMIPS->nextPC);
      StartInput();
      Wii::PerfReset();
      bool firstDisplayed=false;
      while (!quitRequested && !returnToMenu && (coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME)) {
        if(!inputThreadStarted)PollInput();
        if(quitRequested||returnToMenu)break;
        PSP_BeginHostFrame();
        coreState = CORE_RUNNING;
        const auto runStarted=Wii::PerfTime();
        Wii::DiagnosticPhase("guest-run");
        PSP_RunLoopFor(usToCycles(1000));
        Wii::DiagnosticPhase("host-after-guest");
        Wii::PerfRun(runStarted);
        if (coreState == CORE_NEXTFRAME) {
          Wii::DiagnosticPhase("present");
          Wii::TraceEvent(Wii::TraceKind::Frame,Wii::RuntimePC());
          const auto presentStarted=Wii::PerfTime();const bool displayed=WiiVideoPresent();Wii::PerfPresent(presentStarted,displayed);
          if(displayed&&!firstDisplayed) {
            INFO_LOG(BOOT,"BOOT first displayed frame after %.3f seconds",(Wii::PerfTime()-bootStarted)/1000000.0);
            firstDisplayed=true;
          }
        }
        if(Wii::PerfReport())Wii::FlushFileLogging();
        PSP_EndHostFrame();
      }
      StopInput();
      Wii::DiagnosticCPU(currentMIPS->pc,currentMIPS->r,currentMIPS->hi,currentMIPS->lo,currentMIPS->fcr31,currentMIPS->downcount,currentMIPS->inDelaySlot,currentMIPS->nextPC);
      status = quitRequested ? "Returning to homebrew channel" : returnToMenu ? "Returned to game menu" : "Emulation stopped; see ppsspp.log";
    }
    Wii::DumpDiagnosticTrace("session-end");
    Wii::DiagnosticText("session-end",status.c_str());
    INFO_LOG(BOOT, "END: %s", status.c_str());
    INFO_LOG(BOOT,"before shutdown");
    Wii::DiagnosticPhase("game-shutdown");
    Wii::CpuProfileDump("session-end");
    Wii::CpuProfileConfigure(false);
    PSP_Shutdown();
    INFO_LOG(BOOT,"after shutdown");
    ResetUIState();
  }
  VFSShutdown();
  LogManager::GetInstance()->RemoveListener(&logger);
  LogManager::Shutdown();
  host = nullptr;
  WiiVideoShutdown();
  Wii::CloseFileLogging();
  Wii::UnmountStorage();
  return 0;
}
