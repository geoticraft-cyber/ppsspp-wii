// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
// Only the emulation thread is instrumented on Wii. No audio/interrupt hooks.
// Portable tests use TLS so concurrent decoder tests cannot share scope stacks.
#if defined(WII_CPU_PROFILE)
#include "Common/Log.h"
#if defined(PPSSPP_WII_PORTABLE_TEST)
#include <chrono>
#else
#include <ogc/lwp_watchdog.h>
#endif
#endif
namespace Wii {
enum class CpuPhase : unsigned { Interpreter,
  HLE, IO, Inflate, IOCompletion, Scheduler, Switch, Lock, Analysis, Hash,
  Timing, GE, IOWait, Count };
#if defined(WII_CPU_PROFILE)
inline uint64_t CpuProfileClock() {
#if defined(WII_PROFILE_FAKE_CLOCK)
  extern uint64_t cpuProfileFakeClock;return cpuProfileFakeClock;
#elif defined(PPSSPP_WII_PORTABLE_TEST)
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#else
  return gettime();
#endif
}
inline uint64_t CpuProfileMicros(uint64_t ticks) {
#if defined(PPSSPP_WII_PORTABLE_TEST)
  return ticks/1000;
#else
  return ticks_to_microsecs(ticks);
#endif
}
#include "Wii/CpuProfileImpl.h"
#else
inline void CpuProfileConfigure(bool, unsigned=64) {}
inline void CpuProfileDump(const char *) {}
inline void CpuProfileCount(unsigned, uint64_t=1) {}
class CpuScope {
public:
  explicit CpuScope(CpuPhase, bool=false, uint32_t=0) {}
  void End() {}
  bool Active() const { return false; }
};
#endif
}
