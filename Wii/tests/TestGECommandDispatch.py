#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exhaustively match the fast whitelist to the actual backend's empty cases."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/SoftGpu.cpp').read_text()
s=s[s.index('void SoftGPU::ExecuteOp'):s.index('void SoftGPU::GetStats')]
s=re.sub(r'//[^\n]*','',s)
names=[]
for m in re.finditer(r'((?:\s*case GE_CMD_\w+\s*:)+)\s*break;',s):
    names+=re.findall(r'case (GE_CMD_\w+)',m[1])
assert names
code='''#include "Wii/GECommandDispatch.h"
#include "GPU/ge_constants.h"
#include <cassert>
#include <cstdio>
int main() {
 bool expected[256]={};
'''+''.join(' expected[%s]=true;\n'%name for name in names)+'''
 for(unsigned cmd=0;cmd<256;++cmd)
   assert(Wii::GECommandStoresStateOnly(cmd)==expected[cmd]);
 assert(!Wii::GECommandStoresStateOnly(256));
 assert(!Wii::GECommandStoresStateOnly(~0u));
 puts("pass: all 256 GE commands match actual empty handlers; out-of-range rejected");
}
'''
with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
    cpp=Path(tmp)/'ge.cpp';cpp.write_text(code);exe=Path(tmp)/'ge'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-I'+str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
