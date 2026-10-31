#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Sanitize the actual branch dispatcher and integration harness; build portable core first.
Leak scanning is disabled because this host cannot enumerate process threads.
"""
from pathlib import Path
import subprocess,os,tempfile
root=Path(__file__).resolve().parents[2];os.chdir(root);p=root/'Wii/tests/run_io_hle_checks.py';s=p.read_text();ns={'__file__':str(p)};exec(s[:s.index('with concurrent.futures.ThreadPoolExecutor')],ns)
flags=ns['flags']+['-O3','-std=c++11','-fpermissive','-include','Wii/tests/HostConfig.h','-fsanitize=address,undefined','-fno-sanitize-recover=all']
with tempfile.TemporaryDirectory(prefix='branch-sanitized-',dir=ns['out']) as folder:
 folder=Path(folder);objects=[]
 for src in ns['sources']:
  if src in ('Core/MIPS/MIPSTables.cpp','Wii/tests/CoreIntegration.cpp'):
   obj=folder/(Path(src).name+'.o');subprocess.run(['g++',*flags,'-c',src,'-o',str(obj)],check=True);objects.append(obj)
  else:objects.append(ns['out']/(src.replace('/','_')+'.o'))
 (folder/'no-lsan.cpp').write_text('extern \"C\" int __lsan_is_turned_off(){return 1;}\n')
 exe=folder/'check';subprocess.run(['g++','-fsanitize=address,undefined','-Wl,--gc-sections',*map(str,objects),str(folder/'no-lsan.cpp'),'-lz','-lpthread','-o',str(exe)],check=True)
 assert exe.stat().st_size>1000000
 env=dict(os.environ,WII_TEST_DISPATCH='1',ASAN_OPTIONS='detect_leaks=0')
 subprocess.run([str(exe),'smoke.elf',str(root/'assets')],cwd=ns['out']/'test-run',env=env,check=True)
