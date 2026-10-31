#!/usr/bin/env python3
"""Summarize a Wii ppsspp.log/errors.log without assuming a freeze's cause."""
import argparse
import collections
import json
import re
from pathlib import Path

def summarize(paths):
    counts=collections.Counter();pcs=collections.Counter();issues=collections.deque(maxlen=20)
    last_cpu=last_perf=last_session=None
    last_vertex=last_memory=None
    last_cache_state=last_display=last_session_end=None
    events=0;overwritten=0;lines=0
    for path in paths:
        with Path(path).open(errors='replace') as source:
            for line in source:
                lines+=1;line=line.rstrip()
                if line.startswith('trace seq='):
                    events+=1
                    kind=re.search(r'kind=([^ ]+)',line)
                    if kind:counts[kind[1]]+=1
                    pc=re.search(r'pc=([0-9a-fA-F]+)',line)
                    if pc and kind and kind[1] == 'instruction':pcs[pc[1]]+=1
                if line.startswith('cpu '):last_cpu=line
                if 'PERF fps=' in line:last_perf=line
                if 'VERTEX requests=' in line:last_vertex=line
                if 'MEMORY runtime:' in line:last_memory=line
                if 'STATE pc=' in line:last_cache_state=line
                if 'DISPLAY frame=' in line:last_display=line
                if 'kind=session-end' in line:last_session_end=line
                if 'session-begin' in line:last_session=line
                match=re.search(r'overwritten=(\d+)',line)
                if match:overwritten=max(overwritten,int(match[1]))
                if (line.startswith('log ') and re.search(r'\blevel=[123]\b',line)) or any(word in line.lower() for word in ('watchdog:','gx timeout','failure sequence=','exception')):
                    issues.append(line)
    return dict(files=[str(p) for p in paths],lines=lines,trace_records=events,
                event_counts=dict(counts),most_observed_pcs=pcs.most_common(8),
                overwritten_ring_events=overwritten,last_session=last_session,
                last_cpu=last_cpu,last_performance=last_perf,
                last_vertex=last_vertex,last_memory=last_memory,
                last_cache_state=last_cache_state,last_display=last_display,last_session_end=last_session_end,
                recent_issues=list(issues))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs',nargs='+',type=Path)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args()
    result=summarize(args.logs)
    if args.json:print(json.dumps(result,indent=2));return
    print('wii diagnostic summary')
    for key in ('last_session','last_cpu','last_performance','last_vertex','last_memory','last_cache_state','last_display','last_session_end'):
        if result[key]:print(result[key])
    print(f"trace records: {result['trace_records']}; overwritten recent-history events: {result['overwritten_ring_events']}")
    print('event counts: '+', '.join(f'{k}={v}' for k,v in result['event_counts'].items()))
    print('most observed cpu pcs (observations, not proof of a hang): '+str(result['most_observed_pcs']))
    print('recent errors/warnings:')
    for line in result['recent_issues']:print(line)
    print('repeated trace dumps and overlapping input logs can contain the same event sequence more than once.')
if __name__=='__main__':main()
