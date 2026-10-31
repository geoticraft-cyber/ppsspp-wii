#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current-upload baselines, correctness checks and serial host CPU benchmarks.
Run from any directory; needs g++ and python3, never claims console timings.
"""
from pathlib import Path
import csv,platform,statistics,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
logs=root/'Wii/validation-gx-ge-speed';logs.mkdir(exist_ok=True)
def run(name,command):
    with (logs/name).open('w') as out:
        subprocess.run(command,cwd=root,stdout=out,stderr=subprocess.STDOUT,check=True)
    print('pass: '+name,flush=True)
(logs/'environment.txt').write_text(platform.platform()+'\n'+subprocess.check_output(['g++','--version'],text=True))
with tempfile.TemporaryDirectory(prefix='gx-ge-speed-',dir=root) as tmp:
    if '--benchmarks-only' not in sys.argv:
        for source in ['GXPreloadRowTests','GXReadbackTests','OpaqueSpanTests','OpaqueSpanSpeedBenchmark']:
            exe=str(Path(tmp)/source)
            subprocess.run(['g++','-std=c++11','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root),str(root/'Wii/tests'/f'{source}.cpp'),'-o',exe],check=True)
            run(source+'.txt',[exe])
        for script,args in [('TestOpaqueTextureSpan',[]),('TestGXBridge',[]),('TestGXBridge',['--full-upload']),('TestGECommandDispatch',[]),('TestSpritePipeline',['--asan']),('TestGERasterizer',['--asan'])]:
            suffix='-full' if '--full-upload' in args else ''
            run(script+suffix+'.txt',['python3',str(root/'Wii/tests'/f'{script}.py'),*args])
    exe=str(Path(tmp)/'opaque-bench')
    subprocess.run(['g++','-std=c++11','-O3','-DBENCHMARK','-I'+str(root),str(root/'Wii/tests/OpaqueSpanSpeedBenchmark.cpp'),'-o',exe],check=True)
    run('opaque.csv',[exe])
    run('bridge.log',['python3',str(root/'Wii/tests/TestGXBridge.py'),'--speed-benchmark'])
    run('bridge-gate.txt',['python3',str(root/'Wii/tests/CheckGXPolicyBenchmarks.py'),str(logs/'bridge.log')])
rows=[]
for name,prefix,count in [('opaque','opaque',9),('bridge','policy',11)]:
    cells={}
    for line in (logs/(name+('.log' if name=='bridge' else '.csv'))).read_text().splitlines():
        fields=line.split(',')
        if fields[0]!=prefix:continue
        key=tuple(fields[1:-3]);version=fields[-2]
        cells.setdefault(key,{}).setdefault(version,[]).append(float(fields[-1]))
    assert len(cells)==(24 if name=='opaque' else 80)
    for key,versions in cells.items():
        assert set(versions)=={'previous','updated'}
        assert all(len(samples)==count for samples in versions.values())
        old,new=(statistics.median(versions[v]) for v in ['previous','updated'])
        rows.append([name,'/'.join(key),old,new,(old-new)*100/old])
with (logs/'summary.csv').open('w',newline='') as out:
    writer=csv.writer(out);writer.writerow(['suite','case','previous_batch_ms','updated_batch_ms','less_time_percent']);writer.writerows(rows)
print('pass: 104 complete benchmark cells; medians saved in summary.csv')
