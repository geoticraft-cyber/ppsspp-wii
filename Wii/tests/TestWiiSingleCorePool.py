#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the real Wii thread-pool dispatch and verify the direct loop path."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
test_source = r"""
#include "Common/ThreadPools.h"
#include <cassert>
#include <utility>

int main() {
  int calls = 0;
  int sum = 0;
  struct DirectLoop {
    int *calls;
    int *sum;
    DirectLoop(int *c, int *s) : calls(c), sum(s) {}
    DirectLoop(const DirectLoop &) = delete;
    void operator()(int begin, int end) {
      ++*calls;
      for (int i = begin; i < end; ++i)
        *sum += i;
    }
  };
  GlobalThreadPool::Loop(DirectLoop(&calls, &sum), 3, 11);
  assert(calls == 1);
  assert(sum == 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10);
}
"""

with tempfile.TemporaryDirectory(prefix="wii-single-core-pool-") as temp:
    temp = Path(temp)
    cpp = temp / "test.cpp"
    exe = temp / "test"
    cpp.write_text(test_source)
    subprocess.run([
        "g++", "-std=c++11", "-O2", "-DHW_RVL",
        "-I.", "-ICommon", "-Iext/native",
        "Common/ThreadPools.cpp", str(cpp), "-o", str(exe),
    ], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)

print("pass: Wii raster loop executes directly without starting a worker")
