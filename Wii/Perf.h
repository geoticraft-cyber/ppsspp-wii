#pragma once
#include <cstdint>
namespace Wii {
uint64_t PerfTime();
void PerfReset();
void PerfGE(uint64_t start);
void PerfRun(uint64_t start);
void PerfPresent(uint64_t start,bool displayed=true);
struct PerfSnapshot {
  bool valid=false;
  double speed=0,fps=0,cpu=0,ge=0;
  unsigned heapFree=0;
  int clockMHz=0;
};
const PerfSnapshot &GetPerfSnapshot();
bool PerfReport();
}
