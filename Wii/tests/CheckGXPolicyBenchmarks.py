#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Gate all measured GX mock workloads against the user's strict <8% limit.
Usage: CheckGXPolicyBenchmarks.py path/to/logic-benchmark.log
Host command-mock timings do not establish Wii hardware timings.
"""
import csv,re,statistics,sys
from pathlib import Path
p=Path(sys.argv[1]);rows=re.findall(r'policy,(\d+),(\d+),(\d+),(\w+),([\d.]+)',p.read_text())
assert len(rows)==4*20*11*2,len(rows)
summary=[]
for fmt in range(4):
 for mode in range(20):
  med={}
  for name in ('previous','updated'):
   samples=[float(ms)for f,m,r,n,ms in rows if int(f)==fmt and int(m)==mode and n==name]
   assert len(samples)==11
   med[name]=statistics.median(samples)
  percent=(med['updated']/med['previous']-1)*100
  summary.append([fmt,mode,med['previous'],med['updated'],percent])
with p.with_suffix('.csv').open('w')as f:
 w=csv.writer(f);w.writerow(['format','workload','preceding_ms','updated_ms','percent_more_time']);w.writerows(summary)
failures=[r for r in summary if r[4]>=8]
print('host command mock: 80 workloads, 11 alternating rounds, 12 sprites per sample')
print('lower medians: %u/80; this does not establish broad real-game benefit'%sum(r[4]<0 for r in summary))
print('maximum regression: %.3f%%'%max(r[4]for r in summary))
for mode in (0,3,5,12,15):
 gains=[-r[4]for r in summary if r[1]==mode]
 print('logic %u: %.2f to %.2f%% less host mock time'%(mode,min(gains),max(gains)))
print('gate: '+('FAIL '+str(failures)if failures else 'PASS: every measured regression is below 8%'))
if failures:raise SystemExit(1)
