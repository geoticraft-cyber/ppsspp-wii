#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import importlib.util,tempfile
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('diagnose',root/'Wii/tools/diagnose_log.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='summary-check-',dir=Path.cwd()) as folder:
    path=Path(folder)/'ppsspp.log'
    path.write_text('''event us=1 kind=session-begin text=game.iso
cpu us=2 pc=08800000 next=0 delay=0 downcount=100 hi=0 lo=0 fcr31=0
log seq=1 us=3 level=3 progress-pc=08800000 phase=1 source.cpp:2 W[BOOT]: WATCHDOG: no core progress
trace-begin reason=error retained=1 overwritten=7 level=1
trace seq=1 us=2 kind=instruction pc=08800000 a=00000020 b=0 c=0
''')
    with path.open('a') as f:
        f.write("VERTEX requests=100 transformed=20 decoded=30 cache-copy-avoided=2048\nSTATE pc=08800000 guest-ticks=100 heap-used=1024 heap-free=2048\nDISPLAY frame=04000000 stride=512 format=3\nevent us=9 kind=session-end text=returned\n")
    result=module.summarize([path])
    assert result['last_vertex']
    assert result['last_cache_state'] and result['last_display'] and result['last_session_end']
    assert result['overwritten_ring_events']==7 and result['event_counts']=={'instruction':1}
    assert result['most_observed_pcs']==[('08800000',1)]
    assert 'game.iso' in result['last_session'] and len(result['recent_issues'])==1
print('pass: diagnostic summary parser, cpu context, warnings, event counts and lost-history accounting')
