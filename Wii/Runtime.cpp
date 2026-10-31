// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/Runtime.h"
#include <atomic>

namespace Wii {
namespace {
std::atomic<bool> stopRequested{false};
std::atomic<uint32_t> beat{0}, progressPC{0};
std::atomic<unsigned> progressPhase{0};
}
void RequestEmulationStop(bool value) { stopRequested.store(value, std::memory_order_relaxed); }
bool EmulationStopRequested() { return stopRequested.load(std::memory_order_relaxed); }
void RuntimeProgress(uint32_t pc, unsigned phase) {
  progressPC.store(pc, std::memory_order_relaxed);
  progressPhase.store(phase, std::memory_order_relaxed);
  beat.fetch_add(1, std::memory_order_relaxed);
}
uint32_t RuntimeBeat() { return beat.load(std::memory_order_relaxed); }
uint32_t RuntimePC() { return progressPC.load(std::memory_order_relaxed); }
unsigned RuntimePhase() { return progressPhase.load(std::memory_order_relaxed); }
}
