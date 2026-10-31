#include "Wii/CpuProfile.h"
#include "Wii/Perf.h"
#include "Wii/AllocationDiagnostics.h"
#include <algorithm>
#include <cstdio>
#include <malloc.h>
#include <gccore.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>
#include "Common/Log.h"
#include "Wii/Diagnostics.h"
#include "Wii/GXBridge.h"
#include "Wii/GraphicsWork.h"
#include "Wii/Video.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "GPU/Software/TransformUnit.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/HLE/sceKernelMemory.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"

namespace Wii {
static uint64_t beginning, guestBeginning, geTime, runTime, presentTime;
static unsigned draws, frames, peakHeapUsed;
static PerfSnapshot snapshot;
static uint64_t sampleStart,sampleGuest;
static uint64_t totalGE,totalRun,totalFrames,lastGE,lastRun,lastFrames;
const PerfSnapshot &GetPerfSnapshot() {return snapshot;}
static void ResetWindow() {
  beginning=PerfTime();guestBeginning=CoreTiming::GetGlobalTimeUs();
  geTime=runTime=presentTime=0;draws=frames=0;
}
static void UpdateSnapshot() {
  const uint64_t now=PerfTime(),span=now-sampleStart;
  if(span<500000)return;
  const uint64_t guest=CoreTiming::GetGlobalTimeUs();
  const uint64_t run=totalRun-lastRun,ge=totalGE-lastGE;
  const double scale=1000000.0/double(span);
  snapshot.valid=true;
  snapshot.speed=guest>=sampleGuest?100.0*double(guest-sampleGuest)/double(span):0;
  snapshot.fps=double(totalFrames-lastFrames)*scale;
  snapshot.cpu=100.0*double(run>ge?run-ge:0)/double(span);
  snapshot.ge=100.0*double(ge)/double(span);
  snapshot.heapFree=unsigned(mallinfo().fordblks);
  snapshot.clockMHz=CoreTiming::GetClockFrequencyMHz();
  sampleStart=now;sampleGuest=guest;
  lastRun=totalRun;lastGE=totalGE;lastFrames=totalFrames;
}

uint64_t PerfTime() { return ticks_to_microsecs(gettime()); }

void PerfReset() {
  ResetWindow();snapshot=PerfSnapshot();
  totalGE=totalRun=totalFrames=lastGE=lastRun=lastFrames=0;
  sampleStart=beginning;sampleGuest=guestBeginning;
  const auto memory=mallinfo();
  AllocationSnapshot(currentMIPS?currentMIPS->pc:0,unsigned(memory.uordblks),unsigned(memory.fordblks),SYS_GetArena1Size(),SYS_GetArena2Size());
}

void PerfGE(uint64_t start) { const auto elapsed=PerfTime()-start;geTime+=elapsed;totalGE+=elapsed;++draws; }
void PerfRun(uint64_t start) { const auto elapsed=PerfTime()-start;runTime+=elapsed;totalRun+=elapsed; }
void PerfPresent(uint64_t start,bool displayed) { presentTime+=PerfTime()-start;if(displayed) {++frames;++totalFrames;} }

static void PerfMemory(const char *reason) {
  const auto memory = mallinfo();
  peakHeapUsed = std::max(peakHeapUsed, unsigned(memory.uordblks));

  // These four calls match the target's runtime memory diagnostic: installed
  // MEM1/MEM2 followed by the currently-unclaimed low/high arena totals.
  const unsigned physicalMEM1 = SYS_GetPhysicalMem1Size();
  const unsigned physicalMEM2 = SYS_GetPhysicalMem2Size();
  const unsigned arenaMEM1 = SYS_GetArena1Size();
  const unsigned arenaMEM2 = SYS_GetArena2Size();
  AllocationSnapshot(currentMIPS?currentMIPS->pc:0,unsigned(memory.uordblks),unsigned(memory.fordblks),arenaMEM1,arenaMEM2);
  INFO_LOG(BOOT,"Failure trace counters: failures=%u unwritten=%u",AllocationFailureCount(),AllocationUnwrittenCount());
  INFO_LOG(BOOT,
    "MEMORY %s: physical MEM1=%u MEM2=%u; unclaimed arenas MEM1=%u MEM2=%u; heap used=%u free=%u peak-used=%u",
    reason, physicalMEM1, physicalMEM2, arenaMEM1, arenaMEM2,
    unsigned(memory.uordblks), unsigned(memory.fordblks), peakHeapUsed);
  INFO_LOG(BOOT,
    "MEMORY bounds: MEM1=[%p,%p) MEM2=[%p,%p); heap accounted=%u; PSP RAM=33554432 VRAM=2097152 (separate reservations when arena-backed)",
    SYS_GetArena1Lo(), SYS_GetArena1Hi(), SYS_GetArena2Lo(), SYS_GetArena2Hi(),
    unsigned(memory.uordblks)+unsigned(memory.fordblks));

  // Exact target threshold: if combined remaining heap space is <= 0x3fffff,
  // drop idle GX readback scratch. It is recreated lazily if later required.
  // Use the actual heap, not the unclaimed MEM2 arena, for this threshold.
  // Unclaimed arena space cannot satisfy malloc without an allocator bridge.
  const uint64_t remaining = uint64_t(unsigned(memory.fordblks));
  if (remaining <= 0x003fffffULL) {
    const unsigned vertexReleased=ReleaseSoftwareVertexCaches();
    if(vertexReleased)INFO_LOG(BOOT,"Memory pressure: released %u bytes of disposable vertex cache",vertexReleased);
    const unsigned released = GXReleaseReadbackScratch();
    if (released)
      INFO_LOG(BOOT, "Memory pressure: released %u bytes of idle GX readback scratch", released);
  }
}

bool PerfReport() {
  UpdateSnapshot();
  const uint64_t span = PerfTime() - beginning;
  if (span < 2000000)
    return false;

  if(!g_Config.bEnableLogging) {
    // Overlay snapshots and memory-pressure handling remain active without
    // expensive log-only GPU/framebuffer/register reports.
    PerfMemory("runtime");ResetWindow();return false;
  }
  CpuProfileDump("runtime");
  const uint64_t cpu = runTime > geTime ? runTime - geTime : 0;
  INFO_LOG(BOOT, "PERF fps=%.2f CPU/HLE=%.1f%% GE=%.1f%% present=%.1f%% draws=%u (wall %.2fs)",
    double(frames) * 1000000.0 / double(span),
    100.0 * double(cpu) / double(span),
    100.0 * double(geTime) / double(span),
    100.0 * double(presentTime) / double(span), draws, double(span) / 1000000.0);

  const uint64_t guestNow = CoreTiming::GetTicks();
  const uint64_t guestUs=CoreTiming::GetGlobalTimeUs();
  const double guestSpeed=guestUs>=guestBeginning?100.0*double(guestUs-guestBeginning)/double(span):0;
  INFO_LOG(BOOT, "SPEED guest=%.2f%%", guestSpeed);
  INFO_LOG(BOOT, "PSP user RAM: free=%u largest-free-block=%u (guest allocator; not Wii heap)",
    userMemory.GetTotalFreeBytes(), userMemory.GetLargestFreeBlockSize());

  const auto memory = mallinfo();
  INFO_LOG(BOOT, "STATE pc=%08x guest-ticks=%llu heap-used=%u heap-free=%u",
    currentMIPS->pc, (unsigned long long)guestNow,
    unsigned(memory.uordblks), unsigned(memory.fordblks));

  if (gpu) {
    char stats[128] = {};
    gpu->GetStats(stats, sizeof(stats));
    INFO_LOG(BOOT, "GPU %s", stats);
  }

  INFO_LOG(BOOT,"diagnostic-log-io-failures=%u",DiagnosticIOFailures());
  DiagnosticCPU(currentMIPS->pc,currentMIPS->r,currentMIPS->hi,currentMIPS->lo,currentMIPS->fcr31,currentMIPS->downcount,currentMIPS->inDelaySlot,currentMIPS->nextPC);
  DumpDiagnosticTrace("heartbeat");
  const auto &graphics=GetGraphicsWork();
  INFO_LOG(BOOT,"GX upload-bytes=%llu avoided-bytes=%llu CPU texel-hits=%llu misses=%llu",(unsigned long long)graphics.gxUploadBytes,(unsigned long long)graphics.gxUploadSaved,(unsigned long long)graphics.cpuTexelHits,(unsigned long long)graphics.cpuTexelMisses);
  INFO_LOG(BOOT,"GX tev-add=%llu tev-double=%llu native16-sprites=%llu",(unsigned long long)graphics.gxTevAdd,(unsigned long long)graphics.gxTevDouble,(unsigned long long)graphics.gxNative16Sprites);
  INFO_LOG(BOOT,"GX advanced baked=%llu blend=%llu logic=%llu coverage=%llu masked=%llu",(unsigned long long)graphics.gxBaked,(unsigned long long)graphics.gxBlend,(unsigned long long)graphics.gxLogic,(unsigned long long)graphics.gxCoverage,(unsigned long long)graphics.gxMasked);
  INFO_LOG(BOOT,"GRAPHICS sprites=%llu gx-attempts=%llu gx-accepted=%llu palette-attempts=%llu cpu-flat-pixels=%llu cpu-texture-pixels=%llu triangles=%llu pixel-state-fallbacks=%llu",(unsigned long long)graphics.spriteCalls,(unsigned long long)graphics.gxAttempts,(unsigned long long)graphics.gxAccepted,(unsigned long long)graphics.paletteAttempts,(unsigned long long)graphics.flatPixels,(unsigned long long)graphics.texturePixels,(unsigned long long)graphics.triangles,(unsigned long long)graphics.pixelStateFallbacks);
  INFO_LOG(BOOT,"GE pixel-slices prepared=%llu reference=%llu alpha=%llu stencil=%llu fog=%llu packed=%llu",
    (unsigned long long)graphics.preparedPixelSlices,(unsigned long long)graphics.referencePixelSlices,
    (unsigned long long)graphics.alphaPixelSlices,(unsigned long long)graphics.stencilPixelSlices,
    (unsigned long long)graphics.fogPixelSlices,(unsigned long long)graphics.packedPixelSlices);
  INFO_LOG(BOOT,"VERTEX requests=%llu transformed=%llu decoded=%llu cache-copy-avoided=%llu cache-bytes=%u cache-peak=%u decode-scratch-peak=%u allocation-retries=%u",
    (unsigned long long)graphics.vertexRequests,(unsigned long long)graphics.verticesTransformed,
    (unsigned long long)graphics.verticesDecoded,(unsigned long long)graphics.vertexCopyAvoided,
    SoftwareVertexCacheBytes(),graphics.vertexCachePeak,graphics.decodeScratchPeak,graphics.decodeRetries);
  INFO_LOG(BOOT,"GRAPHICS prepared-lighting-draws=%llu triangle-slices=%llu",
    (unsigned long long)graphics.preparedLightingDraws,(unsigned long long)graphics.triangleSlices);
  INFO_LOG(BOOT,"GX display-converted-pixels=%llu source-skipped-pixels=%llu source-memo-bytes=%u",
    (unsigned long long)graphics.gxDisplayConvertedPixels,(unsigned long long)graphics.gxSourceSkippedPixels,graphics.gxSourceMemoBytes);
  INFO_LOG(BOOT,"GX readback-bytes=%llu",(unsigned long long)graphics.gxReadbackBytes);
  WiiVideoReport();
  PerfMemory("runtime");
  ResetWindow();
  return true;
}
}
