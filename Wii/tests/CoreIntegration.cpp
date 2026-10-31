#include "Wii/CpuProfile.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Wii/Runtime.h"
#include "Wii/Settings.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "file/zip_read.h"
#include "GPU/GPUState.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceCtrl.h"
#include <cassert>
#include "Wii/tests/ScalarChecks.h"
#include "Wii/tests/BufferChecks.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/TransformUnit.h"
#include <cstdlib>
#include "Core/FileLoaders/LocalFileLoader.h"
#include <vector>
#include <algorithm>

#include "Wii/tests/IoChecks.h"
#include "Wii/tests/IoIsoChecks.h"
#include "Wii/tests/CompatChecks.h"
#include "Wii/tests/InterpreterDispatchChecks.h"
#include "Wii/tests/ImportIndexChecks.h"
#include "Wii/tests/CoprocessorCacheChecks.h"
#include "Wii/tests/InterpreterBoundaryChecks.h"
#include "Wii/tests/CPUExecutionChecks.h"
#include "Wii/tests/BranchConditionChecks.h"
#include "Wii/tests/GETransferChecks.h"
#include "Wii/tests/GuestServiceChecks.h"

class TestHost final : public Host {
public:
  bool InitGraphics(std::string *, GraphicsContext **p) override { *p = nullptr; return true; }
  void ShutdownGraphics() override {}
  void InitSound() override {}
  void ShutdownSound() override {}
  bool ShouldSkipUI() override { return true; }
  bool IsDebuggingEnabled() override { return false; }
};
class TestLog final : public LogListener {
public:
  void Log(const LogMessage &msg) override { fprintf(stderr, "%s\n", msg.msg.c_str()); }
};
class ScratchProbe final : public SoftwareDrawEngine {
public:
  void Check() {
    assert(!decoded&&!decIndex&&!splineBuffer&&!quadIndices_&&!transformed&&!transformedExpanded);
    assert(!transformUnit.buf&&transformUnit.bufBytes==0);
    assert(EnsureScratchBuffers(false));assert(decoded&&!splineBuffer&&!quadIndices_&&!decIndex);
    assert(EnsureScratchBuffers(true));assert(decoded&&splineBuffer&&quadIndices_&&!decIndex);
  }
};
int main(int argc, char **argv) {
  if (argc < 3) return 2;
  const bool ge=argc>3;
  const bool sinkGuest=getenv("WII_TEST_SINK_GUEST")!=nullptr;
  const uint32_t frame=getenv("WII_WRAP_FRAME")?0x041ffffc:0x04000000;
  if(getenv("WII_SPRITE_REFERENCE"))Rasterizer::wiiSpriteFastPath=false;
  TestHost testHost; host = &testHost;
  LogManager::Init();
  TestLog logger;
  LogManager::GetInstance()->AddListener(&logger);
  for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i) {
    LogManager::GetInstance()->SetEnabled(static_cast<LogTypes::LOG_TYPE>(i), true);
    LogManager::GetInstance()->SetLogLevel(static_cast<LogTypes::LOG_TYPE>(i), LogTypes::LINFO);
  }
  // Exercise actual LocalFileLoader cache windows, tail reads and bypass flags.
  {
    std::vector<uint8_t> source(150001);for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*37+(i>>8));
    FILE *file=fopen("cache-boundary-test.iso","wb");assert(file);
    assert(fwrite(source.data(),1,source.size(),file)==source.size());fclose(file);
    LocalFileLoader loader("cache-boundary-test.iso");
    const size_t positions[]={0,1,2047,2048,65530,65536,131070,149997,150001};
    for(unsigned pass=0;pass<3;++pass)for(size_t pos:positions) {
      uint8_t out[4096];memset(out,0xcd,sizeof(out));
      const size_t expected=std::min(sizeof(out),source.size()-pos);
      auto flags=pass==2?FileLoader::Flags::HINT_UNCACHED:FileLoader::Flags::NONE;
      assert(loader.ReadAt(pos,1,sizeof(out),out,flags)==expected);
      assert(!memcmp(out,source.data()+pos,expected));
      for(size_t j=expected;j<sizeof(out);++j)assert(out[j]==0xcd);
    }
    printf("PASS: disc read cache windows, short tail, EOF and uncached reads\n");
    remove("cache-boundary-test.iso");
  }
  const std::string assetPath=std::string(argv[2])+"/";
  VFSRegister("", new DirectoryAssetReader(assetPath.c_str()));
  g_Config.Load("/nonexistent-ppsspp-test.ini");
  for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i) {
    LogManager::GetInstance()->SetEnabled(static_cast<LogTypes::LOG_TYPE>(i), true);
    LogManager::GetInstance()->SetLogLevel(static_cast<LogTypes::LOG_TYPE>(i), LogTypes::LDEBUG);
  }
  const char *clockOverride=getenv("WII_TEST_PSP_CLOCK");
  if(clockOverride)g_Config.iLockedCPUSpeed=std::atoi(clockOverride);
  g_Config.iPSPModel = PSP_MODEL_FAT;
  g_Config.iCpuCore = static_cast<int>(CPUCore::INTERPRETER);
  g_Config.iNumWorkerThreads = 1;
  g_Config.bSeparateIOThread = g_Config.bSeparateSASThread = false;
  g_Config.bSoftwareRendering = true;
  g_Config.iFrameSkip=0;
  g_Config.bFrameSkipUnthrottle=false;
  g_Config.bAutoFrameSkip=false;
  g_Config.bEnableSound = false;
  g_Config.bEnableWlan = g_Config.bEnableAdhocServer = false;
  g_Config.bCheckForNewVersion = false;
  g_Config.bIgnoreBadMemAccess = false;
  g_Config.bAutoSaveSymbolMap = false;
  g_Config.bHideSlowWarnings = true;
  g_Config.sReportHost.clear();
  g_Config.memStickDirectory = "./test-memstick/";
  g_Config.flash0Directory = std::string(argv[2]) + "/flash0/";
  File::CreateFullPath("./test-memstick/PSP/SYSTEM/");
  CoreParameter p{};
  p.cpuCore = CPUCore::INTERPRETER; p.gpuCore = GPUCORE_SOFTWARE;
  p.graphicsContext = nullptr; p.enableSound = false;
  p.fileToStart = argv[1]; p.startPaused = false;
  p.headLess = true; p.printfEmuLog = true;
  p.renderWidth = p.pixelWidth = 480; p.renderHeight = p.pixelHeight = 272;
  p.updateRecent = false; p.unthrottle = true;
  std::string error;
  if(getenv("WII_TEST_COMPAT"))g_Config.iRenderingMode=0; // exercise required-mode override at boot
  Wii::CpuProfileConfigure(getenv("WII_TEST_PROFILE")!=nullptr,1);
  if (!PSP_Init(p, &error)) { fprintf(stderr, "FAIL boot: %s\n", error.c_str()); return 3; }
  if(getenv("WII_TEST_SILENT")){GuestServiceChecks();PSP_Shutdown();LogManager::Shutdown();return 0;}
  if(getenv("WII_TEST_DISPATCH")) {
    InterpreterDispatchChecks();BranchConditionChecks();CoprocessorCacheChecks();InterpreterBoundaryChecks();CPUExecutionChecks();ImportIndexChecks();GETransferChecks();PSP_Shutdown();LogManager::Shutdown();return 0;
  }
  if(getenv("WII_TEST_COMPAT")) {
    CompatChecks(); Wii::CpuProfileDump("host-fixture"); PSP_Shutdown(); LogManager::Shutdown(); return 0;
  }
  if(getenv("WII_TEST_IO")) {
    IoIsoChecks(); IoChecks(); Wii::CpuProfileDump("host-fixture"); PSP_Shutdown(); LogManager::Shutdown(); return 0;
  }
  if(clockOverride) {
    assert(CoreTiming::GetClockFrequencyMHz()==std::atoi(clockOverride));
    printf("PASS: requested PSP clock is active at %d MHz\n",CoreTiming::GetClockFrequencyMHz());
  }
  {ScratchProbe probe;probe.Check();}
  printf("PASS: software GPU skips %u bytes of hardware transforms; patch scratch allocates only on demand\n",unsigned(4*VERTEX_BUFFER_MAX*sizeof(TransformedVertex)));
  BufferChecks();
  ScalarChecks();
  fprintf(stderr,"INIT OK\n");
  UpdateUIState(UISTATE_INGAME);
  coreState = CORE_RUNNING;
  int slices = 0;
  while ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && slices++ < 1000) {
    coreState = CORE_RUNNING;
    if (slices == 1) fprintf(stderr,"RUN START\n");
    PSP_BeginHostFrame();
    PSP_RunLoopFor(10000);
    PSP_EndHostFrame();
    if ((ge||sinkGuest) ? Memory::ReadUnchecked_U32(0x089ff000)==0xfeed600d : Memory::ReadUnchecked_U32(0x04087ffc) == 0xff0000ff) break;
  }
  if(ge)fprintf(stderr,"STUB %08x %08x %08x VTYPE %08x STRIDE %d V0=%08x\n",Memory::ReadUnchecked_U32(0x08804384),Memory::ReadUnchecked_U32(0x0880438c),Memory::ReadUnchecked_U32(0x08804394),gstate.vertType,gstate.FrameBufStride(),mipsr4k.r[2]);
  if(ge)fprintf(stderr,"SKIP %08x Vertex %08x\n",gstate_c.skipDrawReason,gstate_c.vertexAddr);
  const bool passed = sinkGuest ? Memory::ReadUnchecked_U32(0x089ff000)==0xfeed600d : ge ?
    Memory::ReadUnchecked_U32(0x089ff000)==0xfeed600d &&
    Memory::ReadUnchecked_U32(frame)==0xffffffff &&
    Memory::ReadUnchecked_U32(frame+(271*512+479)*4)==0xff403020 &&
    Memory::ReadUnchecked_U32(frame+(160*512+240)*4)!=0xff403020 :
    Memory::ReadUnchecked_U32(frame)==0xff0000ff && Memory::ReadUnchecked_U32(0x04087ffc)==0xff0000ff;
  printf("%s: %s -> ELF -> interpreter -> %s (%d slices, state %d)\n",passed?"PASS":"FAIL",argv[1],sinkGuest?"guest wait/drain/release":ge?"GE clear + triangle + clipped edge":"red framebuffer",slices,int(coreState));
  if(ge)fprintf(stderr,"GE marker=%08x corner=%08x edge=%08x center=%08x\n",Memory::ReadUnchecked_U32(0x089ff000),Memory::ReadUnchecked_U32(frame),Memory::ReadUnchecked_U32(frame+(271*512+479)*4),Memory::ReadUnchecked_U32(frame+(160*512+240)*4));
  if(ge) { uint32_t hash=2166136261u;for(unsigned i=0;i<512*272*4;++i)hash=(hash^Memory::ReadUnchecked_U8(frame+i))*16777619u;printf("FRAME-HASH %08x\n",hash); }
  // A brief wall-clock tap must survive until a slow guest samples input.
  __CtrlReadLatch();
  __CtrlSetHostState(CTRL_CROSS,0.75f,-0.5f);
  __CtrlSetHostState(0,0.75f,-0.5f);
  for(int i=0;i<1000;++i) {
    coreState=CORE_RUNNING;PSP_BeginHostFrame();PSP_RunLoopFor(10000);PSP_EndHostFrame();
  }
  assert(__CtrlReadLatch()&CTRL_CROSS);
  float ax,ay;__CtrlPeekAnalog(0,&ax,&ay);
  assert(ax>0.7f&&ax<0.8f&&ay>-.55f&&ay<-.45f);
  assert(__CtrlPeekButtons()==0);
  printf("PASS: short host tap latched, analog retained, buttons released\n");
  Wii::RequestEmulationStop(true);coreState=CORE_RUNNING;
  const auto beforeStop=CoreTiming::GetTicks();
  PSP_BeginHostFrame();PSP_RunLoopFor(1000000);PSP_EndHostFrame();
  assert(CoreTiming::GetTicks()-beforeStop<100000);
  Wii::RequestEmulationStop(false);
  printf("PASS: cooperative CPU cancellation returns before a long slice completes\n");
  u8 *reservation=Wii::addressSpace.ram;
  Wii::CpuProfileDump("host-fixture"); PSP_Shutdown();
  for(int reboot=0;reboot<2;++reboot) {
    assert(PSP_Init(p, &error));
    assert(Wii::addressSpace.ram==reservation);
    Wii::CpuProfileDump("host-fixture"); PSP_Shutdown();
  }
  printf("PASS: PSP shutdown/relaunch reuses the RAM reservation\n");
  VFSShutdown(); LogManager::Shutdown(); host = nullptr;
  return passed ? 0 : 4;
}
