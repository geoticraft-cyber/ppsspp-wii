#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""interleaved host benchmark of actual retained-frame ReadBlocks calls."""
from pathlib import Path
import subprocess,tempfile,os,statistics,argparse,re
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True,type=Path);p.add_argument('--profile',action='store_true');a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='r3-cso-') as t:
 t=Path(t);bins={}
 for label,source in [('before',a.baseline.resolve()),('after',root)]:
  exe=t/label
  flags=['g++','-std=c++11','-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),'-I'+str(source),'-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native')]
  if a.profile:flags+=['-DWII_CPU_PROFILE']
  subprocess.run(flags+[str(source/'Core/FileSystems/BlockDevices.cpp'),str(root/'Wii/tests/CSOPerformanceTests.cpp'),'-Wl,--wrap=inflateInit2_,--wrap=inflateReset,--wrap=inflateEnd','-lz','-pthread','-o',str(exe)],check=True);bins[label]=exe
 values={k:[] for k in bins}
 for i in range(1 if a.profile else 9):
  for label in (list(bins) if i%2==0 else list(reversed(bins))):
   env=os.environ.copy();env['WII_CSO_BENCH']='1'
   if a.profile:env['WII_CSO_PROFILE']='1'
   text=subprocess.check_output([str(bins[label])],env=env,text=True)
   values[label].append(float(re.search(r'cpu-us=(\d+)',text)[1]))
   if a.profile:print(label+'\n'+text)
 for k,v in values.items():print(k,'median-us',statistics.median(v),'range-us',min(v),max(v))
