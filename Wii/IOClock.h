// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include "Wii/TransferLimits.h"
#if defined(PPSSPP_WII_PORTABLE_TEST)
#include <chrono>
#else
#include <ogc/lwp_watchdog.h>
#endif
namespace Wii {
inline uint64_t IOClockMicros() {
#if defined(PPSSPP_WII_PORTABLE_TEST)
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
#else
  return ticks_to_microsecs(gettime());
#endif
}
}
