#pragma once
#include <cstdint>
namespace Wii {
void RequestEmulationStop(bool value);
bool EmulationStopRequested();
void RuntimeProgress(uint32_t pc,unsigned phase);
uint32_t RuntimeBeat();
uint32_t RuntimePC();
unsigned RuntimePhase();
}
