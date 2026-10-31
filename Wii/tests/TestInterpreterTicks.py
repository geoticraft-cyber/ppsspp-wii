#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the inline tick expression to the actual upstream GetTicks body."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
body=re.search(r'u64 GetTicks\(\)\s*\{[^}]+\}',(root/'Core/CoreTiming.cpp').read_text()).group()
code='''
#include "Core/CoreTiming.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
struct State {int downcount;};State state;State *currentMIPS=&state;
namespace CoreTiming {
 int slicelength;s64 globalTimer;
'''+body+'''
}
int main() {
  uint32_t seed=0x875328;
  auto random=[&]() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
  for(unsigned i=0;i<200000;++i) {
    CoreTiming::globalTimer=int64_t((uint64_t(random())<<32)|random());
    CoreTiming::slicelength=int32_t(random());state.downcount=int32_t(random());
    assert(CoreTiming::GetTicks()==CoreTiming::GetTicksFromDowncount(state.downcount));
    // A handler may change slice/downcount and advance global time.
    CoreTiming::slicelength=int32_t(random());state.downcount=int32_t(random());
    CoreTiming::globalTimer=int64_t(random());
    assert(CoreTiming::GetTicks()==CoreTiming::GetTicksFromDowncount(state.downcount));
  }
  puts("PASS: 400000 tick comparisons against actual GetTicks, including live slice/time/downcount changes");
}
'''
with tempfile.TemporaryDirectory(prefix='ticks-regression-',dir=Path.cwd()) as folder:
    cpp=Path(folder)/'ticks.cpp';cpp.write_text(code);exe=Path(folder)/'ticks'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
